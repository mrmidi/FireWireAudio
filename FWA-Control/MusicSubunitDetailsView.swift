import SwiftUI

struct MusicSubunitDetailsView: View {
    var subunit: MusicSubunitInfo // Use MusicSubunitInfo
    var onSelectPlug: ((AudioPlugInfo) -> Void)? = nil // Use AudioPlugInfo

    var body: some View {
        VStack(alignment: .leading) {
            Text("Music Subunit (0x\(String(format:"%02X", subunit.subunitType.rawValue))) Details")
                .font(.title2).bold().padding(.bottom, 5)

            HStack {
                Text("Music Dest Plugs:").bold()
                Text("\(subunit.musicDestPlugCount)")
                Spacer()
                Text("Music Src Plugs:").bold()
                Text("\(subunit.musicSourcePlugCount)")
            }
            .font(.callout)
            .padding(.bottom, 5)

            Divider()

            if !subunit.musicDestPlugs.isEmpty {
                 Text("Destination Plugs").font(.subheadline).bold().padding(.top, 5)
                PlugListView(title: "", plugs: subunit.musicDestPlugs, onSelect: onSelectPlug)
            }
            if !subunit.musicSourcePlugs.isEmpty {
                 Text("Source Plugs").font(.subheadline).bold().padding(.top, 5)
                PlugListView(title: "", plugs: subunit.musicSourcePlugs, onSelect: onSelectPlug)
            }

            if let infoBlocks = subunit.statusDescriptorInfoBlocks, !infoBlocks.isEmpty {
                Divider().padding(.vertical, 5)
                Text("Status Descriptor Info Blocks")
                    .font(.headline)
                    .padding(.bottom, 2)

                ForEach(infoBlocks) { infoBlock in
                    DisclosureGroup(
                        content: { InfoBlockDetailView(infoBlock: infoBlock).padding(.leading) },
                        label: { Label(infoBlock.typeName, systemImage: "info.circle").font(.subheadline) }
                    )
                    Divider()
                }
            }
             Spacer() // Push content up if possible
        }
        .padding()
    }
}
