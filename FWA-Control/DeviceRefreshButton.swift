import SwiftUI

struct DeviceRefreshButton: View {
    var action: () -> Void
    
    var body: some View {
        Button(action: action) {
            Label("Refresh", systemImage: "arrow.clockwise")
                .font(.caption)
        }
        .buttonStyle(.bordered)
        .controlSize(.small)
    }
}
