// SystemServicesManager.swift
// Handles XPC, daemon, and driver system service logic
import Combine
import AppKit
import Foundation
import Combine
import ServiceManagement
import Logging
import AVFoundation // <-- Add AVFoundation import

actor SystemServicesManager {
    // --- System Status Struct ---
    struct SystemStatus: Equatable {
        var isXpcConnected: Bool = false
        var isDriverConnected: Bool = false
        var daemonInstallStatus: SMAppService.Status = .notFound
        var showDriverInstallPrompt: Bool = false
        var showDaemonInstallPrompt: Bool = false
        // *** NEW: Add permission status ***
        var cameraPermissionStatus: AVAuthorizationStatus = .notDetermined
    }

    // --- Internal State ---
    // Initialize currentStatus with the initial permission status
    private var currentStatus = SystemStatus(
        cameraPermissionStatus: AVCaptureDevice.authorizationStatus(for: .video)
    )
    // *** NEW: Publisher for internal status ***
    // Use CurrentValueSubject to immediately provide state on subscription
    // Needs to be initialized carefully due to actor isolation
    private lazy var statusSubject = CurrentValueSubject<SystemStatus, Never>(currentStatus)

    // --- Dependencies ---
    private let engineService: EngineService
    private let xpcManager: XPCManager // Keep declaration
    private let driverInstallService = DriverInstallerService() // Instance exists
    private let permissionManager: PermissionManager
    // DaemonServiceManager instance (@MainActor)
    private let daemonServiceManager: DaemonServiceManager

    // --- Internal State ---
    private var wasInBackground: Bool = false
    private var cancellables = Set<AnyCancellable>()

    private let logger = AppLoggers.system

    // *** NEW: Public Nonisolated Publisher ***
    var systemStatusPublisher: AnyPublisher<SystemStatus, Never> {
        statusSubject.eraseToAnyPublisher()
    }

    // --- Initialization ---
    // Ensure this is the ONLY init definition
    init(engineService: EngineService, permissionManager: PermissionManager, daemonServiceManager: DaemonServiceManager) {
        self.engineService = engineService
        // --- MODIFICATION: Pass engineService to XPCManager ---
        self.xpcManager = XPCManager(engineService: engineService) // Pass it here
        self.permissionManager = permissionManager
        self.daemonServiceManager = daemonServiceManager
        logger.info("SystemServicesManager initializing.")
        
        // Set up bidirectional reference between EngineService and XPCManager
        Task {
            await engineService.setXPCManager(self.xpcManager)
        }
        
        Task {
            // Check permissions early AND update status
            _ = await checkAndRequestCameraPermission() // This now updates currentStatus
            // Continue setup
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

        // --- ENSURE THIS TASK EXISTS AND CALLS updateDriverStatus ---
        Task { [weak self] in
            guard let self = self else { return }
            // Get the stream from the XPCManager instance
            let stream = self.xpcManager.driverStatusStream
            // Loop through updates from the stream
            for await driverIsUp in stream {
                // Call the status update method within the actor
                await self.updateDriverStatus(driverIsUp)
            }
            // Log when the stream finishes (optional, indicates disconnection)
            self.logger.info("XPC driverStatusStream finished.")
        }
        // --- END ENSURE ---

        // --- Keep other stream subscriptions (device status, config, log) ---
        Task { [weak self] in
            guard let self = self else { return }
            let stream = self.xpcManager.deviceStatusStream // Assuming this stream exists
            for await _ in stream {
                // Example: await self.updateDeviceStatus(statusUpdate) // Call appropriate handler
            }
            self.logger.info("XPC deviceStatusStream finished.")
        }

        Task { [weak self] in
            guard let self = self else { return }
            let stream = self.xpcManager.deviceConfigStream // Assuming this stream exists
             for await _ in stream {
                // Example: await self.updateDeviceConfig(configUpdate) // Call appropriate handler
             }
            self.logger.info("XPC deviceConfigStream finished.")
        }
        // Removed logMessageStream subscription as per previous step

        logger.info("XPC bindings setup complete.")
    }

    // Helper method to store cancellable on the actor
    private func storeCancellable(_ c: AnyCancellable) {
        cancellables.insert(c)
    }

    // --- DaemonServiceManager Bindings ---
    private func setupDaemonServiceBindings() async {
        await Task { @MainActor [weak self] in
            guard let self = self else { return }
            // Subscription 1
            let c1 = daemonServiceManager.$status
                .receive(on: DispatchQueue.main)
                .sink { [weak self] newSMStatus in
                    Task { await self?.updateDaemonSMStatus(newSMStatus) }
                }
            Task { await self.storeCancellable(c1) }
            // Subscription 2
            let c2 = daemonServiceManager.$requiresUserAction
                .receive(on: DispatchQueue.main)
                .sink { [weak self] requiresAction in
                    Task { await self?.updateDaemonRequiresUserAction(requiresAction) }
                }
            Task { await self.storeCancellable(c2) }
            // Subscription 3 (no if-let, use directly)
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
        let success = await xpcManager.connect()
        await self.updateXpcConnectionStatus(success)
        if success {
            logger.info("XPC connected successfully after DSM signal.")
        } else {
            logger.error("XPC connection failed after DSM signal.")
        }
    }

    // --- Actor-isolated state update methods ---
    // --- State Update Helper ---
    // *** MODIFIED: Include permission status in log ***
    private func updateStatus(_ newStatus: SystemStatus) {
        if newStatus != currentStatus {
            currentStatus = newStatus
            logger.info("System Status Updated: XPC=\(currentStatus.isXpcConnected), Driver=\(currentStatus.isDriverConnected), Daemon=\(currentStatus.daemonInstallStatus), CamPerm=\(currentStatus.cameraPermissionStatus), ShowDaemonPrompt=\(currentStatus.showDaemonInstallPrompt), ShowDriverPrompt=\(currentStatus.showDriverInstallPrompt)")
            // *** Publish the new status ***
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

    // --- App Lifecycle Observers (optional) ---
    private func setupAppLifecycleObserver() async {
        logger.info("Setting up App Lifecycle Observers")
        await Task { @MainActor [weak self] in
            guard let self = self else { return }
            // Notif 1
            let c1 = NotificationCenter.default.publisher(for: NSApplication.didBecomeActiveNotification)
                .receive(on: DispatchQueue.main)
                .sink { [weak self] _ in Task { await self?.handleAppDidBecomeActive() } }
            Task { await self.storeCancellable(c1) }
            // Notif 2
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

    // --- System Status Getter (for UIManager polling) ---
    func getCurrentSystemStatus() -> SystemStatus {
        return currentStatus
    }

    // TODO: Add methods for daemon installation, driver installation, etc.
    // *** UPDATED installDriver method ***
    func installDriver() async {
        logger.info("Install driver requested via SystemServicesManager.")
        do {
            // Call the method on the service instance
            try await driverInstallService.installDriverFromBundle()
            logger.info("Driver installation process completed successfully (reported by service).")
            // Optional: Trigger driver status re-check via XPC?
            // await self.checkDriverStatusViaXPC() // Add helper if needed later
        } catch let error as DriverInstallError {
            logger.error("Driver installation failed: \(error.localizedDescription)")
            // TODO: Publish specific error state for UI?
            // Example: Update internal status
            // var newStatus = currentStatus
            // newStatus.lastError = error // Add error state to SystemStatus struct
            // updateStatus(newStatus)
        } catch {
            logger.error("Unexpected error during driver installation: \(error.localizedDescription)")
            // TODO: Publish generic error state?
        }
    }
    // Optional helper to explicitly check driver status if XPC stream isn't immediate
    private func checkDriverStatusViaXPC() async {
        logger.warning("checkDriverStatusViaXPC() not fully implemented (needs XPC method).")
    }
    // *** END UPDATED installDriver method ***

    // --- Hardware Command Forwarding (IMPLEMENTING ALL) ---

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
    // FIXED: Proper async permission checking
    private func checkAndRequestCameraPermission() async -> Bool {
        // FIXED: Direct async call without completion handlers
        let granted = await Task { @MainActor in
            await permissionManager.checkAndRequestCameraPermission()
        }.value

        // Update status after permission check completes
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
    // --- END NEW ---
}
