// === FWA-Control/InfoBlockDetailView.swift ===
// CORRECTED VERSION
import SwiftUI

struct InfoBlockDetailView: View {
    var infoBlock: AVCInfoBlockInfo // Use AVCInfoBlockInfo from DomainModels
    @State private var expandedNestedBlocks: Bool = false // Keep state for nested blocks

    var body: some View {
        VStack(alignment: .leading, spacing: 5) { // Reduced spacing
            // Basic Info Block Header
            HStack {
                Text("Type:").bold().frame(minWidth: 70, alignment: .leading) // Ensure minimum width
                Text("0x\(String(format: "%04X", infoBlock.type.rawValue))")
                    .font(.system(.body, design: .monospaced))
                Text("(\(infoBlock.typeName))") // Use typeName property
                    .foregroundColor(.secondary)
                Spacer() // Push to left
            }
            HStack {
                Text("Length:").bold().frame(minWidth: 70, alignment: .leading)
                Text("\(infoBlock.compoundLength) bytes")
                Spacer()
                Text("(Fields: \(infoBlock.primaryFieldsLength) bytes)")
                    .foregroundColor(.secondary).font(.caption)
            }

            // Parsed data view (using @ViewBuilder below)
            if infoBlock.parsedData != nil && infoBlock.parsedData != .unknown {
                Divider().padding(.vertical, 2)
                parsedDataView
                    .padding(.leading, 4) // Indent parsed data
            } else if infoBlock.primaryFieldsLength > 0 {
                 // Indicate if fields exist but weren't parsed or applicable
                 Text("Primary fields data not available or not applicable.")
                     .foregroundColor(.orange)
                     .font(.caption)
                     .padding(.leading, 4)
                     .padding(.vertical, 2)
            }

            // Display nested blocks if present
            if !infoBlock.nestedBlocks.isEmpty {
                Divider().padding(.vertical, 2)
                DisclosureGroup(
                    isExpanded: $expandedNestedBlocks,
                    content: {
                        // Indent nested blocks clearly
                        VStack(alignment: .leading, spacing: 6) { // Slightly more spacing for nested blocks
                            ForEach(infoBlock.nestedBlocks) { nestedBlock in
                                InfoBlockDetailView(infoBlock: nestedBlock) // Recursive call
                                if nestedBlock.id != infoBlock.nestedBlocks.last?.id {
                                     Divider().padding(.leading, -4) // Divider between nested blocks, align left
                                }
                            }
                        }.padding(.leading) // Indent entire nested section content
                         .padding(.top, 4) // Add space above nested content
                    },
                    label: {
                        Label("Nested Blocks (\(infoBlock.nestedBlocks.count))", systemImage: "square.stack.3d.down.right")
                            .font(.subheadline)
                    }
                )
            }
        }
        // Removed padding, container view should handle it.
    }

    // MARK: - Parsed Data View Builder

    @ViewBuilder
    private var parsedDataView: some View {
        // Use a switch to handle different parsed data types
        switch infoBlock.parsedData {
        case .rawText(let text):
            InfoBlockFieldView(label: "Text", value: text)

        case .generalMusicStatus(let info):
            InfoBlockFieldView(label: "Transmit Cap", value: info.currentTransmitCapability)
            InfoBlockFieldView(label: "Receive Cap", value: info.currentReceiveCapability)
            InfoBlockFieldView(label: "Latency Cap", value: info.currentLatencyCapability)

        case .routingStatus(let info):
            InfoBlockFieldView(label: "# Src Plugs", value: info.numberOfSubunitSourcePlugs)
            InfoBlockFieldView(label: "# Dest Plugs", value: info.numberOfSubunitDestPlugs)
            InfoBlockFieldView(label: "# Music Plugs", value: info.numberOfMusicPlugs)

        case .subunitPlugInfo(let info):
            InfoBlockFieldView(label: "Subunit Plug ID", value: info.subunitPlugId)
            InfoBlockFieldView(label: "Plug Type", value: info.plugType) // TODO: Map Type to Enum/String?
            InfoBlockFieldView(label: "# Clusters", value: info.numberOfClusters)
            InfoBlockFieldView(label: "Signal Format", value: info.signalFormat, format: .hex) // Example: show as hex
            InfoBlockFieldView(label: "# Channels", value: info.numberOfChannels)

        case .clusterInfo(let info):
            InfoBlockFieldView(label: "Stream Format", value: info.streamFormat) // TODO: Map Format to Enum/String?
            InfoBlockFieldView(label: "Port Type", value: info.portType) // TODO: Map Type to Enum/String?
            InfoBlockFieldView(label: "# Signals", value: info.numberOfSignals)
            if let signals = info.signals, !signals.isEmpty {
                 Text("Signals:").bold().padding(.top, 3)
                 ForEach(signals) { signal in // Requires SignalInfo: Identifiable
                     SignalInfoView(signal: signal).padding(.leading)
                 }
            }

        case .musicPlugInfo(let info):
            InfoBlockFieldView(label: "Music Plug ID", value: info.musicPlugId)
            InfoBlockFieldView(label: "Plug Type", value: info.musicPlugType, format: .hex) // Example: show as hex
            InfoBlockFieldView(label: "Routing Support", value: info.routingSupport) // TODO: Map 0/1 to Bool/String?
            if let source = info.source {
                 Text("Source:").bold().padding(.top, 3)
                 PlugEndpointInfoView(endpoint: source).padding(.leading)
            }
             if let dest = info.destination {
                 Text("Destination:").bold().padding(.top, 3)
                 PlugEndpointInfoView(endpoint: dest).padding(.leading)
             }

        case .unknown, nil:
            EmptyView() // Don't show anything if unknown or nil
        }
    }
}

