// === FWA-Control/OverviewView.swift ===

import SwiftUI

struct OverviewView: View {
    @EnvironmentObject var uiManager: UIManager
    @State private var selectedGuid: UInt64?
    @State private var selectedPlug: AudioPlugInfo?
    @State private var showPlugDetail = false
    @State private var expandedSections: Set<String> = ["audio", "plugs"]
    
    private var devices: [DeviceInfo] {
        uiManager.devices.values.sorted { $0.deviceName < $1.deviceName }
    }
    
    private var device: DeviceInfo? {
        guard let g = selectedGuid else { return nil }
        return uiManager.devices[g]
    }
    
    var body: some View {
        ScrollView {
            LazyVStack(spacing: 20) {
                DevicePickerView(
                    devices: devices,
                    selectedGuid: $selectedGuid
                )
                
                if let device = device {
                    DeviceOverviewCard(device: device, uiManager: uiManager)
                    
                    AudioConfigurationSection(
                        device: device,
                        isExpanded: expandedSections.contains("audio"),
                        onToggle: { isExpanded in
                            withAnimation(.easeInOut(duration: 0.3)) {
                                if isExpanded {
                                    expandedSections.insert("audio")
                                } else {
                                    expandedSections.remove("audio")
                                }
                            }
                        }
                    )
                    
                    PlugMatrixSection(
                        device: device,
                        isExpanded: expandedSections.contains("plugs"),
                        onPlugTap: { plug in
                            selectedPlug = plug
                            showPlugDetail = true
                        },
                        onToggle: { isExpanded in
                            withAnimation(.easeInOut(duration: 0.3)) {
                                if isExpanded {
                                    expandedSections.insert("plugs")
                                } else {
                                    expandedSections.remove("plugs")
                                }
                            }
                        }
                    )
                    
                    SubunitDetailsSection(
                        device: device,
                        isExpanded: expandedSections.contains("subunits"),
                        onToggle: { isExpanded in
                            withAnimation(.easeInOut(duration: 0.3)) {
                                if isExpanded {
                                    expandedSections.insert("subunits")
                                } else {
                                    expandedSections.remove("subunits")
                                }
                            }
                        }
                    )
                    
                    TechnicalSpecsSection(
                        device: device,
                        isExpanded: expandedSections.contains("specs"),
                        onToggle: { isExpanded in
                            withAnimation(.easeInOut(duration: 0.3)) {
                                if isExpanded {
                                    expandedSections.insert("specs")
                                } else {
                                    expandedSections.remove("specs")
                                }
                            }
                        }
                    )
                } else {
                    EmptyDeviceStateView(uiManager: uiManager)
                }
            }
            .padding()
        }
        .navigationTitle("Device Overview")
        .sheet(isPresented: $showPlugDetail) {
            if let plug = selectedPlug {
                NavigationStack {
                    PlugDetailView(plug: plug)
                        .navigationTitle("Plug Details")
                        .toolbar {
                            ToolbarItem(placement: .primaryAction) {
                                Button("Done") { showPlugDetail = false }
                            }
                        }
                }
                .frame(minWidth: 600, minHeight: 500)
            }
        }
        .onAppear {
            if selectedGuid == nil, let first = devices.first?.guid {
                selectedGuid = first
            }
        }
        .onChange(of: devices) { _, newDevices in
            if selectedGuid == nil, let first = newDevices.first?.guid {
                selectedGuid = first
            } else if let guid = selectedGuid, !newDevices.contains(where: { $0.guid == guid }) {
                selectedGuid = newDevices.first?.guid
            }
        }
    }
}