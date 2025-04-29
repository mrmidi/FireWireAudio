import SwiftUI

struct StatusBarView: View {
    @EnvironmentObject var manager: DeviceManager

    var body: some View {
        HStack(spacing: 12) {
            Spacer()
            DeviceStatusIndicatorView(
                statusTypeLabel: "Daemon",
                systemImageName: manager.isDaemonConnected ? "bolt.fill" : "bolt.slash.fill",
                isConnected: manager.isDaemonConnected,
                helpText: manager.isDaemonConnected ? "Daemon connection active" : "Daemon connection inactive"
            )
            DeviceStatusIndicatorView(
                statusTypeLabel: "Driver",
                systemImageName: manager.isDriverConnected ? "puzzlepiece.extension.fill" : "puzzlepiece.extension",
                isConnected: manager.isDriverConnected,
                helpText: manager.isDriverConnected ? "Driver connection active" : "Driver connection inactive or driver not installed"
            )
        }
        .padding(.horizontal)
        .padding(.vertical, 5)
        .background(.thinMaterial)
    }
}

struct StatusBarView_Previews: PreviewProvider {
  static let connectedManager: DeviceManager = {
    let m = DeviceManager()
    m.isDaemonConnected = true
    m.isDriverConnected = true
    return m
  }()

  static let disconnectedManager: DeviceManager = {
    let m = DeviceManager()
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
