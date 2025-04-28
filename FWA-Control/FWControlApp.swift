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
    @StateObject private var manager = DeviceManager()

    // Bootstrap logging system ONCE on app initialization
    init() {
        // Simplified bootstrap: only use InMemoryLogHandler
        LoggingSystem.bootstrap { label in
            var inMemoryHandler = InMemoryLogHandler(label: label)
            inMemoryHandler.logLevel = .trace // Log everything to memory initially
            return inMemoryHandler
        }

        // Optional: Log that bootstrapping is done
        let logger = AppLoggers.app // Use the centralized logger
        logger.info("Logging system bootstrapped (InMemoryLogHandler only).")
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(manager)
        }
        // macOS “Preferences…” (⌘,) under the App menu
         Settings {
            SettingsView()
                .environmentObject(manager)
                .frame(width: 400, height: 300)
        }
    }
}
