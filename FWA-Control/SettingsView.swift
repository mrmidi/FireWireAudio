// === FWA-Control/SettingsView.swift ===

import SwiftUI

struct SettingsView: View {
    @EnvironmentObject var manager: DeviceManager
    @AppStorage("settings.logBufferSize") private var logBufferSize: Int = 500
    @AppStorage("settings.autoConnect") private var autoConnect: Bool = false
    @State private var daemonInstalled = false // Placeholder
    @State private var driverInstalled = false // Placeholder
    
    var body: some View {
        TabView {
            // --- Basic Settings Tab ---
            Form {
                Section(header: Label("Logging", systemImage: "doc.plaintext").font(.headline)) {
                    HStack {
                        Text("Log Buffer Size")
                        Spacer()
                        Stepper(value: $logBufferSize, in: 100...5000, step: 100) {
                            Text("\(logBufferSize)")
                                .frame(width: 60, alignment: .trailing)
                        }
                        .onChange(of: logBufferSize) { newValue in
                            manager.logBufferSize = newValue
                        }
                        .onAppear {
                            manager.logBufferSize = logBufferSize
                        }
                    }
                    Text("Maximum number of log entries to keep in memory.")
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .padding(.leading, 2)
                }
                Section(header: Label("Behavior", systemImage: "gearshape").font(.headline)) {
                    Toggle(isOn: $autoConnect) {
                        Text("Auto-connect on startup")
                    }
                    Text("Automatically start the engine when the application launches.")
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .padding(.leading, 2)
                }
            }
            .formStyle(.grouped)
            .tabItem {
                Label("Basic", systemImage: "gearshape")
            }
            // --- System Tab ---
            VStack(alignment: .leading, spacing: 24) {
                GroupBox(label: Label("Daemon", systemImage: "bolt.horizontal.circle").font(.headline)) {
                    HStack(spacing: 12) {
                        Image(systemName: "circle.fill")
                            .foregroundColor(.red)
                            .imageScale(.large)
                        Text("Not Installed")
                            .foregroundColor(.red)
                        Spacer()
                        Button(daemonInstalled ? "Reinstall Daemon" : "Install Daemon") {
                            // Placeholder action
                        }
                        .buttonStyle(.borderedProminent)
                        .disabled(true) // Placeholder
                    }
                    .padding(.vertical, 4)
                }
                GroupBox(label: Label("Driver", systemImage: "shippingbox").font(.headline)) {
                    HStack(spacing: 12) {
                        Image(systemName: "circle.fill")
                            .foregroundColor(.red)
                            .imageScale(.large)
                        Text("Not Installed")
                            .foregroundColor(.red)
                        Spacer()
                        Button(driverInstalled ? "Reinstall Driver" : "Install Driver") {
                            // Placeholder action
                        }
                        .buttonStyle(.borderedProminent)
                        .disabled(true) // Placeholder
                    }
                    .padding(.vertical, 4)
                }
                Spacer()
            }
            .padding()
            .tabItem {
                Label("System", systemImage: "desktopcomputer")
            }
        }
        .padding()
        .frame(minWidth: 420, minHeight: 340)
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
