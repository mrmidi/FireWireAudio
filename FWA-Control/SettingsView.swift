// === FWA-Control/SettingsView.swift ===

import SwiftUI
import ServiceManagement
import Logging // For Logger.Level

fileprivate struct StepperBinding: View {
    @ObservedObject var logStore: LogStore
    var body: some View {
        Stepper(value: $logStore.logDisplayBufferSize, in: 100...10000, step: 100) {
            Text("\(logStore.logDisplayBufferSize)")
                .frame(width: 60, alignment: .trailing)
        }
    }
}

struct SettingsView: View {
    @EnvironmentObject var uiManager: UIManager

    @AppStorage("settings.autoConnect") private var autoConnect: Bool = false
    private let driverPath = "/Library/Audio/Plug-Ins/HAL/FWADriver.driver" // Keep for path info

    private static let logger = AppLoggers.settings

    // --- FIX: Call UIManager methods ---
    private func registerDaemon() async {
        SettingsView.logger.info("Attempting to register daemon via UIManager…")
        _ = await uiManager.registerDaemon() // Call the UIManager wrapper method
    }

    private func unregisterDaemon() async {
        SettingsView.logger.info("Attempting to unregister daemon via UIManager…")
        _ = await uiManager.unregisterDaemon() // Call the UIManager wrapper method
    }
    // --- End Fix ---

    @MainActor
    private func installDriver() async {
        SettingsView.logger.info("User initiated driver installation from Settings.")
        uiManager.installDriver()
    }

    @MainActor
    private func showAlert(title: String, message: String, style: NSAlert.Style) {
        #if os(macOS)
        let alert = NSAlert()
        alert.messageText = title
        alert.informativeText = message
        alert.alertStyle = style
        alert.addButton(withTitle: "OK") // Add button
        alert.runModal()
        #endif
    }

    var body: some View {
        TabView {
            // --- Basic Settings Tab ---
            Form {
                 Section(header: Label("Logging", systemImage: "doc.plaintext").font(.headline)) {
                    HStack {
                        Text("Log Display Buffer Size")
                        Spacer()
                        if let logStore = uiManager.logStore {
                             StepperBinding(logStore: logStore)
                        } else {
                             Text("Log store unavailable").foregroundColor(.secondary)
                        }
                    }
                    Text("Maximum number of log entries to keep in the UI Log view.")
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .padding(.leading, 2)
                }
                Section(header: Label("Behavior", systemImage: "gearshape").font(.headline)) {
                    Toggle(isOn: $autoConnect) { Text("Auto-connect on startup") }
                    Text("Automatically start the engine when the application launches.")
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .padding(.leading, 2)
                }
            }
            .formStyle(.grouped)
            .tabItem { Label("Basic", systemImage: "gearshape") }

            // --- System Tab ---
            VStack(alignment: .leading, spacing: 24) {
                 GroupBox(label: Label("Daemon", systemImage: "bolt.horizontal.circle").font(.headline)) {
                     HStack(spacing: 12) {
                         Image(systemName: uiManager.daemonInstallStatus == .enabled ? "checkmark.circle.fill" : (uiManager.daemonInstallStatus == .requiresApproval ? "exclamationmark.triangle.fill" : "xmark.circle.fill"))
                             .foregroundColor(uiManager.daemonInstallStatus == .enabled ? .green : (uiManager.daemonInstallStatus == .requiresApproval ? .yellow : .red))
                             .imageScale(.large)
                         Text(uiManager.daemonInstallStatus.description)
                             .foregroundColor(uiManager.daemonInstallStatus == .enabled ? .green : (uiManager.daemonInstallStatus == .requiresApproval ? .yellow : .red))
                         Spacer()
                         if (uiManager.daemonInstallStatus == .enabled) {
                             Button("Uninstall Daemon") { Task { await unregisterDaemon() } }
                                 .buttonStyle(.bordered)
                         } else {
                             Button("Install Daemon") { Task { await registerDaemon() } }
                                 .buttonStyle(.borderedProminent)
                                 .disabled(uiManager.daemonInstallStatus == .requiresApproval)
                         }
                     }
                     .padding(.vertical, 4)
                     if uiManager.daemonInstallStatus == .requiresApproval {
                         Text("Daemon requires user approval in System Settings > Privacy & Security.")
                             .font(.caption)
                             .foregroundColor(.yellow)
                             .padding(.leading, 2)
                     }
                 }
                 GroupBox(label: Label("Audio Driver", systemImage: "puzzlepiece.extension").font(.headline)) {
                     HStack(spacing: 12) {
                         Image(systemName: uiManager.isDriverConnected ? "checkmark.seal.fill" : "xmark.seal")
                             .foregroundColor(uiManager.isDriverConnected ? .green : .red)
                             .imageScale(.large)
                         Text(uiManager.isDriverConnected ? "Connected" : "Disconnected / Not Installed")
                             .foregroundColor(uiManager.isDriverConnected ? .green : .red)
                         Spacer()
                         Button(uiManager.isDriverConnected ? "Reinstall Driver" : "Install Driver") {
                             Task { await installDriver() }
                         }
                         .buttonStyle(.borderedProminent)
                     }
                     .padding(.vertical, 4)
                     Text("The FireWire Audio Driver allows communication with devices. Installation requires admin privileges.")
                         .font(.caption)
                         .foregroundColor(.secondary)
                         .padding(.leading, 2)
                 }
                 Spacer()
            }
            .padding()
            .tabItem { Label("System", systemImage: "desktopcomputer") }
        }
        .padding()
        .frame(minWidth: 420, minHeight: 340)
    }
}

