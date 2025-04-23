import SwiftUI

struct DeviceStatusIndicatorView: View {
    var isConnected: Bool
    var body: some View {
        HStack {
            Circle()
                .fill(isConnected ? Color.green : Color.red)
                .frame(width: 10, height: 10)
            Text(isConnected ? "Connected" : "Disconnected")
                .foregroundColor(isConnected ? .green : .red)
                .font(.caption)
        }
    }
}
