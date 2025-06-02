// === FWA-Control/Views/TechnicalSpecsSection.swift ===

import SwiftUI

struct TechnicalSpecsSection: View {
    let device: DeviceInfo
    let isExpanded: Bool
    let onToggle: (Bool) -> Void
    
    var body: some View {
        ExpandableSection(
            title: "Technical Specifications",
            icon: "info.circle",
            isExpanded: isExpanded,
            onToggle: onToggle
        ) {
            technicalSpecsContent
        }
    }
    
    @ViewBuilder
    private var technicalSpecsContent: some View {
        VStack(spacing: 12) {
            deviceIdentifiers
            Divider()
            plugCounts
        }
    }
    
    @ViewBuilder
    private var deviceIdentifiers: some View {
        VStack(spacing: 8) {
            SpecRow(label: "Model ID", value: device.modelIdString)
            SpecRow(label: "Vendor ID", value: device.vendorIdString)
            SpecRow(label: "GUID", value: "0x\(String(format: "%016llX", device.guid))")
        }
    }
    
    @ViewBuilder
    private var plugCounts: some View {
        VStack(spacing: 8) {
            SpecRow(label: "Iso Input Plugs", value: "\(device.numIsoInPlugs)")
            SpecRow(label: "Iso Output Plugs", value: "\(device.numIsoOutPlugs)")
            SpecRow(label: "External Input Plugs", value: "\(device.numExtInPlugs)")
            SpecRow(label: "External Output Plugs", value: "\(device.numExtOutPlugs)")
        }
    }
}