// === FWA-Control/Views/DeviceOverviewCard.swift (Updated) ===

import SwiftUI

struct DeviceOverviewCard: View {
    let device: DeviceInfo
    let uiManager: UIManager
    
    var body: some View {
        VStack(spacing: 16) {
            deviceHeader
            Divider()
            statusGrid
            streamControlActions
        }
        .padding()
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 16))
    }
    
    @ViewBuilder
    private var deviceHeader: some View {
        HStack {
            VStack(alignment: .leading, spacing: 4) {
                Text(device.deviceName)
                    .font(.title2)
                    .fontWeight(.semibold)
                
                Text(device.vendorName)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }
            
            Spacer()
            
            VStack(alignment: .trailing, spacing: 4) {
                HStack(spacing: 6) {
                    Image(systemName: device.isConnected ? "checkmark.circle.fill" : "xmark.circle.fill")
                        .foregroundColor(device.isConnected ? .green : .red)
                    Text(device.isConnected ? "Connected" : "Disconnected")
                        .font(.caption)
                        .fontWeight(.medium)
                }
                
                Text("GUID: 0x\(String(format: "%016llX", device.guid))")
                    .font(.caption)
                    .fontDesign(.monospaced)
                    .foregroundStyle(.secondary)
            }
        }
    }
    
    @ViewBuilder
    private var statusGrid: some View {
        LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 12), count: 2), spacing: 12) {
            StatusIndicatorCard(
                title: "Sample Rate",
                value: getCurrentSampleRate(),
                icon: "waveform",
                color: .blue
            )
            
            StatusIndicatorCard(
                title: "Streaming",
                value: getStreamingStatus(),
                icon: getStreamingIcon(),
                color: getStreamingColor()
            )
            
            StatusIndicatorCard(
                title: "Total Plugs",
                value: "\(getTotalPlugCount())",
                icon: "cable.connector",
                color: .purple
            )
            
            StatusIndicatorCard(
                title: "Engine Status",
                value: uiManager.isDaemonConnected ? "Connected" : "Disconnected",
                icon: uiManager.isDaemonConnected ? "bolt.fill" : "bolt.slash.fill",
                color: uiManager.isDaemonConnected ? .green : .orange
            )
        }
    }
    
    @ViewBuilder
    private var streamControlActions: some View {
        VStack(spacing: 12) {
            // Primary stream controls
            HStack(spacing: 12) {
                Button {
                    uiManager.startStreams(guid: device.guid)
                } label: {
                    Label("Start Streams", systemImage: "play.fill")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .disabled(!uiManager.isDaemonConnected)
                .help("Start audio streaming for this device")
                
                Button {
                    uiManager.stopStreams(guid: device.guid)
                } label: {
                    Label("Stop Streams", systemImage: "stop.fill")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .disabled(!uiManager.isDaemonConnected)
                .help("Stop audio streaming for this device")
            }
            
            // Secondary actions
            HStack(spacing: 12) {
                Button {
                    uiManager.refreshDevice(guid: device.guid)
                } label: {
                    Label("Refresh", systemImage: "arrow.clockwise")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .disabled(!uiManager.isDaemonConnected)
                .help("Refresh device information")
                
                Button {
                    // TODO: Add device settings/configuration
                    print("Device settings for \(device.deviceName)")
                } label: {
                    Label("Settings", systemImage: "gearshape")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .disabled(!uiManager.isDaemonConnected)
                .help("Configure device settings")
            }
            
            // Connection status message
            if !uiManager.isDaemonConnected {
                HStack(spacing: 8) {
                    Image(systemName: "info.circle")
                        .foregroundColor(.orange)
                    Text("Engine will start automatically when daemon connects")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                .padding(.top, 4)
            }
        }
    }
    
    // MARK: - Helper Functions
    
    private func getCurrentSampleRate() -> String {
        if let isoInPlug = device.isoInputPlugs.first,
           let currentFormat = isoInPlug.currentStreamFormat {
            return currentFormat.sampleRate.description
        }
        return "Unknown"
    }
    
    private func getStreamingStatus() -> String {
        // TODO: Get actual streaming status from daemon
        if uiManager.isDaemonConnected {
            return "Ready"
        } else {
            return "Unavailable"
        }
    }
    
    private func getStreamingIcon() -> String {
        if uiManager.isDaemonConnected {
            return "play.circle.fill"
        } else {
            return "pause.circle.fill"
        }
    }
    
    private func getStreamingColor() -> Color {
        if uiManager.isDaemonConnected {
            return .green
        } else {
            return .orange
        }
    }
    
    private func getTotalPlugCount() -> Int {
        return Int(device.numIsoInPlugs + device.numIsoOutPlugs + 
                  device.numExtInPlugs + device.numExtOutPlugs)
    }
}