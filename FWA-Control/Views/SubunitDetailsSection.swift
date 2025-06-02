// === FWA-Control/Views/SubunitDetailsSection.swift ===

import SwiftUI

struct SubunitDetailsSection: View {
    let device: DeviceInfo
    let isExpanded: Bool
    let onToggle: (Bool) -> Void
    
    var body: some View {
        ExpandableSection(
            title: "Subunit Details",
            icon: "square.stack.3d.up.fill",
            isExpanded: isExpanded,
            onToggle: onToggle
        ) {
            subunitDetailsContent
        }
    }
    
    @ViewBuilder
    private var subunitDetailsContent: some View {
        VStack(spacing: 16) {
            if let audioSubunit = device.audioSubunit {
                SubunitCard(
                    title: "Audio Subunit",
                    subtitle: "0x\(String(format: "%02X", audioSubunit.subunitType.rawValue))",
                    icon: "speaker.wave.2.fill",
                    color: .purple,
                    destPlugCount: Int(audioSubunit.audioDestPlugCount),
                    sourcePlugCount: Int(audioSubunit.audioSourcePlugCount)
                )
            }
            
            if let musicSubunit = device.musicSubunit {
                SubunitCard(
                    title: "Music Subunit", 
                    subtitle: "0x\(String(format: "%02X", musicSubunit.subunitType.rawValue))",
                    icon: "music.note",
                    color: .orange,
                    destPlugCount: Int(musicSubunit.musicDestPlugCount),
                    sourcePlugCount: Int(musicSubunit.musicSourcePlugCount)
                )
            }
            
            if device.audioSubunit == nil && device.musicSubunit == nil {
                Text("No subunits detected")
                    .foregroundStyle(.secondary)
                    .padding()
            }
        }
    }
}

// MARK: - SubunitCard

struct SubunitCard: View {
    let title: String
    let subtitle: String
    let icon: String
    let color: Color
    let destPlugCount: Int
    let sourcePlugCount: Int
    
    var body: some View {
        HStack {
            Image(systemName: icon)
                .font(.title2)
                .foregroundColor(color)
                .frame(width: 40)
            
            VStack(alignment: .leading, spacing: 4) {
                Text(title)
                    .font(.headline)
                
                Text(subtitle)
                    .font(.caption)
                    .fontDesign(.monospaced)
                    .foregroundStyle(.secondary)
            }
            
            Spacer()
            
            VStack(alignment: .trailing, spacing: 4) {
                Text("In: \(destPlugCount)")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                
                Text("Out: \(sourcePlugCount)")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .padding()
        .background(Color(NSColor.quaternaryLabelColor).opacity(0.2), in: RoundedRectangle(cornerRadius: 12))
    }
}