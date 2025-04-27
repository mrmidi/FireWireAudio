// === FWA-Control/SettingsView.swift ===

import SwiftUI
import ServiceManagement

struct SettingsView: View {
    @EnvironmentObject var manager: DeviceManager

    // AppStorage-backed settings
    @AppStorage("settings.logBufferSize") private var logBufferSize: Int = 500
    @AppStorage("settings.autoConnect")    private var autoConnect: Bool = false

    // SMAppService for FWADaemon
    @State private var daemonService: SMAppService?
    @State private var daemonInstalled = false

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
                        Image(systemName: daemonInstalled ? "circle.fill" : "circle")
                            .foregroundColor(daemonInstalled ? .green : .red)
                            .imageScale(.large)
                        Text(daemonInstalled ? "Installed" : "Not Installed")
                            .foregroundColor(daemonInstalled ? .green : .red)
                        Spacer()
                        Button(daemonInstalled ? "Reinstall Daemon" : "Install Daemon") {
                            guard let svc = daemonService else { return }
                            do {
                                if daemonInstalled {
                                    try svc.unregister()
                                    daemonInstalled = false
                                } else {
                                    try svc.register()
                                    daemonInstalled = true
                                }
                            } catch {
                                print("⚠️ Failed to toggle daemon:", error)
                            }
                        }
                        .buttonStyle(.borderedProminent)
                        .disabled(daemonService == nil)
                    }
                    .padding(.vertical, 4)
                }
                // ... Driver GroupBox can remain placeholder or use SMJobBless for kext ...
                Spacer()
            }
            .padding()
            .onAppear {
                // Initialize the SMAppService and installed state
                do {
                    let svc = try SMAppService.loginItem(identifier: "net.mrmidi.FWADaemon")
                    daemonService = svc
                    daemonInstalled = (svc.status == .enabled)
                } catch {
                    print("⚠️ Error retrieving daemon service:", error)
                }
            }
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
        SettingsView()
            .environmentObject(DeviceManager())
            .frame(width: 400, height: 300)
    }
}
