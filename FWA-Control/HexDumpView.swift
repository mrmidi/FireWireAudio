import SwiftUI

struct HexDumpView: View {
    var data: Data
    var body: some View {
        ScrollView(.horizontal) {
            Text(data.map { String(format: "%02X", $0) }.joined(separator: " "))
                .font(.system(.body, design: .monospaced))
                .padding(4)
                .background(Color.gray.opacity(0.2))
                .cornerRadius(4)
        }
    }
}
