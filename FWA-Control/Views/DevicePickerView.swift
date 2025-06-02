// === FWA-Control/Views/DevicePickerView.swift ===

import SwiftUI

struct DevicePickerView: View {
    let devices: [DeviceInfo]
    @Binding var selectedGuid: UInt64?
    
    var body: some View {
        HStack {
            Label("Device", systemImage: "hifispeaker.2")
                .font(.headline)
                .foregroundStyle(.secondary)
            
            Spacer()
            
            Picker("Select Device", selection: $selectedGuid) {
                if devices.isEmpty {
                    Text("No devices found").tag(UInt64?.none)
                } else {
                    ForEach(devices) { device in
                        HStack {
                            Image(systemName: device.isConnected ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
                                .foregroundColor(device.isConnected ? .green : .orange)
                            Text(device.deviceName)
                        }
                        .tag(Optional(device.guid))
                    }
                }
            }
            .pickerStyle(.menu)
            .frame(minWidth: 200)
            .animation(.easeInOut(duration: 0.2), value: selectedGuid)
        }
        .padding()
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 12))
    }
}