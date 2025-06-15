// FWA-Control/CoreAudioHALView.swift
import SwiftUI

struct CoreAudioHALView: View {
    @EnvironmentObject var uiManager: UIManager
    @State private var devicePropsExpanded = true
    @State private var bufferLatencyExpanded = true  
    @State private var sampleRateExpanded = true
    @State private var streamsExpanded = true
    
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                if let info = uiManager.halDeviceInfo {
                    // --- Device Info Section ---
                    ExpandableSection(
                        title: "Device Properties", 
                        icon: "hifispeaker.and.homepodmini", 
                        isExpanded: devicePropsExpanded,
                        onToggle: { devicePropsExpanded = $0 }
                    ) {
                        VStack(spacing: 8) {
                            SpecRow(label: "Device Name", value: info.deviceName)
                            SpecRow(label: "Model Name", value: info.modelName)
                            SpecRow(label: "UID", value: info.deviceUID)
                        }
                    }

                    // --- Buffer & Latency Section ---
                    ExpandableSection(
                        title: "Buffer & Latency", 
                        icon: "timer", 
                        isExpanded: bufferLatencyExpanded,
                        onToggle: { bufferLatencyExpanded = $0 }
                    ) {
                        VStack(spacing: 8) {
                            SpecRow(label: "Current Buffer Size", value: "\(info.bufferSize) frames")
                            SpecRow(label: "Buffer Size Range", value: "\(info.bufferSizeRange.lowerBound) - \(info.bufferSizeRange.upperBound) frames")
                            SpecRow(label: "Device Latency", value: "\(info.latencyFrames) frames")
                            SpecRow(label: "Safety Offset", value: "\(info.safetyOffset) frames")
                            SpecRow(label: "Total Latency", value: "\(info.totalLatency) frames (\(String(format: "%.2f", Double(info.totalLatency) / info.nominalSampleRate * 1000)) ms at current rate)")
                        }
                    }

                    // --- Format Section ---
                    ExpandableSection(
                        title: "Sample Rate & Formats", 
                        icon: "waveform.path.ecg", 
                        isExpanded: sampleRateExpanded,
                        onToggle: { sampleRateExpanded = $0 }
                    ) {
                        VStack(alignment: .leading, spacing: 8) {
                            SpecRow(label: "Nominal Sample Rate", value: "\(String(format: "%.0f", info.nominalSampleRate)) Hz")
                            
                            VStack(alignment: .leading, spacing: 4) {
                                HStack {
                                    Text("Available Sample Rates:")
                                        .foregroundStyle(.secondary)
                                        .font(.caption)
                                    Spacer()
                                }
                                Text(info.availableSampleRates.map { String(format: "%.0f", $0) }.joined(separator: ", "))
                                    .font(.system(.caption, design: .monospaced))
                                    .textSelection(.enabled)
                            }
                        }
                    }

                    // --- Streams Section ---
                    ExpandableSection(
                        title: "Streams (\(info.streams.count))", 
                        icon: "arrow.left.arrow.right.square", 
                        isExpanded: streamsExpanded,
                        onToggle: { streamsExpanded = $0 }
                    ) {
                        if info.streams.isEmpty {
                            Text("No streams available")
                                .foregroundStyle(.secondary)
                                .font(.caption)
                                .padding()
                        } else {
                            VStack(spacing: 12) {
                                ForEach(info.streams) { stream in
                                    VStack(alignment: .leading, spacing: 6) {
                                        HStack {
                                            Text("Stream #\(stream.id)")
                                                .font(.headline)
                                            Spacer()
                                            Text(stream.direction)
                                                .font(.caption)
                                                .padding(.horizontal, 8)
                                                .padding(.vertical, 2)
                                                .background(stream.direction == "Output" ? .blue.opacity(0.2) : .green.opacity(0.2))
                                                .foregroundColor(stream.direction == "Output" ? .blue : .green)
                                                .cornerRadius(4)
                                        }
                                        
                                        VStack(spacing: 4) {
                                            SpecRow(label: "Active", value: stream.isActive ? "Yes" : "No")
                                            SpecRow(label: "Latency", value: "\(stream.latency) frames")
                                            SpecRow(label: "Virtual Format", value: stream.virtualFormat)
                                            SpecRow(label: "Physical Format", value: stream.physicalFormat)
                                        }
                                    }
                                    .padding()
                                    .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 8))
                                    
                                    if stream.id != info.streams.last?.id {
                                        Divider()
                                    }
                                }
                            }
                        }
                    }
                    
                } else {
                    ContentUnavailableView(
                        "No HAL Info",
                        systemImage: "waveform.path.ecg.badge.plus",
                        description: Text("Core Audio information for the FWA driver will be displayed here.")
                    )
                }
            }
            .padding()
        }
        .onAppear {
            // Fetch info for our driver when the view appears
            uiManager.fetchHALInfo()
        }
        .navigationTitle("Core Audio HAL Status")
    }
}