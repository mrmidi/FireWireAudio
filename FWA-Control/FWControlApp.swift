//
//  FWControlApp.swift
//  FWControl
//
//  Created by Alexander Shabelnikov on 15.04.2025.
//

import SwiftUI
import Logging // Import Logging module

@main
struct FWControlApp: App {
    // Keep properties as let
    private let engineService: EngineService?
    private let permissionManager: PermissionManager?
    private let daemonServiceManager: DaemonServiceManager?
    private let systemServicesManager: SystemServicesManager?
    private let logStore: LogStore?

    // UIManager is the StateObject
    @StateObject private var uiManager: UIManager

    init() {
        // --- Logging Bootstrap ---
        LoggingSystem.bootstrap { label in
            var inMemoryHandler = InMemoryLogHandler(label: label)
            inMemoryHandler.logLevel = .trace
            return inMemoryHandler
        }
        let logger = AppLoggers.app
        logger.info("Logging system bootstrapped.")

        // --- Service Initialization ---
        // Attempt to create engine first
        let initializedEngineService = EngineService()
        self.engineService = initializedEngineService

        // Initialize other services, handling potential engine failure
        if let validEngine = initializedEngineService {
            // Engine succeeded, initialize dependent services
            let mainPermManager = PermissionManager()
            let mainDaemonManager = DaemonServiceManager()
            let initializedSystemServices = SystemServicesManager(
                engineService: validEngine, // Pass the non-optional engine
                permissionManager: mainPermManager,
                daemonServiceManager: mainDaemonManager
            )
            let initializedLogStore = LogStore()

            // Assign to instance properties
            self.permissionManager = mainPermManager
            self.daemonServiceManager = mainDaemonManager
            self.systemServicesManager = initializedSystemServices
            self.logStore = initializedLogStore

            // --- FIX: Create local constants BEFORE StateObject init ---
            let localSystemServices = initializedSystemServices
            let localLogStore = initializedLogStore
            // --- End Fix ---

            // Initialize StateObject using local constants (and the valid engine)
            _uiManager = StateObject(wrappedValue: UIManager(
                engineService: validEngine, // Pass the non-optional engine
                systemServicesManager: localSystemServices,
                logStore: localLogStore
            ))
             logger.info("Core services and UIManager initialized successfully.")

        } else {
            // Engine failed, assign nil to services and initialize UIManager with nils
            self.permissionManager = nil // Or initialize if they don't depend on engine
            self.daemonServiceManager = nil // Or initialize if they don't depend on engine
            self.systemServicesManager = nil
            self.logStore = nil

            // Initialize StateObject with nil dependencies
            _uiManager = StateObject(wrappedValue: UIManager(
                engineService: nil,
                systemServicesManager: nil,
                logStore: nil
            ))
            logger.critical("EngineService failed to initialize. Application cannot function.")
        }
    } // End init

    var body: some Scene {
        WindowGroup {
            // Check engineService directly or check a status on uiManager if preferred
            if engineService != nil {
                ContentView()
                    .environmentObject(uiManager) // Pass the single UIManager instance
                    // No need to pass logStore separately if ContentView gets it via uiManager
                    // .environmentObject(logStore!)
            } else {
                Text("Critical Error: Failed to initialize audio engine.")
                    .padding()
            }
        }
        Settings {
            // Check if UIManager was successfully initialized before showing Settings content
            // This assumes SettingsView can handle a potentially degraded UIManager state
            SettingsView()
                .environmentObject(uiManager) // Pass the single UIManager instance
        }
    }
}
