//
//  FWControlApp.swift
//  FWControl
//
//  Created by Alexander Shabelnikov on 15.04.2025.
//

import SwiftUI
//import FWA

@main
struct FWControlApp: App {
//    var body: some Scene {
//        WindowGroup {
//            // Ensure this instantiates RootView
//            ContentView()
//        }
//        .commands(
//    }
    @StateObject private var manager = DeviceManager()
    
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
