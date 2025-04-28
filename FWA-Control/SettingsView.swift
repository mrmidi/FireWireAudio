// === FWA-Control/SettingsView.swift ===

import SwiftUI
import ServiceManagement
import Logging // For Logger.Level

struct SettingsView: View {
    @EnvironmentObject var manager: DeviceManager

    // Use the @AppStorage variable directly from DeviceManager if possible,
    // or create a local binding that calls manager update method.
    @State private var localLogBufferSize: Int = 500 // Local state
    @AppStorage("settings.autoConnect")    private var autoConnect: Bool = false

    // Daemon status state
    @State private var daemonStatus: SMAppService.Status = .notFound
    
    @State private var driverPath = "/Library/Audio/Plug-Ins/HAL/FWADriver.driver"
    @State private var isDriverInstalled = false

    private let logger = AppLoggers.settings // Logger for this view

    // Helper for SMAppService (can be moved to DeviceManager later)
    private var daemonService: SMAppService {
        SMAppService.daemon(plistName: "FWADaemon.plist") // Use the plist filename
    }
    
    private func updateDaemonStatus() {
        daemonStatus = daemonService.status
        logger.info("Daemon status updated: \(daemonStatus)")
    }
    
    private func registerDaemon() async {
        do {
            try await daemonService.register()
            updateDaemonStatus() // Update status after attempt
            logger.info("Daemon registration requested.") // Add logging
        } catch {
            // Daemon registration failed: Error Domain=SMAppServiceErrorDomain Code=1 "Operation not permitted" UserInfo={NSLocalizedFailureReason=Operation not permitted}
            // DO NOT FAIL ON THIS
            // This is a non-fatal error, just log it and show an alert describing that the user needs to approve the daemon in System Settings > Login Items & Extensions > FWA-Control
            if error.localizedDescription.contains("Operation not permitted") { // TODO: Make it i18n universal
                // This is a non-fatal error, just log it and show an alert
                logger.warning("Daemon registration requires user approval in System Settings.")
                showAlert(title: "Daemon Registration Required", message: "Please approve the daemon in System Settings > Privacy & Security > Login Items & Extensions.", style: .informational)
            } else {
                logger.error("Daemon registration failed: \(error)") // Log error
                showAlert(title: "Daemon Registration Failed", message: error.localizedDescription, style: .critical)
            }
            updateDaemonStatus() // Update status even on failure
        }
    }
    
    private func unregisterDaemon() async {
        do {
             try await daemonService.unregister()
             updateDaemonStatus() // Update status after attempt
             logger.info("Daemon unregistration requested.") // Add logging
         } catch {
             logger.error("Daemon unregistration failed: \(error)") // Log error
             showAlert(title: "Daemon Unregistration Failed", message: error.localizedDescription, style: .critical)
             updateDaemonStatus() // Update status even on failure
         }
    }
    
    private func checkDriverStatus() {
        isDriverInstalled = FileManager.default.fileExists(atPath: driverPath)
    }

    // Helper for showing alerts (macOS only, simple implementation)
    private func showAlert(title: String, message: String, style: NSAlert.Style) {
#if os(macOS)
        let alert = NSAlert()
        alert.messageText = title
        alert.informativeText = message
        alert.alertStyle = style
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
                        Stepper(value: $localLogBufferSize, in: 100...10000, step: 100) {
                            Text("\(localLogBufferSize)")
                                .frame(width: 60, alignment: .trailing)
                        }
                        .onChange(of: localLogBufferSize) { newValue in
                            manager.updateLogBufferSize(newValue)
                        }
                        .onAppear {
                            localLogBufferSize = manager.logDisplayBufferSize
                        }
                    }
                    Text("Maximum number of log entries to keep in the UI Log view.")
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
                        // Status indicator
                        Image(systemName: daemonStatus == .enabled ? "circle.fill" : (daemonStatus == .requiresApproval ? "exclamationmark.triangle.fill" : "circle"))
                            .foregroundColor(daemonStatus == .enabled ? .green : (daemonStatus == .requiresApproval ? .yellow : .red))
                            .imageScale(.large)
                        Text(daemonStatus == .enabled ? "Installed" : (daemonStatus == .requiresApproval ? "Requires Approval" : "Not Installed"))
                            .foregroundColor(daemonStatus == .enabled ? .green : (daemonStatus == .requiresApproval ? .yellow : .red))
                        Spacer()
                        if (daemonStatus == .enabled) {
                            Button("Uninstall Daemon") {
                                Task { await unregisterDaemon() }
                            }
                            .buttonStyle(.borderedProminent)
                        } else {
                            Button("Install Daemon") {
                                Task { await registerDaemon() }
                            }
                            .buttonStyle(.borderedProminent)
                            .disabled(daemonStatus == .requiresApproval)
                        }
                    }
                    .padding(.vertical, 4)
                    if daemonStatus == .requiresApproval {
                        Text("Daemon requires user approval in System Settings > Privacy & Security.")
                            .font(.caption)
                            .foregroundColor(.yellow)
                            .padding(.leading, 2)
                    }
                }
                // --- Audio Driver Section ---
                GroupBox(label: Label("Audio Driver", systemImage: "puzzlepiece.extension").font(.headline)) {
                    HStack(spacing: 12) {
                        Image(systemName: isDriverInstalled ? "checkmark.seal.fill" : "xmark.seal")
                            .foregroundColor(isDriverInstalled ? .green : .red)
                            .imageScale(.large)
                        Text(isDriverInstalled ? "Installed" : "Not Installed")
                            .foregroundColor(isDriverInstalled ? .green : .red)
                        Spacer()
                        Button(isDriverInstalled ? "Reinstall Driver" : "Install Driver") {
                            Task {
                                await manager.installDriverFromBundle()
                                checkDriverStatus()
                            }
                        }
                        .buttonStyle(.borderedProminent)
                    }
                    .padding(.vertical, 4)
                    Text("The FireWire Audio Driver is required for device communication. You may be prompted for admin credentials.")
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .padding(.leading, 2)
                }
                Spacer()
            }
            .padding()
            .onAppear {
                updateDaemonStatus()
                checkDriverStatus()
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