// --- FIX: Add CustomStringConvertible extension ---
extension SMAppService.Status: @retroactive CustomStringConvertible {
    public var description: String {
        switch self {
        case .enabled: return "Enabled"
        case .requiresApproval: return "Requires Approval"
        case .notFound: return "Not Found"
        case .notRegistered: return "Not Registered"
        @unknown default: return "Unknown"
        }
    }
}
// --- End extension ---

// MARK: - Preview
// struct SettingsView_Previews: PreviewProvider {
//     @MainActor
//     static func createPreviewServices() -> (engine: EngineService?, system: SystemServicesManager?, log: LogStore?) {
//         guard let engine = EngineService() else {
//             print("PREVIEW ERROR: EngineService() failed in SettingsView preview.")
//             return (nil, nil, nil)
//         }
//         let permManager = PermissionManager()
//         let daemonManager = DaemonServiceManager()
//         let systemServices = SystemServicesManager(
//             engineService: engine,
//             permissionManager: permManager,
//             daemonServiceManager: daemonManager
//         )
//         let logStore = LogStore()
//         return (engine, systemServices, logStore)
//     }

//     @MainActor
//     static func createPreviewUIManager(connected: Bool, needsApproval: Bool = false) -> UIManager {
//         let (engine, system, log) = createPreviewServices()
//         let uiManager = UIManager(engineService: engine, systemServicesManager: system, logStore: log)
//         uiManager.isDaemonConnected = connected
//         uiManager.isDriverConnected = connected
//         uiManager.daemonInstallStatus = needsApproval ? .requiresApproval : (connected ? .enabled : .notRegistered)
//         uiManager.showDaemonInstallPrompt = needsApproval
//         uiManager.showDriverInstallPrompt = !connected
//         log?.logDisplayBufferSize = 333

//         return uiManager
//     }

//     static var previews: some View {
//         SettingsView()
//             .environmentObject(createPreviewUIManager(connected: true))
//             .previewDisplayName("Connected State")

//         SettingsView()
//             .environmentObject(createPreviewUIManager(connected: false))
//             .previewDisplayName("Disconnected State")

//         SettingsView()
//              .environmentObject(createPreviewUIManager(connected: false, needsApproval: true))
//              .previewDisplayName("Needs Approval State")
//     }
// }
