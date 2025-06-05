// === FWA-Control/ContentView.swift (Updated with Diagnostics) ===

import SwiftUI
import UniformTypeIdentifiers

struct ContentView: View {
    @EnvironmentObject var uiManager: UIManager
    @State private var selectedTab: Int = 0
    @State private var showExportJsonSheet = false
    @State private var exportJsonContent: String = ""
    @Environment(\.openSettings) var openSettings
    private let logger = AppLoggers.app

    var body: some View {
        VStack(spacing: 0) {
            mainTabView
            Divider()
            StatusBarView()
        }
        .navigationTitle("FWA Control")
        .toolbar(content: mainToolbar)
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
        .alert("FireWire Driver Missing", isPresented: $uiManager.showDriverInstallPrompt, actions: driverAlertActions, message: driverAlertMessage)
        .onChange(of: uiManager.showDriverInstallPrompt) { isShowing, _ in
            handleDriverPromptChange(isShowing: isShowing)
        }
        .alert("FireWire Daemon Not Installed", isPresented: $uiManager.showDaemonInstallPrompt, actions: daemonAlertActions, message: daemonAlertMessage)
        .onChange(of: uiManager.showDaemonInstallPrompt) { isShowing, _ in
            handleDaemonPromptChange(isShowing: isShowing)
        }
    }

    @ViewBuilder private var mainTabView: some View {
        TabView(selection: $selectedTab) {
            OverviewView()
                .tabItem { Label("Overview", systemImage: "dial.medium") }
                .tag(0)
                .keyboardShortcut("1", modifiers: .command)
            ConnectionMatrixView()
                .tabItem { Label("Matrix", systemImage: "rectangle.grid.3x2") }
                .tag(1)
                .keyboardShortcut("2", modifiers: .command)
            LogConsoleView()
                .tabItem { Label("Logs", systemImage: "doc.plaintext") }
                .tag(2)
                .keyboardShortcut("3", modifiers: .command)
            AVCTabView()
                .tabItem { Label("AV/C", systemImage: "terminal") }
                .tag(3)
                .keyboardShortcut("4", modifiers: .command)
            DiagnosticsView()
                .tabItem { Label("Diagnostics", systemImage: "chart.bar.xaxis") }
                .tag(4)
                .keyboardShortcut("5", modifiers: .command)
        }
    }

    @ToolbarContentBuilder private func mainToolbar() -> some ToolbarContent {
        // Export functionality
        ToolbarItemGroup {
            if uiManager.devices.first?.value != nil {
                Button {
                    if let guid = uiManager.devices.keys.sorted().first,
                       let json = uiManager.deviceJsons[guid] {
                        exportJsonContent = json
                        showExportJsonSheet = true
                        logger.info("User requested export JSON for GUID 0x\(String(format: "%016llX", guid))")
                    } else {
                        logger.warning("Export JSON requested, but no device or JSON found.")
                    }
                } label: {
                    Label("Export JSON", systemImage: "square.and.arrow.up")
                }
                .help("Export device configuration as JSON")
            }
        }
        
        // Refresh functionality - only for views that need it
        ToolbarItemGroup {
            if selectedTab == 0 || selectedTab == 1 {
                Button {
                    logger.info("User requested refresh all devices.")
                    uiManager.refreshAllDevices()
                } label: {
                    Label("Refresh Devices", systemImage: "arrow.clockwise")
                }
                .disabled(!uiManager.isDaemonConnected)
                .help("Refresh data for all connected devices")
            }
        }
        
        // System status indicators
        ToolbarItemGroup {
            // Connection status indicator
            connectionStatusIndicator
            
            // Settings access
            Button {
                openSettings()
            } label: {
                Label("Settings", systemImage: "gearshape")
            }
            .help("Open application settings")
        }
    }
    
    @ViewBuilder
    private var connectionStatusIndicator: some View {
        HStack(spacing: 8) {
            // Daemon status
            HStack(spacing: 4) {
                Image(systemName: uiManager.isDaemonConnected ? "bolt.fill" : "bolt.slash.fill")
                    .foregroundColor(uiManager.isDaemonConnected ? .green : .orange)
                    .font(.caption)
                
                Text("Daemon")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
            }
            .help("Daemon connection status: \(uiManager.isDaemonConnected ? "Connected (Engine auto-started)" : "Disconnected")")
            
            // Driver status
            HStack(spacing: 4) {
                Image(systemName: uiManager.isDriverConnected ? "puzzlepiece.extension.fill" : "puzzlepiece.extension")
                    .foregroundColor(uiManager.isDriverConnected ? .green : .red)
                    .font(.caption)
                
                Text("Driver")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
            }
            .help("Driver status: \(uiManager.isDriverConnected ? "Installed" : "Not installed")")
            
            // Device count
            if !uiManager.devices.isEmpty {
                HStack(spacing: 4) {
                    Image(systemName: "hifispeaker.2.fill")
                        .foregroundColor(.blue)
                        .font(.caption)
                    
                    Text("\(uiManager.devices.count)")
                        .font(.caption2)
                        .fontWeight(.medium)
                }
                .help("\(uiManager.devices.count) device\(uiManager.devices.count == 1 ? "" : "s") connected")
            }
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 4)
        .background(.regularMaterial, in: Capsule())
    }

    @ViewBuilder private func driverAlertActions() -> some View {
        Button("Install Driver") {
            logger.info("User clicked 'Install Driver' button.")
            uiManager.installDriver()
        }
        Button("Cancel", role: .cancel) {
            logger.info("User cancelled driver installation prompt.")
        }
    }

    @ViewBuilder private func driverAlertMessage() -> some View {
        Text("To communicate with your FireWire audio hardware, please install the driver. You will be prompted for admin credentials.")
    }

    @ViewBuilder private func daemonAlertActions() -> some View {
        Button("Go to Settings") {
            logger.info("User clicked 'Go to Settings' for daemon installation.")
            openSettings()
        }
        Button("Cancel", role: .cancel) {
            logger.info("User cancelled daemon installation prompt.")
        }
    }

    @ViewBuilder private func daemonAlertMessage() -> some View {
        Text("The FireWire Daemon is required for device communication. Please go to Settings → System to install the daemon.")
    }

    private func handleDriverPromptChange(isShowing: Bool) {
        if isShowing { logger.info("Presenting driver installation prompt.") }
        else { logger.debug("Driver installation prompt dismissed.") }
    }

    private func handleDaemonPromptChange(isShowing: Bool) {
        if isShowing { logger.info("Presenting daemon installation prompt.") }
        else { logger.debug("Daemon installation prompt dismissed.") }
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