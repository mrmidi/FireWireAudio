// RootView.swift

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
        .onChange(of: uiManager.showDriverInstallPrompt, perform: handleDriverPromptChange)
        .alert("FireWire Daemon Not Installed", isPresented: $uiManager.showDaemonInstallPrompt, actions: daemonAlertActions, message: daemonAlertMessage)
        .onChange(of: uiManager.showDaemonInstallPrompt, perform: handleDaemonPromptChange)
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
            StreamsView()
                .tabItem { Label("Streams", systemImage: "waveform.path.ecg") }
                .tag(4)
                .keyboardShortcut("5", modifiers: .command)
        }
    }

    @ToolbarContentBuilder private func mainToolbar() -> some ToolbarContent {
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
                .help("Export original device JSON (first device)")
            }
        }
        ToolbarItemGroup {
            Button { uiManager.startEngine() } label: { Label("Start", systemImage: "play.fill") }
                .help("Start Engine Service")
                .disabled(uiManager.isRunning)
            Button { uiManager.stopEngine() }  label: { Label("Stop", systemImage: "stop.fill") }
                .disabled(!uiManager.isRunning)
        }
        ToolbarItemGroup {
            if selectedTab == 0 || selectedTab == 1 || selectedTab == 4 {
                Button {
                    logger.info("User requested refresh all devices.")
                    uiManager.refreshAllDevices()
                } label: { Label("Refresh Devices", systemImage: "arrow.clockwise") }
                .disabled(!uiManager.isRunning)
                .help("Refresh data for all connected devices")
            }
        }
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
struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        // --- FIX: Robust UIManager preview initialization ---
        let previewUIManager: UIManager = {
            if let engine = EngineService() {
                let permManager = PermissionManager()
                let daemonManager = DaemonServiceManager()
                let systemServices = SystemServicesManager(
                    engineService: engine,
                    permissionManager: permManager,
                    daemonServiceManager: daemonManager
                )
                let logStore = LogStore()
                let uiManager = UIManager(
                    engineService: engine,
                    systemServicesManager: systemServices,
                    logStore: logStore
                )
                // Optionally set preview state:
                // uiManager.isRunning = true
                // uiManager.showDriverInstallPrompt = true
                return uiManager
            } else {
                // Fallback: nil dependencies
                return UIManager(engineService: nil, systemServicesManager: nil, logStore: nil)
            }
        }()
        return ContentView()
            .environmentObject(previewUIManager)
    }
}
