// === FWA-Control/ConnectionMatrixView.swift ===

import SwiftUI

// MARK: - Connection Cell View
struct ConnectionCellView: View {
    let sourcePlug: AudioPlugInfo
    let destinationPlug: AudioPlugInfo
    let isConnected: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Image(systemName: isConnected ? "checkmark.circle.fill" : "circle")
                .resizable()
                .aspectRatio(contentMode: .fit)
                .frame(width: 18, height: 18)
                .foregroundColor(isConnected ? .green : .secondary)
        }
        .buttonStyle(.plain)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color.secondary.opacity(0.1))
        .help("Connect \(sourcePlug.label) → \(destinationPlug.label)")
        .accessibilityElement()
        .accessibilityLabel("\(isConnected ? "Connected" : "Not connected") plug \(sourcePlug.label) to \(destinationPlug.label)")
        .accessibilityAddTraits(.isButton)
    }
}

// MARK: - Main Matrix View
struct ConnectionMatrixView: View {
    @EnvironmentObject var uiManager: UIManager

    @State private var selectedGuid: UInt64? = nil
    @State private var simulatedConnections: [UUID: UUID] = [:]

    var sortedDevices: [DeviceInfo] {
        uiManager.devices.values.sorted { $0.deviceName < $1.deviceName }
    }
    var selectedDevice: DeviceInfo? {
        guard let guid = selectedGuid else { return nil }
        return uiManager.devices[guid]
    }

    var sourcePlugs: [AudioPlugInfo] {
        guard let device = selectedDevice else { return [] }
        var sources: [AudioPlugInfo] = []
        sources.append(contentsOf: device.isoOutputPlugs)
        sources.append(contentsOf: device.externalOutputPlugs)
        sources.append(contentsOf: device.audioSubunit?.audioSourcePlugs ?? [])
        sources.append(contentsOf: device.musicSubunit?.musicSourcePlugs ?? [])
        return sources.sorted { $0.label < $1.label }
    }

    var destinationPlugs: [AudioPlugInfo] {
        guard let device = selectedDevice else { return [] }
        var destinations: [AudioPlugInfo] = []
        destinations.append(contentsOf: device.isoInputPlugs)
        destinations.append(contentsOf: device.externalInputPlugs)
        destinations.append(contentsOf: device.audioSubunit?.audioDestPlugs ?? [])
        destinations.append(contentsOf: device.musicSubunit?.musicDestPlugs ?? [])
        return destinations.sorted { $0.label < $1.label }
    }

