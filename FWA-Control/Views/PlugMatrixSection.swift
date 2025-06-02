// === FWA-Control/Views/PlugMatrixSection.swift ===

import SwiftUI

struct PlugMatrixSection: View {
    let device: DeviceInfo
    let isExpanded: Bool
    let onPlugTap: (AudioPlugInfo) -> Void
    let onToggle: (Bool) -> Void
    
    var body: some View {
        ExpandableSection(
            title: "Plug Matrix",
            icon: "rectangle.grid.3x2",
            isExpanded: isExpanded,
            onToggle: onToggle
        ) {
            plugMatrixContent
        }
    }
    
    @ViewBuilder
    private var plugMatrixContent: some View {
        VStack(spacing: 16) {
            // Unit Plugs
            if hasIsochronousPlugs {
                PlugGroupView(
                    title: "Isochronous Plugs",
                    inputPlugs: device.isoInputPlugs,
                    outputPlugs: device.isoOutputPlugs,
                    color: .blue,
                    onPlugTap: onPlugTap
                )
            }
            
            if hasExternalPlugs {
                PlugGroupView(
                    title: "External Plugs",
                    inputPlugs: device.externalInputPlugs,
                    outputPlugs: device.externalOutputPlugs,
                    color: .green,
                    onPlugTap: onPlugTap
                )
            }
            
            // Subunit Plugs
            if let audioSubunit = device.audioSubunit {
                PlugGroupView(
                    title: "Audio Subunit Plugs",
                    inputPlugs: audioSubunit.audioDestPlugs,
                    outputPlugs: audioSubunit.audioSourcePlugs,
                    color: .purple,
                    onPlugTap: onPlugTap
                )
            }
            
            if let musicSubunit = device.musicSubunit {
                PlugGroupView(
                    title: "Music Subunit Plugs",
                    inputPlugs: musicSubunit.musicDestPlugs,
                    outputPlugs: musicSubunit.musicSourcePlugs,
                    color: .orange,
                    onPlugTap: onPlugTap
                )
            }
        }
    }
    
    private var hasIsochronousPlugs: Bool {
        !device.isoInputPlugs.isEmpty || !device.isoOutputPlugs.isEmpty
    }
    
    private var hasExternalPlugs: Bool {
        !device.externalInputPlugs.isEmpty || !device.externalOutputPlugs.isEmpty
    }
}

// MARK: - PlugGroupView

struct PlugGroupView: View {
    let title: String
    let inputPlugs: [AudioPlugInfo]
    let outputPlugs: [AudioPlugInfo]
    let color: Color
    let onPlugTap: (AudioPlugInfo) -> Void
    
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(title)
                .font(.subheadline)
                .fontWeight(.semibold)
                .foregroundColor(color)
            
            HStack(spacing: 16) {
                if !inputPlugs.isEmpty {
                    plugColumn(title: "Input", plugs: inputPlugs)
                }
                
                if !inputPlugs.isEmpty && !outputPlugs.isEmpty {
                    Divider()
                }
                
                if !outputPlugs.isEmpty {
                    plugColumn(title: "Output", plugs: outputPlugs)
                }
            }
        }
        .padding()
        .background(Color(NSColor.quaternaryLabelColor).opacity(0.2), in: RoundedRectangle(cornerRadius: 12))
    }
    
    @ViewBuilder
    private func plugColumn(title: String, plugs: [AudioPlugInfo]) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(title)
                .font(.caption)
                .foregroundStyle(.secondary)
            
            LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 4), count: min(plugs.count, 4)), spacing: 4) {
                ForEach(plugs) { plug in
                    PlugChip(plug: plug, color: color, direction: plug.direction) {
                        onPlugTap(plug)
                    }
                }
            }
        }
    }
}

// MARK: - PlugChip

struct PlugChip: View {
    let plug: AudioPlugInfo
    let color: Color
    let direction: PlugDirection
    let onTap: () -> Void
    
    @State private var isHovered = false
    
    var body: some View {
        Button(action: onTap) {
            VStack(spacing: 4) {
                Image(systemName: direction == .input ? "arrow.down.circle.fill" : "arrow.up.circle.fill")
                    .font(.title3)
                    .foregroundColor(color)
                
                Text("\(plug.plugNumber)")
                    .font(.caption2)
                    .fontWeight(.medium)
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 6)
            .background(Color(NSColor.controlBackgroundColor), in: RoundedRectangle(cornerRadius: 8))
            .overlay(
                RoundedRectangle(cornerRadius: 8)
                    .stroke(isHovered ? color.opacity(0.6) : Color.clear, lineWidth: 1.5)
            )
            .scaleEffect(isHovered ? 1.05 : 1.0)
            .animation(.easeInOut(duration: 0.15), value: isHovered)
        }
        .buttonStyle(.plain)
        .onHover { hovering in
            isHovered = hovering
        }
        .help("Click to view details for \(plug.label)")
    }
}