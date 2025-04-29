import SwiftUI

struct UnitInfoSectionView: View {
    var deviceInfo: DeviceInfo // Pass the whole DeviceInfo
    var onSelectPlug: ((AudioPlugInfo) -> Void)? // Callback

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("UNIT PLUGS")
                .font(.caption.weight(.semibold))
                .foregroundColor(.secondary)
                .padding(.bottom, 2)

            HStack {
                Text("Iso In:")   .frame(width: 60, alignment: .leading)
                Text("\(deviceInfo.numIsoInPlugs)")
                Spacer()
                Text("Iso Out:")  .frame(width: 60, alignment: .leading)
                Text("\(deviceInfo.numIsoOutPlugs)")
                 Spacer()
                 Text("Ext In:")   .frame(width: 60, alignment: .leading)
                 Text("\(deviceInfo.numExtInPlugs)")
                 Spacer()
                 Text("Ext Out:")  .frame(width: 60, alignment: .leading)
                 Text("\(deviceInfo.numExtOutPlugs)")
            }
            .font(.callout)
            .padding(.bottom, 5)

            Divider()

            if !deviceInfo.isoInputPlugs.isEmpty {
                Text("Isochronous Input").font(.subheadline).bold().padding(.top, 5)
                PlugListView(title: "", plugs: deviceInfo.isoInputPlugs, onSelect: onSelectPlug)
            }
            if !deviceInfo.isoOutputPlugs.isEmpty {
                 Text("Isochronous Output").font(.subheadline).bold().padding(.top, 5)
                PlugListView(title: "", plugs: deviceInfo.isoOutputPlugs, onSelect: onSelectPlug)
            }
             if !deviceInfo.externalInputPlugs.isEmpty {
                 Text("External Input").font(.subheadline).bold().padding(.top, 5)
                 PlugListView(title: "", plugs: deviceInfo.externalInputPlugs, onSelect: onSelectPlug)
             }
             if !deviceInfo.externalOutputPlugs.isEmpty {
                 Text("External Output").font(.subheadline).bold().padding(.top, 5)
                 PlugListView(title: "", plugs: deviceInfo.externalOutputPlugs, onSelect: onSelectPlug)
             }
        }
    }
}