// MARK: - Helper Views for Parsed Data

/// Generic key-value row used inside `InfoBlockDetailView`.
struct InfoBlockFieldView: View {

    // MARK: - Types

    enum ValueFormat { case decimal, hex }

    // MARK: - Public properties

    var label: String
    var value: (any CustomStringConvertible)?
    var format: ValueFormat = .decimal

    // MARK: - Private helpers

    /// Pre-computes the string shown in the right-hand column.
    private var displayValue: String {
        guard let value else { return "N/A" }

        switch (format, value) {

        // --- signed integers -------------------------------------------------
        case (.hex, let v as Int):     return String(format: "0x%X",  v)
        case (.hex, let v as Int8):    return String(format: "0x%02X", v)
        case (.hex, let v as Int16):   return String(format: "0x%04X", v)
        case (.hex, let v as Int32):   return String(format: "0x%X",   v)
        case (.hex, let v as Int64):   return String(format: "0x%llX", v)

        // --- unsigned integers ----------------------------------------------
        case (.hex, let v as UInt):    return String(format: "0x%X",   v)
        case (.hex, let v as UInt8):   return String(format: "0x%02X", v)
        case (.hex, let v as UInt16):  return String(format: "0x%04X", v)
        case (.hex, let v as UInt32):  return String(format: "0x%X",   v)
        case (.hex, let v as UInt64):  return String(format: "0x%llX", v)

        // --- everything else -------------------------------------------------
        default:                       return "\(value)"
        }
    }

    // Convenience to know whether we’re really showing N/A
    private var isNA: Bool { value == nil }

    // MARK: - View body

    var body: some View {
        HStack(alignment: .top) {
            // left column – field label
            Text("\(label):")
                .bold()
                .frame(width: 110, alignment: .leading)

            // right column – value (with conditional formatting)
            Group {
                if isNA {
                    Text(displayValue)
                        .foregroundColor(.secondary)
                        .italic()
                } else {
                    Text(displayValue)
                }
            }
            .textSelection(.enabled)

            Spacer(minLength: 0)
        }
        .font(.system(size: 11, design: .monospaced))
    }
}

/// View to display SignalInfo details
struct SignalInfoView: View {
    var signal: SignalInfo // Assumes SignalInfo is Identifiable (see below)
    var body: some View {
        // Display signal fields using the helper view
        VStack(alignment: .leading, spacing: 2) {
             InfoBlockFieldView(label: "Music Plug ID", value: signal.musicPlugId)
             InfoBlockFieldView(label: "Stream Loc", value: signal.streamLocation)
             InfoBlockFieldView(label: "Stream Pos", value: signal.streamPosition)
        }
        .padding(.bottom, 2) // Small spacing below each signal
    }
}

/// View to display PlugEndpointInfo details
struct PlugEndpointInfoView: View {
    var endpoint: PlugEndpointInfo
    var body: some View {
        // Display endpoint fields using the helper view
        VStack(alignment: .leading, spacing: 2) {
             InfoBlockFieldView(label: "Func Type", value: endpoint.plugFunctionType, format: .hex)
             InfoBlockFieldView(label: "Func Block ID", value: endpoint.plugFunctionBlockId, format: .hex)
             InfoBlockFieldView(label: "Plug ID", value: endpoint.plugId)
             InfoBlockFieldView(label: "Stream Loc", value: endpoint.streamLocation)
             InfoBlockFieldView(label: "Stream Pos", value: endpoint.streamPosition)
        }
    }
}

/// Make SignalInfo Identifiable for ForEach loop by using Hashable conformance
extension SignalInfo: Identifiable {
     public var id: Int { hashValue }
}