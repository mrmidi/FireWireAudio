import SwiftUI

struct DeviceStatusIndicatorView: View {
    var statusTypeLabel: String // e.g., "Daemon", "Driver". Used for default help text.
    var systemImageName: String // System image name for the icon
    var isConnected: Bool
    var helpText: String? = nil // Optional explicit help tooltip

    var body: some View {
        HStack(spacing: 4) {
            Image(systemName: systemImageName)
                .foregroundColor(isConnected ? .green : .secondary)
                .imageScale(.medium)
        }
        .help(helpText ?? (isConnected ? "\(statusTypeLabel) Status: Connected" : "\(statusTypeLabel) Status: Disconnected"))
    }
}
