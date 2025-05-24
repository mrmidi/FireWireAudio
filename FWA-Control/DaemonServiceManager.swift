// === FWA-Control/DaemonServiceManager.swift ===

import Foundation
@preconcurrency import ServiceManagement // <-- ADD @preconcurrency HERE
import Combine // Needed for @Published and Timer
import Logging
import AppKit // For NSApplication.isActive and Timer RunLoop mode

@MainActor // Ensure UI-related updates and timer setup happen on main actor
class DaemonServiceManager: ObservableObject {

    @Published var status: SMAppService.Status = .notFound
    @Published var requiresUserAction: Bool = false // Simplified state for UI prompts

    // Combine subject to notify DeviceManager when connection *should* be attempted
    let connectionNeededPublisher = PassthroughSubject<Void, Never>()

    private let logger = Logger(label: "net.mrmidi.fwa-control.DaemonServiceManager")
    private let serviceIdentifier = "net.mrmidi.FWADaemon" // Define service name once
    private let plistName = "FWADaemon.plist"           // Define plist name once
    private var xpcIsConnected: Bool = false // Track XPC connection state locally

    // Helper for SMAppService
    private var daemonService: SMAppService {
        SMAppService.daemon(plistName: self.plistName)
    }

    init() {
        logger.info("DaemonServiceManager initialized.")
        // Initial check
        checkStatus(isInitialCheck: true)
    }

    // Public method for external components (like XPCManager) to report connection status
    func updateXpcConnectionStatus(_ isConnected: Bool) {
         if isConnected != self.xpcIsConnected {
             logger.info("XPC connection status reported as: \(isConnected)")
             self.xpcIsConnected = isConnected
             // Re-evaluate status and timer when XPC connection changes
             checkStatus(isInitialCheck: false)
         }
    }

    /// Checks the daemon status, updates published properties, and signals if XPC connection is needed.
    func checkStatus(isInitialCheck: Bool = false) {
        let currentSMStatus = daemonService.status // Check synchronously
        logger.trace("Checking daemon SMAppService status: \(currentSMStatus)")

        // Update internal state only if changed
        if currentSMStatus != self.status {
            logger.info("Daemon SMAppService status changed: \(currentSMStatus) (was \(self.status))")
            self.status = currentSMStatus
        }

        // Update the simplified UI prompt flag based on the *new* status
        let shouldPrompt = (currentSMStatus == .requiresApproval || (currentSMStatus == .notRegistered && !isInitialCheck))
        if shouldPrompt != self.requiresUserAction {
             logger.info("Daemon requiresUserAction state changing to: \(shouldPrompt)")
             self.requiresUserAction = shouldPrompt
        }

        // --- Connection Logic ---
        // If the daemon is enabled but XPC isn't connected yet, signal that a connection attempt is needed.
        if currentSMStatus == .enabled && !self.xpcIsConnected {
            logger.info("Daemon is enabled, XPC not connected. Signaling connection needed...")
            connectionNeededPublisher.send()
        }

        // --- REMOVE Timer Logic ---
        // No calls to start/stopStatusTimer needed
    }

    func registerDaemon() async -> Bool {
        logger.info("Attempting to register daemon service (\(plistName))...")
        do {
            try daemonService.register()
            logger.info("Daemon registration request successful (may require user approval).")
            checkStatus() // Update status immediately after request
            return true
        } catch {
            logger.error("Daemon registration failed: \(error.localizedDescription)")
            if error.localizedDescription.contains("Operation not permitted") {
                 logger.warning("Daemon registration requires user approval in System Settings.")
                 // The status check should update requiresUserAction for the UI prompt
            }
            checkStatus() // Update status even on failure
            return false
        }
    }

    func unregisterDaemon() async -> Bool {
        logger.info("Attempting to unregister daemon service (\(plistName))...")
        do {
             try await daemonService.unregister()
             logger.info("Daemon unregistration request successful.")
             checkStatus() // Update status immediately
             return true
         } catch {
             logger.error("Daemon unregistration failed: \(error.localizedDescription)")
             checkStatus() // Update status
             return false
         }
    }

    deinit {
        logger.info("Deinitializing DaemonServiceManager.")
        // Do NOT call stopStatusTimer() here. Timer will be cleaned up via [weak self] capture.
        NotificationCenter.default.removeObserver(self)
        logger.info("Removed NotificationCenter observers.")
    }
}
