// === FWA-Control/DaemonServiceManager.swift ===

import Foundation
import ServiceManagement
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
    private var statusCheckTimer: Timer?
    private var wasInBackground: Bool = false // Track app background state
    private var xpcIsConnected: Bool = false // Track XPC connection state locally

    // Helper for SMAppService
    private var daemonService: SMAppService {
        SMAppService.daemon(plistName: self.plistName)
    }

    init() {
        logger.info("DaemonServiceManager initialized.")
        setupAppLifecycleObserver()
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

    /// Checks the daemon status and updates published properties.
    /// Also determines if the connection timer needs to run.
    func checkStatus(isInitialCheck: Bool = false) {
        let currentSMStatus = daemonService.status
        logger.trace("Checking daemon SMAppService status: \(currentSMStatus)")

        // Update internal state only if changed or it's the initial check
        if currentSMStatus != self.status || isInitialCheck {
            logger.info("Daemon SMAppService status changed: \(currentSMStatus) (was \(self.status))")
            self.status = currentSMStatus
            // Update the simplified UI prompt flag
            let shouldPrompt = (currentSMStatus == .requiresApproval || (currentSMStatus == .notRegistered && !isInitialCheck)) // Prompt if needs approval, or if not registered *after* initial launch
            if shouldPrompt != self.requiresUserAction {
                 logger.info("Daemon requiresUserAction state changing to: \(shouldPrompt)")
                 self.requiresUserAction = shouldPrompt
            }
        }

        // --- Connection / Timer Logic ---
        if currentSMStatus == .enabled {
            if !self.xpcIsConnected {
                logger.info("Daemon is enabled, XPC not connected. Signaling connection needed...")
                // Signal DeviceManager via publisher that it should attempt connection
                connectionNeededPublisher.send()
            }
            // If enabled (and XPC is connected or connection attempt will be signaled), stop polling.
            stopStatusTimer()
        } else {
            // If not enabled, make sure the timer is running to check again later.
            startStatusTimer()
        }
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


    private func startStatusTimer() {
        guard statusCheckTimer == nil else { return } // Already running
        // Only start timer if daemon isn't enabled and XPC isn't connected
        guard status != .enabled || !xpcIsConnected else {
             logger.debug("Timer start requested but status is '\(status)' and XPC is \(xpcIsConnected ? "connected" : "disconnected"). Timer not needed.")
             return
        }

        logger.info("Starting daemon status check timer (interval: 5s).")
        statusCheckTimer = Timer.scheduledTimer(withTimeInterval: 5.0, repeats: true) { [weak self] _ in
            // Timer runs on main thread, ensure self is valid and perform check
            guard let self = self else { return }
            // Only check if the app is active to avoid unnecessary work
            if NSApplication.shared.isActive {
                self.checkStatus()
            } else if !self.wasInBackground { // Log inactivity only once per background transition
                self.logger.trace("App inactive, skipping periodic daemon status check.")
            }
        }
        // Add to common modes to run during UI tracking
        RunLoop.current.add(statusCheckTimer!, forMode: RunLoopMode.commonModes)
    }

    private func stopStatusTimer() {
        statusCheckTimer?.invalidate()
        statusCheckTimer = nil
    }

    private func setupAppLifecycleObserver() {
        NotificationCenter.default.addObserver(forName: NSApplication.didBecomeActiveNotification, object: nil, queue: .main) { [weak self] _ in
            guard let self = self else { return }
            self.logger.debug("App became active.")
            self.wasInBackground = false
            // Trigger an immediate check upon activation if timer *would* be running or status unknown
            if self.statusCheckTimer != nil || self.status != .enabled {
                self.logger.info("App activated, performing immediate daemon status check.")
                self.checkStatus()
            }
        }
        NotificationCenter.default.addObserver(forName: NSApplication.willResignActiveNotification, object: nil, queue: .main) { [weak self] _ in
            self?.logger.debug("App resigning active.")
            self?.wasInBackground = true
        }
    }

    deinit {
        logger.info("Deinitializing DaemonServiceManager.")
        // Do NOT call stopStatusTimer() here. Timer will be cleaned up via [weak self] capture.
        NotificationCenter.default.removeObserver(self)
        logger.info("Removed NotificationCenter observers.")
    }
}
