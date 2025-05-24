// === FWA-Control/StreamsView.swift ===

import SwiftUI

struct StreamsView: View {
    @EnvironmentObject var uiManager: UIManager
    @State private var selectedGuid: UInt64?

    private var sortedDevices: [DeviceInfo] {
        uiManager.devices.values.sorted { $0.deviceName < $1.deviceName }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            // Device Selector
            HStack {
                Text("Target Device:")
                    .font(.headline)
                Picker("Select Device", selection: $selectedGuid) {
                    Text("— Please Select —").tag(UInt64?.none)
                    ForEach(sortedDevices) { device in
                        Text("\(device.deviceName) (0x\(String(format: "%016llX", device.guid)))")
                            .tag(UInt64?.some(device.guid))
                    }
                }
                .pickerStyle(.menu)
                Spacer()
            }
            .padding(.bottom)

            // Status Info & Controls
            if !uiManager.isRunning {
                Text("Engine is not running. Start the engine from the toolbar to enable stream control.")
                    .foregroundColor(.secondary)
                    .padding()
                    .frame(maxWidth: .infinity, alignment: .center)
            } else if selectedGuid == nil {
                Text("Select a target device above to control its streams.")
                    .foregroundColor(.secondary)
                    .padding()
                    .frame(maxWidth: .infinity, alignment: .center)
            } else {
                // Buttons
                HStack(spacing: 15) {
                    Spacer() // Center buttons
                    Button {
                        if let guid = selectedGuid {
                            uiManager.startStreams(guid: guid)
                        }
                    } label: {
                        Label("Start Streams", systemImage: "play.circle.fill")
                            .font(.title2)
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                    .tint(.green)
                    .help("Start audio streaming for the selected device.")

                    Button {
                        if let guid = selectedGuid {
                             uiManager.stopStreams(guid: guid)
                         }
                    } label: {
                        Label("Stop Streams", systemImage: "stop.circle.fill")
                             .font(.title2)
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                    .tint(.red)
                    .help("Stop audio streaming for the selected device.")
                    Spacer() // Center buttons
                }
                // TODO: Add status indicator here later if stream state becomes available per device
                // Text("Current Stream Status: Unknown")
                //    .foregroundColor(.secondary)
                //    .padding(.top)
            }

            Spacer() // Push controls towards the top
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        .onAppear {
            // Select first device if none is selected initially
            if selectedGuid == nil, let firstGuid = sortedDevices.first?.guid {
                selectedGuid = firstGuid
            }
        }
        .onChange(of: uiManager.devices) { _, newDevices in
            // If the selected device disappears, reset selection
            if let currentGuid = selectedGuid, newDevices[currentGuid] == nil {
                selectedGuid = sortedDevices.first?.guid // Select first available again, or nil
            }
            // If no device was selected, select the first one now available
            if selectedGuid == nil, let firstGuid = sortedDevices.first?.guid {
                 selectedGuid = firstGuid
            }
        }
    }
}

// MARK: - Preview
struct StreamsView_Previews: PreviewProvider {
     static var previews: some View {
         // --- Preview UIManager Setup ---
         let previewUIManager: UIManager = {
             // Use the existing robust preview setup logic
             let engine = EngineService() // No longer failable
             let permManager = PermissionManager()
             let daemonManager = DaemonServiceManager()
             let systemServices = SystemServicesManager(
                 engineService: engine,
                 permissionManager: permManager,
                 daemonServiceManager: daemonManager
             )
             let logStore = LogStore()
             let uiManager = UIManager(
                 engineService: engine,
                 systemServicesManager: systemServices,
                 logStore: logStore
                 )
                 // Add a dummy device for selection
                 let dummyDevice = DeviceInfo(
                     guid: 0xFEEDFACECAFE0001,
                     deviceName: "Stream Preview Device",
                     vendorName: "Preview Systems"
                 )
                 uiManager.devices[dummyDevice.guid] = dummyDevice
                 // Set engine running for preview
                 uiManager.isRunning = true
                 return uiManager
         }()

         return StreamsView()
             .environmentObject(previewUIManager)
             .frame(width: 500, height: 300)
     }
 }
