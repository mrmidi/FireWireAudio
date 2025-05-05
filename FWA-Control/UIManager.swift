// UIManager.swift
// Central UI state manager for FWA-Control

import Foundation
@preconcurrency import Combine
import SwiftUI
import ServiceManagement
import Logging

@MainActor
final class UIManager: ObservableObject {

    // --- Published Properties for UI State ---
    @Published var isRunning: Bool = false // Engine running state
    @Published var devices: [UInt64: DeviceInfo] = [:] // Discovered device info
    private(set) var deviceJsons: [UInt64: String] = [:] // Final JSON per device

    // Connection & System Status (Reflects state from SystemServicesManager)
    @Published var isDaemonConnected = false
    @Published var isDriverConnected = false
    @Published var daemonInstallStatus: SMAppService.Status = .notFound

    // Prompts (Reflects state from SystemServicesManager)
    @Published var showDriverInstallPrompt = false
    @Published var showDaemonInstallPrompt = false

    // Dependencies (Injected)
    private let engineService: EngineService? // Keep private if only used internally
    // --- FIX: Change access level ---
    internal let systemServicesManager: SystemServicesManager? // Changed from private
    internal unowned let logStore: LogStore? // Already internal

    private var cancellables = Set<AnyCancellable>()
    private let logger = AppLoggers.app // Dedicated UIManager logger

    // --- Initialization ---
    init(engineService: EngineService?, systemServicesManager: SystemServicesManager?, logStore: LogStore?) {
        self.engineService = engineService
        self.systemServicesManager = systemServicesManager
        self.logStore = logStore
        if let engineService = engineService {
            subscribeToEngineService(service: engineService)
        } else {
            logger.error("UIManager initialized without a valid EngineService.")
        }
        if let systemServicesManager = systemServicesManager {
            subscribeToSystemServices(service: systemServicesManager)
        } else {
            logger.error("UIManager initialized without a valid SystemServicesManager.")
        }
        logger.info("UIManager initialized.")
    }
    
    private func subscribeToEngineService(service: EngineService) {
        Task {
            // --- FIX: Subscribe directly to publisher ---
            await service.$isRunning
                .receive(on: RunLoop.main)
                .sink { [weak self] (runningState: Bool) in
                    guard let self = self else { return }
                    if self.isRunning != runningState {
                        self.isRunning = runningState
                        self.logger.debug("UIManager received Engine isRunning state: \(runningState)")
                    }
                }
                .store(in: &cancellables)

            await service.$devices
                .receive(on: RunLoop.main)
                .sink { [weak self] (deviceMap: [UInt64: DeviceInfo]) in
                    guard let self = self else { return }
                    if self.devices != deviceMap {
                        self.devices = deviceMap
                        self.logger.debug("UIManager received updated Devices map (count: \(deviceMap.count))")
                    }
                }
                .store(in: &cancellables)

            logger.info("Subscribed to EngineService publishers.")
        }
    }

    private func subscribeToSystemServices(service: SystemServicesManager) {
        Task {
            let statusPublisher = await service.systemStatusPublisher
            statusPublisher
                .receive(on: RunLoop.main)
                .sink { [weak self] status in
                    guard let self = self else { return }
                    logger.trace("Received SystemStatus update: \(status)")
                    // Update UI state based on SystemStatus
                    if self.isDaemonConnected != status.isXpcConnected { self.isDaemonConnected = status.isXpcConnected }
                    if self.isDriverConnected != status.isDriverConnected { self.isDriverConnected = status.isDriverConnected }
                    if self.daemonInstallStatus != status.daemonInstallStatus { self.daemonInstallStatus = status.daemonInstallStatus }
                    if self.showDriverInstallPrompt != status.showDriverInstallPrompt { self.showDriverInstallPrompt = status.showDriverInstallPrompt }
                    if self.showDaemonInstallPrompt != status.showDaemonInstallPrompt { self.showDaemonInstallPrompt = status.showDaemonInstallPrompt }
                }
                .store(in: &cancellables)


            logger.info("Subscribed to SystemServicesManager status publisher.")
        }
    }

    // --- UI Actions (Delegated to Services) ---


    func startEngine() {
        logger.info("UIManager: Start engine requested.")
        guard let engineService = engineService else {
            logger.error("Cannot start engine: EngineService is nil.")
            return
        }
        Task {
            let success = await engineService.start()
            logger.info("EngineService start() returned: \(success)")
            // isRunning state will update via the publisher subscription
        }
    }

