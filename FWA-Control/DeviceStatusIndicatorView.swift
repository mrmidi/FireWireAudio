import SwiftUI

struct DeviceStatusIndicatorView: View {
    var label: String? = nil // Optional label (e.g., "Daemon", "Driver")
    var isConnected: Bool
    var body: some View {
        HStack {
            if let label = label {
                Text("\(label):")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            Circle()
                .fill(isConnected ? Color.green : Color.red)
                .frame(width: 10, height: 10)
            Text(isConnected ? "Connected" : "Disconnected")
                .foregroundColor(isConnected ? .green : .red)
                .font(.caption)
        }
    }
}
