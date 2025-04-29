import SwiftUI

struct PlugListView: View {
    var title: String
    var plugs: [AudioPlugInfo] // Use AudioPlugInfo
    var onSelect: ((AudioPlugInfo) -> Void)? = nil // Use AudioPlugInfo
    @State private var hoveredPlugID: AudioPlugInfo.ID? = nil

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            // Title moved outside ForEach if it's a section header
            ForEach(plugs) { plug in
                Button(action: { onSelect?(plug) }) {
                    HStack(spacing: 8) {
                        Image(systemName: plug.direction == .input ? "arrow.down.left.circle.fill" : "arrow.up.right.circle.fill") // More distinct icons
                            .foregroundColor(plug.direction == .input ? .blue : .orange)
                            .imageScale(.medium)
                        Text(plug.label) // Use the calculated label
                            .fontWeight(.regular)
                            .lineLimit(1)
                            .truncationMode(.tail)
                        Spacer()
                         // Optionally show connection status briefly
                        if plug.direction == .input, let connInfo = plug.connectionInfo, connInfo.isConnected {
                             Image(systemName: "link")
                                 .foregroundColor(.secondary)
                                 .imageScale(.small)
                        }
                        Text("#\(plug.plugNumber)")
                            .foregroundColor(.gray)
                            .font(.caption)
                    }
                    .padding(.vertical, 4) // Reduced padding
                    .padding(.horizontal, 8)
                    .contentShape(Rectangle()) // Ensure whole area is tappable
                     .background(hoveredPlugID == plug.id ? Color.gray.opacity(0.15) : Color.clear) // Use gray for hover
                     .cornerRadius(5)
                }
                .buttonStyle(PlainButtonStyle())
                .onHover { hovering in
                     withAnimation(.easeInOut(duration: 0.1)) { // Add animation
                         hoveredPlugID = hovering ? plug.id : nil
                     }
                }
                 Divider().padding(.leading, 25) // Indent divider slightly
            }
        }
        .padding(.bottom, 4)
    }
}
