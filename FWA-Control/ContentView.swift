// ContentView.swift

import SwiftUI
// No need to import FWA_CAPI here; DeviceManager handles it.

struct ContentView: View {
    // The single source of truth for engine state and device data
    @StateObject private var manager = DeviceManager()

    // State for UI selection
    @State private var selectedGuid: UInt64?
    @State private var selectedPlug: AudioPlugInfo?

    // Computed property to get the device list for the picker/list
    // Sorted for consistent display order
    var deviceList: [DeviceInfo] {
        manager.devices.values.sorted { $0.deviceName < $1.deviceName }
    }

    // Computed property for the currently selected device object
    var selectedDevice: DeviceInfo? {
        guard let guid = selectedGuid else { return nil }
        return manager.devices[guid]
    }

    var body: some View {
        NavigationSplitView(columnVisibility: .constant(.all)) {
            // --- Sidebar View ---
            VStack {
                DeviceListSidebar(
                    devices: deviceList,
                    selectedGuid: $selectedGuid,
                    isRunning: manager.isRunning,
                    startAction: manager.start,
                    stopAction: manager.stop,
                    refreshAction: manager.refreshAllDevices
                )
                Divider()
                LogView(logs: manager.logs)
                    .frame(height: 200)
            }
            .navigationSplitViewColumnWidth(min: 250, ideal: 300, max: 400)
        } content: {
            Group {
                if let device = selectedDevice {
                    DeviceDetailView(device: device, selectedPlug: $selectedPlug)
                        .id(device.id)
                } else {
                    Text("Select a device from the list.")
                        .foregroundColor(.secondary)
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
            }
            .navigationSplitViewColumnWidth(min: 400, ideal: 500, max: 800)
        } detail: {
            Group {
                if let plug = selectedPlug {
                    PlugDetailView(plug: plug)
                        .id(plug.id)
                } else {
                    Text("Select a plug to view details.")
                        .foregroundColor(.secondary)
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
            }
            .navigationSplitViewColumnWidth(min: 300, ideal: 350, max: 500)
        }
        .navigationTitle("FWA Control")
    }

    // ... (Rest of ContentView, helper views, preview remain the same) ...

     // Helper function for log color
     func logColor(_ level: SwiftLogLevel) -> Color {
         switch level {
             case .trace, .debug: return .gray
             case .info: return .primary
             case .warn: return .orange
             case .error, .critical: return .red
             case .off: return .clear
         }
     }

     // Helper function for printing cached JSON
     func printCachedJson() {
          print("--- Cached Device Info ---")
          if manager.devices.isEmpty {
              print("No devices cached.")
              return
          }
          let encoder = JSONEncoder()
          encoder.outputFormatting = .prettyPrinted
          for (guid, device) in manager.devices {
              do {
                  let jsonData = try encoder.encode(device)
                  if let jsonString = String(data: jsonData, encoding: .utf8) {
                      print("GUID: 0x\(String(format: "%llX", guid))\n\(jsonString)\n---")
                  }
              } catch {
                  print("Error encoding cached data for GUID 0x\(String(format: "%llX", guid)): \(error)")
              }
          }
          print("-------------------------")
      }

} // End struct ContentView

// MARK: - Sidebar View Structure

struct DeviceListSidebar: View {
    let devices: [DeviceInfo]
    @Binding var selectedGuid: UInt64?
    let isRunning: Bool
    let startAction: () -> Void
    let stopAction: () -> Void
    let refreshAction: () -> Void

    var body: some View {
        VStack {
            // Control Buttons
            HStack {
                Button("Start", systemImage: "play.fill", action: startAction)
                    .disabled(isRunning)
                    .keyboardShortcut("r", modifiers: .command) // Example shortcut
                Button("Stop", systemImage: "stop.fill", action: stopAction)
                    .disabled(!isRunning)
                Button("Refresh All", systemImage: "arrow.clockwise", action: refreshAction)
                    .disabled(!isRunning || devices.isEmpty)
                    .keyboardShortcut("r", modifiers: [.command, .option])
            }
            .padding(.vertical)
            .buttonStyle(.bordered)
            .controlSize(.regular)

            Divider()

            // Device List
            Text("Connected Devices").font(.headline).padding(.top)
            List(devices, selection: $selectedGuid) { device in
                HStack {
                    DeviceStatusIndicatorView(isConnected: device.isConnected) // Show status in list
                    Text(device.deviceName)
                    Spacer()
                    Text("0x\(String(format: "%llX", device.guid))")
                        .font(.caption.monospaced())
                        .foregroundColor(.secondary)
                }
                .tag(device.guid) // Tag list items with their GUID for selection
            }
            .listStyle(.sidebar) // Use sidebar list style

        }
    }
}

// MARK: - Log View Structure

struct LogView: View {
    let logs: [LogEntry]

    var body: some View {
        List {
            ForEach(logs) { entry in
                HStack(alignment: .top, spacing: 5) {
                    Text(entry.timestamp, format: .dateTime.hour().minute().second().secondFraction(.fractional(3)))
                        .font(.system(size: 10, design: .monospaced))
                        .foregroundColor(.secondary)
                        .frame(width: 75, alignment: .leading) // Align timestamps

                    Text("[\(entry.level.description)]")
                        .font(.system(size: 10, design: .monospaced))
                        .foregroundColor(logColor(entry.level))
                        .frame(width: 50, alignment: .leading) // Align levels

                    Text(entry.message)
                        .font(.system(size: 11, design: .monospaced))
                        .lineLimit(nil) // Allow multi-line logs
                        .textSelection(.enabled) // Allow copying log messages
                }
                .listRowInsets(EdgeInsets(top: 2, leading: 5, bottom: 2, trailing: 5)) // Compact rows
            }
        }
        .listStyle(.plain) // Plain style for log list
        .overlay( // Add border for clarity
            RoundedRectangle(cornerRadius: 5)
                .stroke(Color.gray.opacity(0.5), lineWidth: 1)
        )
        .padding([.horizontal, .bottom]) // Padding around the log box
    }

    func logColor(_ level: SwiftLogLevel) -> Color {
        switch level {
            case .trace, .debug: return .gray
            case .info: return .primary
            case .warn: return .orange
            case .error, .critical: return .red
            case .off: return .clear // Should not appear if filtered
        }
    }
}


// MARK: - Device Detail Container View

// This view holds all the sections for a selected device
struct DeviceDetailView: View {
    let device: DeviceInfo
    @Binding var selectedPlug: AudioPlugInfo? // Pass binding down for plug selection

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 15) { // Add more spacing between sections
                DeviceBasicInfoView(guid: device.guid, name: device.deviceName, vendor: device.vendorName)
                Divider()
                UnitInfoSectionView(deviceInfo: device, onSelectPlug: { plug in
                    selectedPlug = plug
                })
                Divider()
                SubunitInfoSectionView(musicSubunit: device.musicSubunit, audioSubunit: device.audioSubunit)

                // Show detailed subunit views conditionally
                if let music = device.musicSubunit {
                    Divider()
                    MusicSubunitDetailsView(subunit: music, onSelectPlug: { plug in
                        selectedPlug = plug
                    })
                }
                if let audio = device.audioSubunit {
                    Divider()
                    AudioSubunitDetailsView(subunit: audio, onSelectPlug: { plug in
                        selectedPlug = plug
                    })
                }
                 Spacer() // Push content up if screen is large
            }
            .padding() // Add padding around the entire detail content
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity) // Allow it to fill space
    }
}


// MARK: - Preview (Optional - uses DummyData)
struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
            // You might need to provide a dummy DeviceManager for previews
            // if the real one causes issues in preview canvas.
    }
}
