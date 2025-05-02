// === FWA-Control/OverviewView.swift ===

import SwiftUI

/// The six sections available in the sidebar
enum OverviewSection: String, CaseIterable, Identifiable {
    case basicInfo       = "Basic Info"
    case plugCounts      = "Unit Plug Counts"
    case unitPlugs       = "Unit Plugs Detail"
    case subunitsSummary = "Subunits Summary"
    case musicDetails    = "Music Subunit Details"
    case audioDetails    = "Audio Subunit Details"

    var id: String { rawValue }
}

struct OverviewView: View {
    @EnvironmentObject var uiManager: UIManager // Correct EnvironmentObject
    @State private var selectedGuid: UInt64?
    
    private var devices: [DeviceInfo] {
        // --- FIX: Use uiManager ---
        uiManager.devices.values.sorted { $0.deviceName < $1.deviceName }
    }
    private var device: DeviceInfo? {
        guard let g = selectedGuid else { return nil }
        // --- FIX: Use uiManager ---
        return uiManager.devices[g]
    }
    
    var body: some View {
        ScrollView { // <-- Make content scrollable
            VStack(spacing: 12) {
                // Device selector
                HStack {
                    Text("Device:")
                        .bold()
                    Picker("Device", selection: $selectedGuid) {
                        Text("—").tag(UInt64?.none)
                        ForEach(devices) { d in
                            Text(d.deviceName).tag(Optional(d.guid))
                        }
                    }
                    .pickerStyle(.menu)
                    Spacer()
                }
                .padding(.horizontal)
                .onAppear {
                    if selectedGuid == nil, let first = devices.first?.guid {
                        selectedGuid = first
                    }
                }
                
                Divider()
                
                // If no device, prompt
                if device == nil {
                    Text("Please select a device above.")
                        .foregroundColor(.secondary)
                        .padding()
                } else if let d = device {
                    // Summary cards in a grid
                    LazyVGrid(columns: Array(repeating: .init(.flexible(), spacing: 12), count: 2), spacing: 12) {
                        // Basic Info
                        GroupBox("Basic Info") {
                            DeviceBasicInfoView(guid: d.guid, name: d.deviceName, vendor: d.vendorName)
                                .padding(.vertical, 4)
                        }
                        
                        // Plug Counts
                        GroupBox("Plug Counts") {
                            HStack {
                                summaryItem(title: "Iso In", count: d.numIsoInPlugs)
                                summaryItem(title: "Iso Out", count: d.numIsoOutPlugs)
                                summaryItem(title: "Ext In", count: d.numExtInPlugs)
                                summaryItem(title: "Ext Out", count: d.numExtOutPlugs)
                            }
                            .padding(.vertical, 4)
                        }
                        
                        // Unit Plugs Detail
                        GroupBox("Unit Plugs") {
                            UnitInfoSectionView(deviceInfo: d, onSelectPlug: { _ in })
                                .padding(.vertical, 4)
                        }
                        
                        // Subunits Summary
                        GroupBox("Subunits") {
                            SubunitInfoSectionView(musicSubunit: d.musicSubunit, audioSubunit: d.audioSubunit)
                                .padding(.vertical, 4)
                        }
                    }
                    .padding(.horizontal)
                    
                    // Full-width detailed subunit panels
                    if let m = d.musicSubunit {
                        VStack(alignment: .leading, spacing: 8) { // Changed from GroupBox to VStack
                            Text("Music Subunit Details")
                                .font(.headline)
                                .padding(.bottom, 2)
                            MusicSubunitDetailsView(subunit: m, onSelectPlug: { _ in })
                                .padding(.vertical, 4)
                        }
                        .padding(.horizontal)
                    }
                    
                    if let a = d.audioSubunit {
                        VStack(alignment: .leading, spacing: 8) { // Changed from GroupBox to VStack
                            Text("Audio Subunit Details")
                                .font(.headline)
                                .padding(.bottom, 2)
                            AudioSubunitDetailsView(subunit: a, onSelectPlug: { _ in })
                                .padding(.vertical, 4)
                        }
                        .padding(.horizontal)
                    }
                }
                
                Spacer(minLength: 12)
            }
            .padding(.top)
            .frame(maxWidth: .infinity, alignment: .top) // <-- Help with resizing
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top) // <-- Expand with window
    }
    
    // Helper for the count summary
    @ViewBuilder
    private func summaryItem(title: String, count: UInt32) -> some View {
        VStack {
            Text(title).font(.caption)
            Text("\(count)").font(.title2).bold()
        }
        .frame(maxWidth: .infinity)
    }
}

// MARK: - Preview
//struct OverviewView_Previews: PreviewProvider {
//    static var previews: some View {
//        let previewManager = UIManager()
//        let dummyDevice = DeviceInfo(
//            guid: 0xABCDEF0123456789,
//            deviceName: "Preview Device",
//            vendorName: "Preview Vendor",
//            isConnected: true,
//            numIsoInPlugs: 2, isoInputPlugs: [
//                AudioPlugInfo(subunitAddress: 0xFF, plugNumber: 0, direction: .input, usage: .isochronous, plugName: "Iso In 1"),
//                AudioPlugInfo(subunitAddress: 0xFF, plugNumber: 1, direction: .input, usage: .isochronous, plugName: "Iso In 2")
//            ],
//            audioSubunit: AudioSubunitInfo(audioDestPlugCount: 1, audioDestPlugs: [AudioPlugInfo(subunitAddress: 0x08, plugNumber: 0, direction: .input, usage: .audioSubunit, plugName: "Audio Dest 1")])
//        )
//        previewManager.devices[dummyDevice.guid] = dummyDevice
//        previewManager.isRunning = true
//
//        return OverviewView()
//            .environmentObject(previewManager)
//            .frame(width: 600, height: 700)
//    }
//}
