import SwiftUI

struct DeviceBasicInfoView: View {
    var guid: UInt64 // Use UInt64
    var name: String
    var vendor: String
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text("GUID:").bold()
                // Format UInt64 hex properly
                Text("0x\(String(format: "%016llX", guid))")
                    .font(.system(.body, design: .monospaced)) // Monospaced for GUID
            }
            HStack {
                Text("Device Name:").bold()
                Text(name)
            }
            HStack {
                Text("Vendor Name:").bold()
                Text(vendor)
            }
        }
        .padding(.bottom, 5)
    }
}