    func stopEngine() {
        logger.info("UIManager: Stop engine requested.")
        guard let engineService = engineService else {
            logger.error("Cannot stop engine: EngineService is nil.")
            return
        }
        Task {
            let success = await engineService.stop()
            logger.info("EngineService stop() returned: \(success)")
            // isRunning and devices state will update via the publisher subscriptions
        }
    }

    func refreshDevice(guid: UInt64) {
        logger.info("UIManager: Refresh device requested for GUID 0x\(String(format: "%llX", guid)).")
        guard let engineService = engineService else {
            logger.error("Cannot refresh device: EngineService is nil.")
            return
        }
        Task {
            await engineService.fetchAndAddOrUpdateDevice(guid: guid)
            // devices state will update via the publisher subscription
        }
    }

    func refreshAllDevices() {
        logger.info("UIManager: Refresh all devices requested.")
        guard let engineService = engineService else {
            logger.error("Cannot refresh all devices: EngineService is nil.")
            return
        }
        Task {
            await engineService.refreshAllDevices()
            logger.info("UIManager: Refresh all devices complete action triggered.")
            // devices state will update via the publisher subscription
        }
    }

    func sendAVCCommand(guid: UInt64, command: Data) async -> Data? {
        logger.info("UIManager: Send AV/C command requested for GUID 0x\(String(format: "%llX", guid)).")
        guard let engineService = engineService else {
            logger.error("Cannot send AV/C command: EngineService is nil.")
            return nil
        }
        let response = await engineService.sendCommand(guid: guid, command: command)
        if response == nil {
            logger.warning("UIManager: engineService.sendCommand() returned nil.")
        }
        return response
    }

    func installDriver() {
        logger.info("UIManager: Install driver requested.")
        guard let systemServicesManager = systemServicesManager else {
            logger.error("Cannot install driver: SystemServicesManager is nil.")
            return
        }
        Task {
            await systemServicesManager.installDriver()
            // State (driver status, prompt) should update via polling for now
        }
    }

    func registerDaemon() async -> Bool { // Add convenience methods to UIManager
        logger.info("UIManager: Register daemon requested.")
        guard let system = systemServicesManager else {
            logger.error("Cannot register daemon: SystemServicesManager is nil.")
            return false
        }
        return await system.registerDaemon()
    }

    func unregisterDaemon() async -> Bool { // Add convenience methods to UIManager
        logger.info("UIManager: Unregister daemon requested.")
        guard let system = systemServicesManager else {
            logger.error("Cannot unregister daemon: SystemServicesManager is nil.")
            return false
        }
        return await system.unregisterDaemon()
    }

    // --- Stream Control Actions (NEW) ---
    func startStreams(guid: UInt64) {
        logger.info("UIManager: Start streams requested for GUID 0x\(String(format: "%llX", guid)).")
        guard let engineService = engineService else {
            logger.error("Cannot start streams: EngineService is nil.")
            return
        }
        guard isRunning else {
            logger.warning("Cannot start streams: Engine is not running.")
            // Optionally show an alert to the user
            return
        }
        Task {
            let success = await engineService.startStreams(guid: guid)
            logger.info("UIManager: EngineService startStreams(guid: 0x\(String(format: "%llX", guid))) returned: \(success)")
            // TODO: Update UI based on success/failure if needed (e.g., show alert)
        }
    }

    func stopStreams(guid: UInt64) {
        logger.info("UIManager: Stop streams requested for GUID 0x\(String(format: "%llX", guid)).")
        guard let engineService = engineService else {
            logger.error("Cannot stop streams: EngineService is nil.")
            return // Exit if engineService is nil
        }

        // --- FIX: Replace guard with if for warning ---
        if !isRunning {
            // Don't strictly need engine to be running to attempt stop,
            // but might prevent unnecessary calls if state tracking existed.
            // For now, allow stop even if UI thinks engine isn't running.
            logger.warning("Attempting to stop streams while engine state is not 'running'.")
            // No return here - proceed with the stop attempt
        }
        // --- END FIX ---

        Task {
            let success = await engineService.stopStreams(guid: guid)
            logger.info("UIManager: EngineService stopStreams(guid: 0x\(String(format: "%llX", guid))) returned: \(success)")
            // TODO: Update UI based on success/failure if needed
        }
    }
    // --- End Stream Control Actions ---

    // Add other methods for Install Daemon, Set Sample Rate (via SystemServicesManager -> XPC), etc.
    
    // Clean up on deinit
    deinit {
        cancellables.forEach { $0.cancel() } // Cancel Combine subscriptions
        logger.info("UIManager deinitialized.")
    }
}