    let headerHeight: CGFloat = 100
    let rowHeaderWidth: CGFloat = 150

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            deviceSelector
            Divider()
            matrixContent
        }
        .onChange(of: uiManager.devices) { _, _ in
            if selectedGuid == nil, let first = sortedDevices.first?.guid {
                selectedGuid = first
            } else if let guid = selectedGuid, uiManager.devices[guid] == nil {
                selectedGuid = nil
            }
        }
        .onAppear {
            if selectedGuid == nil, let first = sortedDevices.first?.guid {
                selectedGuid = first
            }
        }
    }

    private func toggleConnection(source: AudioPlugInfo, destination: AudioPlugInfo) {
        if simulatedConnections[destination.id] == source.id {
            simulatedConnections[destination.id] = nil
            print("SIMULATE: Disconnect \(source.label) from \(destination.label)")
        } else {
            simulatedConnections[destination.id] = source.id
            print("SIMULATE: Connect \(source.label) to \(destination.label)")
        }
    }

    @ViewBuilder private var deviceSelector: some View {
        HStack {
            Text("Device:")
                .font(.headline)
            Picker("Select Device", selection: $selectedGuid) {
                Text("No Device Selected").tag(UInt64?.none)
                ForEach(sortedDevices) { device in
                    Text("\(device.deviceName) (0x\(String(format: "%016llX", device.guid)))")
                        .tag(UInt64?.some(device.guid))
                }
            }
            .pickerStyle(.menu)
            .onChange(of: selectedGuid) { _, _ in
                simulatedConnections = [:]
            }
            Spacer()
        }
        .padding([.horizontal, .top])
    }

    @ViewBuilder private var matrixContent: some View {
        if let _ = selectedDevice,
           !sourcePlugs.isEmpty,
           !destinationPlugs.isEmpty {
            GeometryReader { geometry in
                matrixGrid(geometry: geometry)
            }
        } else if selectedDevice != nil {
            noPlugsView
        } else {
            selectDevicePrompt
        }
    }

    @ViewBuilder private func matrixGrid(geometry: GeometryProxy) -> some View {
        let columns = CGFloat(max(1, sourcePlugs.count))
        let availableWidth = geometry.size.width - rowHeaderWidth
        let calculatedSize = availableWidth / columns
        let gridItemSize = max(30, min(calculatedSize, 60))
        ScrollView([.horizontal, .vertical]) {
            Grid(alignment: .topLeading, horizontalSpacing: 1, verticalSpacing: 1) {
                gridHeaderRow(itemSize: gridItemSize)
                gridDataRows(itemSize: gridItemSize)
            }
            .padding(1)
            .background(Color(NSColor.gridColor))
        }
    }

    @ViewBuilder private func gridHeaderRow(itemSize: CGFloat) -> some View {
        GridRow {
            Color.clear
                .gridCellUnsizedAxes([.horizontal, .vertical])
                .frame(width: rowHeaderWidth, height: headerHeight)
            ForEach(sourcePlugs) { plug in
                SourcePlugHeaderView(plug: plug)
                    .frame(width: itemSize, height: headerHeight)
            }
        }
    }

    @ViewBuilder private func gridDataRows(itemSize: CGFloat) -> some View {
        ForEach(destinationPlugs) { dest in
            GridRow(alignment: .center) {
                DestinationPlugHeaderView(plug: dest)
                    .frame(width: rowHeaderWidth, height: itemSize)
                ForEach(sourcePlugs) { src in
                    let connected = simulatedConnections[dest.id] == src.id
                    ConnectionCellView(
                        sourcePlug: src,
                        destinationPlug: dest,
                        isConnected: connected
                    ) {
                        toggleConnection(source: src, destination: dest)
                    }
                    .frame(width: itemSize, height: itemSize)
                }
            }
        }
    }

    @ViewBuilder private var noPlugsView: some View {
        Text("Selected device has no compatible input/output plugs for matrix view.")
            .foregroundColor(.secondary)
            .padding()
            .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    @ViewBuilder private var selectDevicePrompt: some View {
        Text("Select a device to view the connection matrix.")
            .foregroundColor(.secondary)
            .padding()
            .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

// MARK: - Header Helper Views
struct SourcePlugHeaderView: View {
    let plug: AudioPlugInfo
    var body: some View {
        Text(plug.label)
            .font(.caption)
            .lineLimit(3)
            .rotationEffect(.degrees(-70), anchor: .leading)
            .frame(maxHeight: .infinity, alignment: .leading)
            .padding(.leading, 8)
            .background(Color.secondary.opacity(0.1))
            .border(Color.secondary.opacity(0.3), width: 0.5)
            .help(plug.label)
    }
}

struct DestinationPlugHeaderView: View {
    let plug: AudioPlugInfo
    var body: some View {
        Text(plug.label)
            .font(.caption)
            .lineLimit(2)
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.horizontal, 4)
            .background(Color.gray.opacity(0.1))
            .border(Color.gray.opacity(0.3), width: 0.5)
            .help(plug.label)
    }
}

// MARK: - Preview
// struct ConnectionMatrixView_Previews: PreviewProvider {
//     static var previews: some View {
//         // 1. Create UIManager for preview (pass nil dependencies)
//         let previewUIManager = UIManager(
//             engineService: nil,
//             systemServicesManager: nil,
//             logStore: nil
//         )

//         // 2. Create dummy DeviceInfo data
//         let isoInPlug = AudioPlugInfo(id: UUID(), subunitAddress: 0xFF, plugNumber: 0, direction: .input, usage: .isochronous, plugName: "Iso In 1")
//         let extInPlug = AudioPlugInfo(id: UUID(), subunitAddress: 0xFF, plugNumber: 1, direction: .input, usage: .external, plugName: "Ext In 2")
//         let audioDestPlug = AudioPlugInfo(id: UUID(), subunitAddress: 0x08, plugNumber: 0, direction: .input, usage: .audioSubunit, plugName: "Audio Dest 0")

//         let isoOutPlug = AudioPlugInfo(id: UUID(), subunitAddress: 0xFF, plugNumber: 0, direction: .output, usage: .isochronous, plugName: "Iso Out 1")
//         let extOutPlug = AudioPlugInfo(id: UUID(), subunitAddress: 0xFF, plugNumber: 1, direction: .output, usage: .external, plugName: "Ext Out 2")
//         let audioSrcPlug = AudioPlugInfo(id: UUID(), subunitAddress: 0x08, plugNumber: 0, direction: .output, usage: .audioSubunit, plugName: "Audio Src 0")

//         let dummyDevice = DeviceInfo(
//             guid: 0xABCDEF0123456789,
//             deviceName: "Matrix Preview Device",
//             vendorName: "Preview Vendor",
//             isConnected: true,
//             isoInputPlugs: [isoInPlug],
//             isoOutputPlugs: [isoOutPlug],
//             externalInputPlugs: [extInPlug],
//             externalOutputPlugs: [extOutPlug],
//             audioSubunit: AudioSubunitInfo(
//                 audioDestPlugs: [audioDestPlug],
//                 audioSourcePlugs: [audioSrcPlug]
//             )
//         )
//         // 3. Populate the UIManager's state for the preview
//         previewUIManager.devices[dummyDevice.guid] = dummyDevice
//         previewUIManager.isRunning = true // Simulate running state

//         // 4. Return the view with the environment object
//         return ConnectionMatrixView()
//             .environmentObject(previewUIManager)
//             .frame(width: 600, height: 500)
//     }
// }
