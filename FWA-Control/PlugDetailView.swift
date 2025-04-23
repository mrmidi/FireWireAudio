import SwiftUI

struct PlugDetailView: View {
    var plug: AudioPlugInfo   // Domain model

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 8) {

                // ----------------------------------------------------------------
                // HEADER
                // ----------------------------------------------------------------
                Text("Details: \(plug.label)")
                    .font(.headline)
                    .padding(.bottom, 4)

                // ----------------------------------------------------------------
                // BASIC PLUG INFO
                // ----------------------------------------------------------------
                Group {
                    keyValueRow("Subunit Address", value: "0x\(String(format: "%02X", plug.subunitAddress))")
                    keyValueRow("Plug Number"     , value: "\(plug.plugNumber)")
                    keyValueRow("Direction"       , value: plug.direction.description)
                    keyValueRow("Usage"           , value: plug.usage.description)
                }

                // Optional plug name
                if let name = plug.plugName, !name.isEmpty {
                    keyValueRow("Plug Name", value: name)
                }

                // ----------------------------------------------------------------
                // CONNECTION INFO
                // ----------------------------------------------------------------
                if let conn = plug.connectionInfo {
                    Divider()
                    Text("Connection Info")
                        .font(.subheadline)
                        .bold()

                    keyValueRow("Connected", value: conn.isConnected ? "Yes" : "No",
                                valueColor: conn.isConnected ? .green : .secondary)

                    keyValueRow("Source Unit/Subunit",
                                value: "0x\(String(format: "%02X", conn.sourceSubUnitAddress))",
                                monospaced: true)

                    keyValueRow("Source Plug Number",
                                value: "\(conn.sourcePlugNumber)")

                    keyValueRow("Status (raw)",
                                value: "0x\(String(format: "%02X", conn.sourcePlugStatusValue))",
                                monospaced: true)

                    // Human-readable description from `description` computed property
                    keyValueRow("Connection Status", value: conn.description)
                }

                // ----------------------------------------------------------------
                // CURRENT STREAM FORMAT
                // ----------------------------------------------------------------
                if let fmt = plug.currentStreamFormat {
                    Divider()
                    Text("Stream Format Info")
                        .font(.subheadline)
                        .bold()

                    keyValueRow("Format Type",  value: fmt.formatType.description)
                    keyValueRow("Sample Rate", value: fmt.sampleRate.description)
                    keyValueRow("Sync Source", value: fmt.syncSource ? "Yes" : "No")

                    if !fmt.channels.isEmpty {
                        Text("Channel Information:")
                            .bold()
                            .padding(.top, 2)

                        ForEach(fmt.channels) { ch in
                            HStack {
                                Image(systemName: "circle.fill")
                                    .imageScale(.small)
                                Text("\(ch.channelCount) ch")
                                Text(ch.formatCode.description)
                                    .foregroundColor(.secondary)
                            }
                            .padding(.leading)
                            .font(.caption)
                        }
                    }
                }

                // ----------------------------------------------------------------
                // SUPPORTED FORMATS
                // ----------------------------------------------------------------
                if !plug.supportedStreamFormats.isEmpty {
                    Divider()
                    Text("Supported Formats:")
                        .font(.subheadline)
                        .bold()

                    ForEach(plug.supportedStreamFormats) { fmt in
                        HStack {
                            Image(systemName: "checkmark.circle.fill")
                                .foregroundColor(.green)
                                .imageScale(.small)
                            Text(fmt.description)
                        }
                        .padding(.leading)
                        .font(.caption)
                    }
                }
            }
            .padding()
            .frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    // MARK: - Little helper for uniform rows
    @ViewBuilder
    private func keyValueRow(_ key: String,
                             value: String,
                             valueColor: Color = .primary,
                             monospaced: Bool = false) -> some View {
        HStack(alignment: .top) {
            Text("\(key):")
                .bold()
            if monospaced {
                Text(value)
                    .font(.system(.body, design: .monospaced))
                    .foregroundColor(valueColor)
            } else {
                Text(value)
                    .foregroundColor(valueColor)
            }
            Spacer(minLength: 0)
        }
    }
}