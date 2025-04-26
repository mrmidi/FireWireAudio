// === FWA-Control/SettingsView.swift ===

import SwiftUI

struct SettingsView: View {
    @EnvironmentObject var manager: DeviceManager // Access manager if needed, e.g., for buffer size

    // MARK: - AppStorage Properties
    @AppStorage("settings.logBufferSize") private var logBufferSize: Int = 500 // Default value
    @AppStorage("settings.autoConnect") private var autoConnect: Bool = false
    // Add other settings here as needed

    // MARK: - Body
    var body: some View {
        Form { // Use Form for standard settings layout
            // --- Logging Section ---
            Section("Logging") {
                // Log Buffer Size
                Stepper("Log Buffer Size: \(logBufferSize) entries", value: $logBufferSize, in: 100...5000, step: 100)
                    .onChange(of: logBufferSize) { newValue in
                        manager.logBufferSize = newValue
                        print("Log buffer size set to: \(newValue)")
                    }
                    .onAppear {
                        manager.logBufferSize = logBufferSize
                    }
                Text("Maximum number of log entries to keep in memory.")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }

            // --- Behavior Section ---
            Section("Behavior") {
                Toggle("Auto-connect on startup", isOn: $autoConnect)
                    .onChange(of: autoConnect) { newValue in
                        print("Auto-connect setting changed to: \(newValue)")
                        // Note: DeviceManager needs logic to check this setting on launch
                    }
                Text("Automatically start the engine when the application launches.")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }

            // --- Appearance Section (Optional) ---
            /*
            Section("Appearance") {
                // Theme picker, font size, etc.
            }
            */

            Spacer() // Push sections to top
        }
        .padding() // Add padding around the Form
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top) // Align form to top
    }
}

// MARK: - Preview
struct SettingsView_Previews: PreviewProvider {
    static var previews: some View {
        // Provide a dummy manager for the preview environment
        SettingsView()
            .environmentObject(DeviceManager())
            .frame(width: 400, height: 300)
            .onAppear {
                // Uncomment to reset prefs for preview
                // UserDefaults.standard.removeObject(forKey: "settings.logBufferSize")
                // UserDefaults.standard.removeObject(forKey: "settings.autoConnect")
            }
    }
}
