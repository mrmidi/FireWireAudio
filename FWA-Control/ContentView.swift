// RootView.swift

import SwiftUI
import UniformTypeIdentifiers


struct ContentView: View {
    @EnvironmentObject var manager: DeviceManager // Now always from environment
    @State private var selectedTab: Int = 0
    @State private var showExportJsonSheet = false
    @State private var exportJsonContent: String = ""
    @Environment(\.openSettings) var openSettings
    private let logger = AppLoggers.app

    var body: some View {
        VStack(spacing: 0) { // Use spacing 0 to have status bar directly below TabView
            TabView(selection: $selectedTab) {
                // --- Overview Tab ---
                OverviewView()
                    .tabItem { Label("Overview", systemImage: "dial.medium") }
                    .tag(0)
                    .keyboardShortcut("1", modifiers: .command)

                // --- Connection Matrix Tab ---
                ConnectionMatrixView()
                    .tabItem { Label("Matrix", systemImage: "rectangle.grid.3x2") }
                    .tag(1)
                    .keyboardShortcut("2", modifiers: .command)

                // --- Log Console Tab ---
                LogConsoleView()
                    .tabItem { Label("Logs", systemImage: "doc.plaintext") }
                    .tag(2)
                    .keyboardShortcut("3", modifiers: .command)

                // --- AV/C Tab ---
                AVCTabView()
                    .tabItem { Label("AV/C", systemImage: "terminal") }
                    .tag(3)
                    .keyboardShortcut("4", modifiers: .command)
            }
            Divider() // Separator line
            StatusBarView() // <-- Add the status bar here
        }
        .environmentObject(manager)
        .navigationTitle("FWA Control")
        .toolbar {
            ToolbarItemGroup {
                if manager.devices.first?.value != nil {
                    Button {
                        if let guid = manager.devices.keys.sorted().first,
                           let json = manager.deviceJsons[guid] {
                            exportJsonContent = json
                            showExportJsonSheet = true
                            logger.info("User requested export JSON for GUID 0x\(String(format: "%llX", guid))")
                        } else {
                            logger.warning("Export JSON requested, but no device or JSON found.")
                        }
                    } label: {
                        Label("Export JSON", systemImage: "square.and.arrow.up")
                    }
                    .help("Export original device JSON (first device)")
                }
            }
            ToolbarItemGroup {
                Button { manager.start() } label: { Label("Start", systemImage: "play.fill") }
                    .help("Start CoreAudio Driver Discovery (requires driver)")
                    .disabled(manager.isRunning)
                Button { manager.stop() }  label: { Label("Stop", systemImage: "stop.fill") }
                    .disabled(!manager.isRunning)
            }
            ToolbarItemGroup {
                if selectedTab == 0 || selectedTab == 1 {
                    Button {
                        logger.info("User requested refresh all devices.")
                        manager.refreshAllDevices()
                    } label: { Label("Refresh Devices", systemImage: "arrow.clockwise") }
                    .disabled(!manager.isRunning)
                    .help("Refresh data for all connected devices")
                }
            }
        }
        .fileExporter(
            isPresented: $showExportJsonSheet,
            document: JsonDocument(content: exportJsonContent),
            contentType: .json,
            defaultFilename: "fwa-device.json"
        ) { result in
            switch result {
            case .success(let url):
                logger.info("JSON exported successfully to: \(url.path)")
            case .failure(let error):
                logger.error("JSON export failed: \(error.localizedDescription)")
            }
        }
        // --- Alerts ---
        .alert(
            "FireWire Driver Missing",
            isPresented: $manager.showDriverInstallPrompt
        ) {
            Button("Install Driver") {
                logger.info("User clicked 'Install Driver' button.")
                Task { await manager.installDriverFromBundle() }
            }
            Button("Cancel", role: .cancel) {
                logger.info("User cancelled driver installation prompt.")
            }
        } message: {
            Text("To communicate with your FireWire audio hardware, please install the driver. You will be prompted for admin credentials.")
        }
        .onChange(of: manager.showDriverInstallPrompt) { isShowing in // <-- Log when prompt appears/disappears
            if isShowing {
                logger.info("Presenting driver installation prompt.")
            } else {
                logger.debug("Driver installation prompt dismissed.")
            }
        }
        .alert(
            "FireWire Daemon Not Installed",
            isPresented: $manager.showDaemonInstallPrompt
        ) {
            Button("Go to Settings") {
                logger.info("User clicked 'Go to Settings' for daemon installation.")
                openSettings() // <-- Use the environment action
            }
            Button("Cancel", role: .cancel) {
                logger.info("User cancelled daemon installation prompt.")
            }
        } message: {
            Text("The FireWire Daemon is required for device communication. Please go to Settings → System to install the daemon.")
        }
        .onChange(of: manager.showDaemonInstallPrompt) { isShowing in // <-- Log when prompt appears/disappears
            if isShowing {
                logger.info("Presenting daemon installation prompt.")
            } else {
                logger.debug("Daemon installation prompt dismissed.")
            }
        }
    }

    private func showExportLogs() {
        // This function can be implemented to trigger log export in LogConsoleView
    }
}

// Helper for JSON file export
struct JsonDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.json] }
    var content: String
    init(content: String) { self.content = content }
    init(configuration: ReadConfiguration) throws {
        guard let data = configuration.file.regularFileContents,
              let string = String(data: data, encoding: .utf8)
        else { throw CocoaError(.fileReadCorruptFile) }
        content = string
    }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        return FileWrapper(regularFileWithContents: content.data(using: .utf8)!)
    }
}

// MARK: - Preview
struct RootView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
            .environmentObject(DeviceManager())
    }
}
