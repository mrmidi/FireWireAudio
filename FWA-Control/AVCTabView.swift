import SwiftUI

struct AVCTabView: View {
  @EnvironmentObject var manager: DeviceManager
  @State private var guid: UInt64?
  @State private var hexCommand: String = ""
  @State private var responseHex: String = ""
  @State private var errorMessage: String?

  var sortedDevices: [DeviceInfo] {
    manager.devices.values.sorted { $0.deviceName < $1.deviceName }
  }

  var body: some View {
    Form {
      Section("Device") {
        Picker("Select Device", selection: $guid) {
          Text("—").tag(UInt64?.none)
          ForEach(sortedDevices) { d in
            Text(d.deviceName).tag(Optional(d.guid))
          }
        }
        .pickerStyle(.menu)
      }

      Section("AV/C Command") {
        TextField("Hex-bytes (e.g. 10 20 FF…)", text: $hexCommand)
          .textFieldStyle(.roundedBorder)
        Button("Send") {
          sendAVC()
        }
        .disabled(guid == nil || hexCommand.isEmpty)
      }

      if let err = errorMessage {
        Section("Error") {
          Text(err).foregroundColor(.red)
        }
      }

      if !responseHex.isEmpty {
        Section("Response") {
          ScrollView(.horizontal) {
            Text(responseHex)
              .font(.system(.body, design: .monospaced))
              .padding(4)
              .background(Color.secondary.opacity(0.1))
              .cornerRadius(4)
          }
        }
      }
    }
    .padding()
  }

  private func sendAVC() {
    errorMessage = nil
    responseHex = ""
    guard let guid = guid else { return }

    // Convert hex string → Data
    let tokens = hexCommand.split(whereSeparator: \.isWhitespace)
    for token in tokens {
      guard token.count == 2, token.allSatisfy({ $0.isHexDigit }) else {
        errorMessage = "Invalid hex format"
        return
      }
    }
    let bytes = tokens.compactMap { UInt8($0, radix: 16) }

    if let resp = manager.sendCommand(guid: guid, command: Data(bytes)) {
      responseHex = resp.map { String(format: "%02X", $0) }.joined(separator: " ")
    } else {
      errorMessage = "No response or send failed"
    }
  }
}
