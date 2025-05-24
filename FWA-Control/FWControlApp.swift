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
        // Create engine (now always succeeds)
        let initializedEngineService = EngineService()
        self.engineService = initializedEngineService

        // Initialize other services
        let mainPermManager = PermissionManager()
        let mainDaemonManager = DaemonServiceManager()
        let initializedSystemServices = SystemServicesManager(
            engineService: initializedEngineService,
            permissionManager: mainPermManager,
            daemonServiceManager: mainDaemonManager
        )
        let initializedLogStore = LogStore()

        // Assign to instance properties
        self.permissionManager = mainPermManager
        self.daemonServiceManager = mainDaemonManager
        self.systemServicesManager = initializedSystemServices
        self.logStore = initializedLogStore

        // --- Create local constants BEFORE StateObject init ---
        let localSystemServices = initializedSystemServices
        let localLogStore = initializedLogStore

        // Initialize StateObject using local constants
        _uiManager = StateObject(wrappedValue: UIManager(
            engineService: initializedEngineService,
            systemServicesManager: localSystemServices,
            logStore: localLogStore
        ))
        logger.info("Core services and UIManager initialized successfully.")
    } // End init

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(uiManager) // Pass the single UIManager instance
        }
        Settings {
            SettingsView()
                .environmentObject(uiManager) // Pass the single UIManager instance
        }
    }
}
