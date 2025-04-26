// RootView.swift

import SwiftUI


struct ContentView: View {
    // The single source of truth for engine state and device data
    @StateObject private var manager = DeviceManager()

    // State for the selected tab
    @State private var selectedTab: Int = 0 // 0: Overview, 1: Matrix, 2: Logs, 3: AV/C

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
            // Always‐visible engine controls (no explicit placement)
            ToolbarItemGroup {
                Button { manager.start() } label: { Label("Start", systemImage: "play.fill") }
                    .disabled(manager.isRunning)
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
    }

    private func showExportLogs() {
        // This function can be implemented to trigger log export in LogConsoleView
    }
}

// MARK: - Preview
struct RootView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
            .environmentObject(DeviceManager())
    }
}
