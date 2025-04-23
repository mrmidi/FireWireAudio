import SwiftUI

struct AudioSubunitDetailsView: View {
    var subunit: AudioSubunitInfo // Use AudioSubunitInfo
    var onSelectPlug: ((AudioPlugInfo) -> Void)? = nil // Use AudioPlugInfo

    var body: some View {
        VStack(alignment: .leading) {
            Text("Audio Subunit (0x\(String(format:"%02X", subunit.subunitType.rawValue))) Details")
                .font(.title2).bold().padding(.bottom, 5)

            HStack {
                Text("Audio Dest Plugs:").bold()
                Text("\(subunit.audioDestPlugCount)")
                 Spacer()
                 Text("Audio Src Plugs:").bold()
                 Text("\(subunit.audioSourcePlugCount)")
            }
            .font(.callout)
            .padding(.bottom, 5)

            Divider()

            if !subunit.audioDestPlugs.isEmpty {
                 Text("Destination Plugs").font(.subheadline).bold().padding(.top, 5)
                PlugListView(title: "", plugs: subunit.audioDestPlugs, onSelect: onSelectPlug)
            }
            if !subunit.audioSourcePlugs.isEmpty {
                 Text("Source Plugs").font(.subheadline).bold().padding(.top, 5)
                PlugListView(title: "", plugs: subunit.audioSourcePlugs, onSelect: onSelectPlug)
            }
            // Placeholder for Function Block details
             Spacer() // Push content up if possible
        }
        .padding()
    }
}
