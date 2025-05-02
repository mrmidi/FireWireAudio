import SwiftUI

struct AVCTabView: View {
  @EnvironmentObject var uiManager: UIManager
  @State private var guid: UInt64?
  @State private var hexCommand: String = ""
  @State private var responseHex: String = ""
  @State private var errorMessage: String?

  var sortedDevices: [DeviceInfo] {
    uiManager.devices.values.sorted { $0.deviceName < $1.deviceName }
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
          Task {
              await sendAVC()
          }
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

  private func sendAVC() async {
    errorMessage = nil
    responseHex = ""
    guard let guid = guid else { return }

    let bytes = hexCommand
      .split(whereSeparator: \.isWhitespace)
      .compactMap { UInt8($0, radix:16) }

    guard !bytes.isEmpty, bytes.count * 2 >= hexCommand.filter({ !$0.isWhitespace }).count else {
        errorMessage = "Invalid hex format or empty command"
        return
    }
    let commandData = Data(bytes)

    if let resp = await uiManager.sendAVCCommand(guid: guid, command: commandData) {
      responseHex = resp.map { String(format: "%02X", $0) }.joined(separator: " ")
      if responseHex.isEmpty {
          errorMessage = "Command sent successfully, no response data returned."
      }
    } else {
      errorMessage = "No response or send failed (check logs)"
    }
  }
}
