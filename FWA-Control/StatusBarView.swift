import SwiftUI

struct StatusBarView: View {
    @EnvironmentObject var uiManager: UIManager

    var body: some View {
        HStack(spacing: 12) {
            Spacer()
            DeviceStatusIndicatorView(
                statusTypeLabel: "Daemon",
                systemImageName: uiManager.isDaemonConnected ? "bolt.fill" : "bolt.slash.fill",
                isConnected: uiManager.isDaemonConnected,
                helpText: uiManager.isDaemonConnected ? "Daemon connection active" : "Daemon connection inactive"
            )
            DeviceStatusIndicatorView(
                statusTypeLabel: "Driver",
                systemImageName: uiManager.isDriverConnected ? "puzzlepiece.extension.fill" : "puzzlepiece.extension",
                isConnected: uiManager.isDriverConnected,
                helpText: uiManager.isDriverConnected ? "Driver connection active" : "Driver connection inactive or driver not installed"
            )
        }
        .padding(.horizontal)
        .padding(.vertical, 5)
        .background(.thinMaterial)
    }
}

struct StatusBarView_Previews: PreviewProvider {

    @MainActor
    static func createPreviewUIManager() -> UIManager {
        // 1. Create EngineService (no longer failable)
        let engine = EngineService()
        // 2. Create @MainActor dependencies
        let permManager = PermissionManager()
        let daemonManager = DaemonServiceManager()
        // 3. Create SystemServicesManager with all dependencies
        let systemServices = SystemServicesManager(
            engineService: engine,
            permissionManager: permManager,
            daemonServiceManager: daemonManager
        )
        // 4. Create LogStore
        let logStore = LogStore()
        // 5. Create UIManager with fully initialized services
        return UIManager(
            engineService: engine,
            systemServicesManager: systemServices,
            logStore: logStore
        )
    }

    static let connectedManager: UIManager = {
        let m = createPreviewUIManager()
        m.isDaemonConnected = true
        m.isDriverConnected = true
        return m
    }()

    static let disconnectedManager: UIManager = {
        let m = createPreviewUIManager()
        m.isDaemonConnected = false
        m.isDriverConnected = false
        return m
    }()

    static var previews: some View {
        VStack(spacing: 12) {
            StatusBarView()
                .environmentObject(connectedManager)
            Divider()
            StatusBarView()
                .environmentObject(disconnectedManager)
        }
        .padding()
        .frame(width: 200)
    }
}
