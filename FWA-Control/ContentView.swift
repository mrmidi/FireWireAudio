// RootView.swift

import SwiftUI
import UniformTypeIdentifiers


struct ContentView: View {
    // The single source of truth for engine state and device data
    @StateObject private var manager = DeviceManager()
    @Environment(\.openSettings) var openSettings // <-- Add this
    private let logger = AppLoggers.app // Logger for ContentView actions

    // State for the selected tab
    @State private var selectedTab: Int = 0 // 0: Overview, 1: Matrix, 2: Logs, 3: AV/C

    // State for file export
    @State private var showExportJsonSheet = false
    @State private var exportJsonContent: String = ""

    var body: some View {
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
        .environmentObject(manager) // Make manager available to all tabs
        .navigationTitle("FWA Control") // Keep the window title
        .toolbar {
            // --- Export JSON Button ---
            ToolbarItemGroup {
                if let guid = manager.devices.keys.sorted().first, // Use first device for now
                   let json = manager.deviceJsons[guid], !json.isEmpty {
                    Button {
                        exportJsonContent = json
                        showExportJsonSheet = true
                    } label: {
                        Label("Export JSON", systemImage: "square.and.arrow.up")
                    }
                    .help("Export original device JSON")
                }
            }
            // Always‐visible engine controls (no explicit placement)
            ToolbarItemGroup {
                Button { manager.start() } label: { Label("Start", systemImage: "play.fill") }
                    .help("Start CoreAudio Driver Discovery (requires driver)")
                    .disabled(manager.isRunning)
                // Optional: Disable start if driver isn't connected?
                // .disabled(manager.isRunning || !manager.isDriverConnected)

                // Display Driver connection status next to engine controls
                DeviceStatusIndicatorView(label: "Driver", isConnected: manager.isDriverConnected)

                Button { manager.stop() }  label: { Label("Stop", systemImage: "stop.fill") }
                    .disabled(!manager.isRunning)
            }
            // Contextual per‐tab actions
            ToolbarItemGroup {
                if selectedTab == 1 {
                    Button { manager.refreshAllDevices() } label: { Label("Refresh", systemImage: "arrow.clockwise") }
                }
                if selectedTab == 2 {
                    Button { showExportLogs() } label: { Label("Export Logs", systemImage: "square.and.arrow.up") }
                }
            }
        }
        .fileExporter(
            isPresented: $showExportJsonSheet,
            document: JsonDocument(content: exportJsonContent),
            contentType: .json,
            defaultFilename: "fwa-device.json"
        ) { result in
            // Optionally handle result
        }
        // --- Driver Install Alert ---
        .alert(
            "FireWire Driver Missing",
            isPresented: $manager.showDriverInstallPrompt // Bind to the manager's state
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
        // --- Daemon Install Alert ---
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
