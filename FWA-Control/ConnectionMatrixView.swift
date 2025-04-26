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
    @EnvironmentObject var manager: DeviceManager

    @State private var selectedGuid: UInt64? = nil
    @State private var simulatedConnections: [UUID: UUID] = [:]

    var sortedDevices: [DeviceInfo] {
        manager.devices.values.sorted { $0.deviceName < $1.deviceName }
    }
    var selectedDevice: DeviceInfo? {
        guard let guid = selectedGuid else { return nil }
        return manager.devices[guid]
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
            // Device Selector
            HStack {
                Text("Device:")
                    .font(.headline)
                Picker("Select Device", selection: $selectedGuid) {
                    Text("No Device Selected").tag(UInt64?.none)
                    ForEach(sortedDevices) { device in
                        Text("\(device.deviceName) (0x\(String(format: "%llX", device.guid)))")
                            .tag(UInt64?.some(device.guid))
                    }
                }
                .pickerStyle(.menu)
                .onChange(of: selectedGuid) { _ in
                    simulatedConnections = [:]
                }
                Spacer()
            }
            .padding([.horizontal, .top])

            Divider()

            // Matrix
            if let _ = selectedDevice,
               !sourcePlugs.isEmpty,
               !destinationPlugs.isEmpty
            {
                GeometryReader { geometry in
                    let gridItemSize = min((geometry.size.width - rowHeaderWidth) / CGFloat(sourcePlugs.count), 40)
                    let fixedItems = Array(repeating: GridItem(.fixed(gridItemSize), spacing: 1), count: sourcePlugs.count)

                    ScrollView([.horizontal, .vertical]) {
                        Grid(alignment: .topLeading, horizontalSpacing: 1, verticalSpacing: 1) {
                            // Header Row
                            GridRow {
                                Color.clear
                                    .gridCellUnsizedAxes([.horizontal, .vertical])
                                    .frame(width: rowHeaderWidth, height: headerHeight)
                                ForEach(sourcePlugs) { plug in
                                    SourcePlugHeaderView(plug: plug)
                                        .frame(width: gridItemSize, height: headerHeight)
                                }
                            }
                            // Data Rows
                            ForEach(destinationPlugs) { dest in
                                GridRow(alignment: .center) {
                                    DestinationPlugHeaderView(plug: dest)
                                        .frame(width: rowHeaderWidth, height: gridItemSize)
                                    ForEach(sourcePlugs) { src in
                                        let connected = simulatedConnections[dest.id] == src.id
                                        ConnectionCellView(
                                            sourcePlug: src,
                                            destinationPlug: dest,
                                            isConnected: connected
                                        ) {
                                            toggleConnection(source: src, destination: dest)
                                        }
                                        .frame(width: gridItemSize, height: gridItemSize)
                                    }
                                }
                            }
                        }
                        .padding(1)
                        .background(Color(NSColor.gridColor))
                    }
                }
            }
            else if selectedDevice != nil {
                Text("Selected device has no compatible input/output plugs for matrix view.")
                    .foregroundColor(.secondary)
                    .padding()
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
            else {
                Text("Select a device to view the connection matrix.")
                    .foregroundColor(.secondary)
                    .padding()
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .onChange(of: manager.devices) { _ in
            if selectedGuid == nil, let first = sortedDevices.first?.guid {
                selectedGuid = first
            } else if let guid = selectedGuid, manager.devices[guid] == nil {
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
struct ConnectionMatrixView_Previews: PreviewProvider {
    static var previews: some View {
        let previewManager = DeviceManager()
        let dummyDevice = DeviceInfo(
            guid: 0xABCDEF0123456789,
            deviceName: "Matrix Preview Device",
            vendorName: "Preview Vendor",
            isConnected: true,
            numIsoInPlugs: 1,
            numIsoOutPlugs: 1,
            isoInputPlugs: [
                AudioPlugInfo(
                    id: UUID(),
                    subunitAddress: 0xFF,
                    plugNumber: 0,
                    direction: .input,
                    usage: .isochronous,
                    plugName: "Iso In 1"
                )
            ],
            isoOutputPlugs: [
                AudioPlugInfo(
                    id: UUID(),
                    subunitAddress: 0xFF,
                    plugNumber: 0,
                    direction: .output,
                    usage: .isochronous,
                    plugName: "Iso Out 1"
                )
            ],
            externalInputPlugs: [],
            externalOutputPlugs: [],
            audioSubunit: AudioSubunitInfo(
                audioDestPlugCount: 1,
                audioSourcePlugCount: 1,
                audioDestPlugs: [
                    AudioPlugInfo(
                        id: UUID(),
                        subunitAddress: 0x08,
                        plugNumber: 0,
                        direction: .input,
                        usage: .audioSubunit,
                        plugName: "Audio Dest 1"
                    )
                ],
                audioSourcePlugs: [
                    AudioPlugInfo(
                        id: UUID(),
                        subunitAddress: 0x08,
                        plugNumber: 0,
                        direction: .output,
                        usage: .audioSubunit,
                        plugName: "Audio Src 1"
                    )
                ]
            ),
            musicSubunit: nil
        )
        previewManager.devices[dummyDevice.guid] = dummyDevice
        previewManager.isRunning = true

        return ConnectionMatrixView()
            .environmentObject(previewManager)
            .frame(width: 500, height: 400)
    }
}
