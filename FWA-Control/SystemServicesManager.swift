// SystemServicesManager.swift
// Handles XPC, daemon, and driver system service logic
import Combine
import AppKit
import Foundation
import ServiceManagement
import Logging
import AVFoundation

actor SystemServicesManager {
    // --- System Status Struct ---
    struct SystemStatus: Equatable {
        var isXpcConnected: Bool = false
        var isDriverConnected: Bool = false
        var daemonInstallStatus: SMAppService.Status = .notFound
        var showDriverInstallPrompt: Bool = false
        var showDaemonInstallPrompt: Bool = false
        var cameraPermissionStatus: AVAuthorizationStatus = .notDetermined
    }

    // --- Internal State ---
    private var currentStatus = SystemStatus(
        cameraPermissionStatus: AVCaptureDevice.authorizationStatus(for: .video)
    )
    private lazy var statusSubject = CurrentValueSubject<SystemStatus, Never>(currentStatus)

    // --- Dependencies ---
    private let engineService: EngineService
    private let xpcManager: XPCManager
    private let driverInstallService = DriverInstallerService()
    private let permissionManager: PermissionManager
    private let daemonServiceManager: DaemonServiceManager

    // --- Internal State ---
    private var wasInBackground: Bool = false
    private var cancellables = Set<AnyCancellable>()

    private let logger = AppLoggers.system

    // *** Public Nonisolated Publisher ***
    var systemStatusPublisher: AnyPublisher<SystemStatus, Never> {
        statusSubject.eraseToAnyPublisher()
    }

    // *** Public XPCManager accessor for diagnostics ***
    nonisolated var xpcManagerAccess: XPCManager {
        xpcManager
    }

    // --- Initialization ---
    init(engineService: EngineService, permissionManager: PermissionManager, daemonServiceManager: DaemonServiceManager) {
        self.engineService = engineService
        self.xpcManager = XPCManager(engineService: engineService)
        self.permissionManager = permissionManager
        self.daemonServiceManager = daemonServiceManager
        logger.info("SystemServicesManager initializing.")
        
        Task {
            await engineService.setXPCManager(self.xpcManager)
        }
        
        Task {
            _ = await checkAndRequestCameraPermission()
            await setupXPCBindings()
            await setupDaemonServiceBindings()
            await setupAppLifecycleObserver()
            await Task { @MainActor in daemonServiceManager.checkStatus(isInitialCheck: true) }.value
            let status = await self.currentStatus
            logger.info("SystemServicesManager initialization complete. Initial Status: \(status)")
        }
    }

    // --- XPC Bindings ---
    private func setupXPCBindings() async {
        logger.info("Setting up XPC bindings...")

        Task { [weak self] in
            guard let self = self else { return }
            let stream = self.xpcManager.driverStatusStream
            for await driverIsUp in stream {
                await self.updateDriverStatus(driverIsUp)
            }
            self.logger.info("XPC driverStatusStream finished.")
        }

        Task { [weak self] in
            guard let self = self else { return }
            let stream = self.xpcManager.deviceStatusStream
            for await _ in stream {
                // Handle device status updates if needed
            }
            self.logger.info("XPC deviceStatusStream finished.")
        }

        Task { [weak self] in
            guard let self = self else { return }
            let stream = self.xpcManager.deviceConfigStream
             for await _ in stream {
                // Handle device config updates if needed
             }
            self.logger.info("XPC deviceConfigStream finished.")
        }

        logger.info("XPC bindings setup complete.")
    }

    private func storeCancellable(_ c: AnyCancellable) {
        cancellables.insert(c)
    }

    // --- DaemonServiceManager Bindings ---
    private func setupDaemonServiceBindings() async {
        await Task { @MainActor [weak self] in
            guard let self = self else { return }
            
            let c1 = daemonServiceManager.$status
                .receive(on: DispatchQueue.main)
                .sink { [weak self] newSMStatus in
                    Task { await self?.updateDaemonSMStatus(newSMStatus) }
                }
            Task { await self.storeCancellable(c1) }
            
            let c2 = daemonServiceManager.$requiresUserAction
                .receive(on: DispatchQueue.main)
                .sink { [weak self] requiresAction in
                    Task { await self?.updateDaemonRequiresUserAction(requiresAction) }
                }
            Task { await self.storeCancellable(c2) }
            
            let connectionNeededPublisher = daemonServiceManager.connectionNeededPublisher
            let c3 = connectionNeededPublisher
                .receive(on: DispatchQueue.main)
                .sink { [weak self] _ in
                    Task { await self?.attemptXPCConnectionIfNeeded() }
                }
            Task { await self.storeCancellable(c3) }
        }.value
        logger.info("Subscribed to DaemonServiceManager publishers.")
    }

    // --- Daemon Status Update Handlers ---
    private func updateDaemonSMStatus(_ smStatus: SMAppService.Status) {
        logger.debug("Received DSM status update: \(smStatus)")
        var newStatus = currentStatus
        if newStatus.daemonInstallStatus != smStatus {
            newStatus.daemonInstallStatus = smStatus
            updateStatus(newStatus)
        }
    }

    private func updateDaemonRequiresUserAction(_ requiresAction: Bool) {
        logger.debug("Received DSM requiresUserAction update: \(requiresAction)")
        var newStatus = currentStatus
        if newStatus.showDaemonInstallPrompt != requiresAction {
            newStatus.showDaemonInstallPrompt = requiresAction
            updateStatus(newStatus)
        }
    }

    private func attemptXPCConnectionIfNeeded() async {
        logger.info("Attempting XPC connection based on signal from DaemonServiceManager...")
        guard !currentStatus.isXpcConnected else {
            logger.info("XPC connection attempt skipped: Already connected.")
            return
        }
        do {
            let success = try await xpcManager.connectAndInitializeDaemonEngine()
            await self.updateXpcConnectionStatus(success)
            if success {
                logger.info("XPC connected successfully after DSM signal.")
            } else {
                logger.error("XPC connection failed after DSM signal.")
            }
        } catch {
            logger.error("XPC connection failed with error: \(error)")
            await self.updateXpcConnectionStatus(false)
        }
    }

    // --- State Update Helper ---
    private func updateStatus(_ newStatus: SystemStatus) {
        if newStatus != currentStatus {
            currentStatus = newStatus
            logger.info("System Status Updated: XPC=\(currentStatus.isXpcConnected), Driver=\(currentStatus.isDriverConnected), Daemon=\(currentStatus.daemonInstallStatus), CamPerm=\(currentStatus.cameraPermissionStatus), ShowDaemonPrompt=\(currentStatus.showDaemonInstallPrompt), ShowDriverPrompt=\(currentStatus.showDriverInstallPrompt)")
            statusSubject.send(currentStatus)
        }
    }

    // --- Driver Status Update ---
    private func updateDriverStatus(_ isConnected: Bool) {
        var newStatus = currentStatus
        logger.debug("Updating driver status internally to: \(isConnected)")

        if newStatus.isDriverConnected != isConnected {
            newStatus.isDriverConnected = isConnected
            newStatus.showDriverInstallPrompt = !isConnected && newStatus.isXpcConnected
            logger.debug("Updating showDriverInstallPrompt to: \(newStatus.showDriverInstallPrompt)")
            updateStatus(newStatus)
        } else {
            logger.trace("Driver status received (\(isConnected)) but no change needed.")
        }
    }

    // --- XPC Status Update ---
    private func updateXpcConnectionStatus(_ isConnected: Bool) async {
        var newStatus = currentStatus
        if newStatus.isXpcConnected != isConnected {
            newStatus.isXpcConnected = isConnected
            newStatus.showDriverInstallPrompt = !newStatus.isDriverConnected && isConnected
            updateStatus(newStatus)
            await Task { @MainActor in
                daemonServiceManager.updateXpcConnectionStatus(isConnected)
            }.value
        }
    }

    // --- App Lifecycle Observers ---
    private func setupAppLifecycleObserver() async {
        logger.info("Setting up App Lifecycle Observers")
        await Task { @MainActor [weak self] in
            guard let self = self else { return }
            
            let c1 = NotificationCenter.default.publisher(for: NSApplication.didBecomeActiveNotification)
                .receive(on: DispatchQueue.main)
                .sink { [weak self] _ in Task { await self?.handleAppDidBecomeActive() } }
            Task { await self.storeCancellable(c1) }
            
            let c2 = NotificationCenter.default.publisher(for: NSApplication.willResignActiveNotification)
                .receive(on: DispatchQueue.main)
                .sink { [weak self] _ in Task { await self?.handleAppWillResignActive() } }
            Task { await self.storeCancellable(c2) }
        }.value
    }

    private func handleAppDidBecomeActive() async {
        logger.debug("App became active.")
        let wasInBackgroundBefore = self.wasInBackground
        self.wasInBackground = false
        if wasInBackgroundBefore {
            logger.info("App activated from background, checking daemon status via DSM...")
            await Task { @MainActor in
                daemonServiceManager.checkStatus()
            }.value
        }
    }

    private func handleAppWillResignActive() async {
        logger.debug("App resigning active.")
        self.wasInBackground = true
    }

    // --- Daemon Registration Actions ---
    func registerDaemon() async -> Bool {
        logger.info("SystemServicesManager: Register daemon requested.")
        return await Task { @MainActor in
            await daemonServiceManager.registerDaemon()
        }.value
    }

    func unregisterDaemon() async -> Bool {
        logger.info("SystemServicesManager: Unregister daemon requested.")
        return await Task { @MainActor in
            await daemonServiceManager.unregisterDaemon()
        }.value
    }

    // --- System Status Getter ---
    func getCurrentSystemStatus() -> SystemStatus {
        return currentStatus
    }

    // --- Driver Installation ---
    func installDriver() async {
        logger.info("Install driver requested via SystemServicesManager.")
        do {
            try await driverInstallService.installDriverFromBundle()
            logger.info("Driver installation process completed successfully (reported by service).")
        } catch let error as DriverInstallError {
            logger.error("Driver installation failed: \(error.localizedDescription)")
        } catch {
            logger.error("Unexpected error during driver installation: \(error.localizedDescription)")
        }
    }

    // --- Hardware Command Forwarding ---
    func performSetSampleRate(guid: UInt64, rate: Double) async -> Bool {
        logger.info("SystemServicesManager: Forwarding SetSampleRate request to EngineService...")
        return await engineService.setSampleRate(guid: guid, rate: rate)
    }

    func performStartIO(guid: UInt64) async -> Bool {
        logger.info("SystemServicesManager: Forwarding StartIO request to EngineService...")
        return await engineService.startIO(guid: guid)
    }

    func performStopIO(guid: UInt64) async {
        logger.info("SystemServicesManager: Forwarding StopIO request to EngineService...")
        await engineService.stopIO(guid: guid)
    }

    func performSetVolume(guid: UInt64, scope: UInt32, element: UInt32, value: Float) async -> Bool {
        logger.info("SystemServicesManager: Forwarding SetVolume request to EngineService...")
        return await engineService.setVolume(guid: guid, scope: scope, element: element, value: value)
    }

    func performSetMute(guid: UInt64, scope: UInt32, element: UInt32, state: Bool) async -> Bool {
        logger.info("SystemServicesManager: Forwarding SetMute request to EngineService...")
        return await engineService.setMute(guid: guid, scope: scope, element: element, state: state)
    }

    func performSetClockSource(guid: UInt64, sourceID: UInt32) async -> Bool {
        logger.info("SystemServicesManager: Forwarding SetClockSource request to EngineService...")
        return await engineService.setClockSource(guid: guid, sourceID: sourceID)
    }

    // MARK: - Permissions
    private func checkAndRequestCameraPermission() async -> Bool {
        let granted = await Task { @MainActor in
            await permissionManager.checkAndRequestCameraPermission()
        }.value

        let latestStatus = await Task { @MainActor in
            permissionManager.cameraPermissionStatus
        }.value

        logger.info("Permission check complete. Granted: \(granted), Final Status: \(latestStatus)")
        var newStatus = currentStatus
        if newStatus.cameraPermissionStatus != latestStatus {
            newStatus.cameraPermissionStatus = latestStatus
            updateStatus(newStatus)
        }
        return granted
    }

    func triggerPermissionCheck() async -> Bool {
        logger.info("Manual permission check triggered.")
        return await checkAndRequestCameraPermission()
    }
}