// === FWA-Control/Views/AudioConfigurationSection.swift ===

import SwiftUI

struct AudioConfigurationSection: View {
    let device: DeviceInfo
    let isExpanded: Bool
    let onToggle: (Bool) -> Void
    
    var body: some View {
        ExpandableSection(
            title: "Audio Configuration",
            icon: "slider.horizontal.3",
            isExpanded: isExpanded,
            onToggle: onToggle
        ) {
            audioConfigurationContent
        }
    }
    
    @ViewBuilder
    private var audioConfigurationContent: some View {
        VStack(spacing: 16) {
            if let isoInPlug = device.isoInputPlugs.first,
               let currentFormat = isoInPlug.currentStreamFormat {
                
                currentFormatCard(currentFormat)
                
                if !isoInPlug.supportedStreamFormats.isEmpty {
                    supportedSampleRates(
                        supportedFormats: isoInPlug.supportedStreamFormats,
                        currentFormat: currentFormat
                    )
                }
            } else {
                Text("No audio format information available")
                    .foregroundStyle(.secondary)
                    .padding()
            }
        }
    }
    
    @ViewBuilder
    private func currentFormatCard(_ currentFormat: AudioStreamFormat) -> some View {
        HStack {
            VStack(alignment: .leading, spacing: 4) {
                Text("Current Format")
                    .font(.headline)
                Text("\(currentFormat.formatType.description)")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }
            
            Spacer()
            
            VStack(alignment: .trailing, spacing: 4) {
                Text(currentFormat.sampleRate.description)
                    .font(.title3)
                    .fontWeight(.semibold)
                    .foregroundColor(.blue)
                Text("\(currentFormat.channels.count) channel\(currentFormat.channels.count == 1 ? "" : "s")")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .padding()
        .background(Color(NSColor.quaternaryLabelColor).opacity(0.2), in: RoundedRectangle(cornerRadius: 12))
    }
    
    @ViewBuilder
    private func supportedSampleRates(
        supportedFormats: [AudioStreamFormat],
        currentFormat: AudioStreamFormat
    ) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Supported Sample Rates")
                .font(.headline)
            
            let uniqueRates = Array(Set(supportedFormats.map(\.sampleRate)))
            
            LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 8), count: 2), spacing: 8) {
                ForEach(uniqueRates, id: \.self) { rate in
                    SampleRateChip(
                        rate: rate,
                        isActive: rate == currentFormat.sampleRate,
                        onSelect: {
                            // TODO: Implement sample rate change
                            print("Selected sample rate: \(rate)")
                        }
                    )
                }
            }
        }
    }
}