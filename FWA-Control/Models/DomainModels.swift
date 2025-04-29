import Foundation

// === Combined Domain Models ===

// MARK: - Core Data Structures

/// Represents format info for a set of channels within a stream.
struct ChannelFormatInfo: Equatable, Hashable, Identifiable, Encodable {
    var id = UUID()
    var channelCount: UInt8 = 0
    var formatCode: StreamFormatCode = .unknown
}

/// Represents the format of an audio stream.
struct AudioStreamFormat: Equatable, Hashable, Identifiable, Encodable {
    var id = UUID()
    var formatType: FormatType = .unknown
    var sampleRate: SampleRate = .unknown
    var syncSource: Bool = false
    var channels: [ChannelFormatInfo] = []

    var description: String {
        let channelDesc = channels.map { "\($0.channelCount)ch (\($0.formatCode.description))" }.joined(separator: ", ")
        return "\(formatType.description) @ \(sampleRate.description), Sync: \(syncSource ? "Yes" : "No"), Channels: [\(channelDesc)]"
    }
}

/// Represents connection details for a destination or input plug.
struct PlugConnectionInfo: Equatable, Hashable, Identifiable, Encodable {
    var id = UUID()
    var sourceSubUnitAddress: UInt8 = 0xFF
    var sourcePlugNumber: UInt8 = 0xFF
    var sourcePlugStatusValue: UInt8 = 0xFF
    var isConnected: Bool = false

    var description: String {
        if isConnected {
            let subunitName: String = SubunitType(rawValue: sourceSubUnitAddress)?.description ?? "Unknown"
            return "Connected to \(subunitName) [0x\(String(format: "%02X", sourceSubUnitAddress))], Plug \(sourcePlugNumber)"
        } else {
            return "Not Connected"
        }
    }
}

// MARK: - AV/C Info Blocks

/// An AV/C Info Block data structure.
struct AVCInfoBlockInfo: Equatable, Hashable, Identifiable, Encodable {
    var id = UUID()
    var type: InfoBlockType = .unknown
    var compoundLength: UInt16 = 0
    var primaryFieldsLength: UInt16 = 0
    var parsedData: ParsedInfoBlockData? = nil
    var nestedBlocks: [AVCInfoBlockInfo] = []
    var typeName: String { type.description }

    enum ParsedInfoBlockData: Equatable, Hashable, Encodable {
        case rawText(text: String)
        case generalMusicStatus(info: GeneralMusicStatus)
        case routingStatus(info: RoutingStatus)
        case subunitPlugInfo(info: SubunitPlugInfo)
        case clusterInfo(info: ClusterInfo)
        case musicPlugInfo(info: MusicPlugInfo)
        case unknown
    }
}

// MARK: - Parsed Info Block Data Structures

struct GeneralMusicStatus: Equatable, Hashable, Encodable {
    var currentTransmitCapability: Int?
    var currentReceiveCapability: Int?
    var currentLatencyCapability: UInt32?
}

struct RoutingStatus: Equatable, Hashable, Encodable {
    var numberOfSubunitSourcePlugs: Int?
    var numberOfSubunitDestPlugs: Int?
    var numberOfMusicPlugs: Int?
}

struct SubunitPlugInfo: Equatable, Hashable, Encodable {
    var subunitPlugId: Int?
    var plugType: Int?
    var numberOfClusters: Int?
    var signalFormat: Int?
    var numberOfChannels: Int?
}

struct ClusterInfo: Equatable, Hashable, Encodable {
    var streamFormat: Int?
    var portType: Int?
    var numberOfSignals: Int?
    var signals: [SignalInfo]?
}

struct SignalInfo: Equatable, Hashable, Encodable {
    var musicPlugId: Int?
    var streamLocation: Int?
    var streamPosition: Int?
}

struct MusicPlugInfo: Equatable, Hashable, Encodable {
    var musicPlugId: Int?
    var musicPlugType: Int?
    var routingSupport: Int?
    var source: PlugEndpointInfo?
    var destination: PlugEndpointInfo?
}

struct PlugEndpointInfo: Equatable, Hashable, Encodable {
    var plugFunctionType: Int?
    var plugFunctionBlockId: Int?
    var plugId: Int?
    var streamLocation: Int?
    var streamPosition: Int?
}

// MARK: - Subunit Models

/// Represents a standard audio subunit.
struct AudioSubunitInfo: Equatable, Hashable, Identifiable, Encodable {
    var id = UUID()
    var subunitType: SubunitType = .audio
    var audioDestPlugCount: UInt32 = 0
    var audioSourcePlugCount: UInt32 = 0
    var audioDestPlugs: [AudioPlugInfo] = []
    var audioSourcePlugs: [AudioPlugInfo] = []
    var typeName: String { subunitType.description }
}

/// Represents a music subunit.
struct MusicSubunitInfo: Equatable, Hashable, Identifiable, Encodable {
    var id = UUID()
    var subunitType: SubunitType = .music
    var musicDestPlugCount: UInt32 = 0
    var musicSourcePlugCount: UInt32 = 0
    var musicDestPlugs: [AudioPlugInfo] = []
    var musicSourcePlugs: [AudioPlugInfo] = []
    var statusDescriptorInfoBlocks: [AVCInfoBlockInfo]? = nil
    var typeName: String { subunitType.description }
}

// MARK: - Plug & Device Models

/// Represents a single plug on either unit or subunit level.
struct AudioPlugInfo: Equatable, Hashable, Identifiable, Encodable {
    var id = UUID()
    var subunitAddress: UInt8 = 0xFF
    var plugNumber: UInt8 = 0
    var direction: PlugDirection = .input
    var usage: PlugUsage = .unknown
    var plugName: String? = nil
    var connectionInfo: PlugConnectionInfo? = nil
    var currentStreamFormat: AudioStreamFormat? = nil
    var supportedStreamFormats: [AudioStreamFormat] = []

    var label: String {
        if let name = plugName, !name.isEmpty {
            return name
        }
        // Treat 0xFF as top‐level “Unit” plugs
        let prefix: String
        if subunitAddress == 0xFF {
            prefix = "Unit"
        } else {
            prefix = SubunitType(rawValue: subunitAddress)?.description ?? "Unknown"
        }
        return "\(prefix) \(direction.description) \(usage.description) #\(plugNumber)"
    }
}

/// Top-level device model for UI and logic.
struct DeviceInfo: Equatable, Hashable, Identifiable, Encodable {
    var id = UUID()
    var guid: UInt64 = 0
    var deviceName: String = ""
    var vendorName: String = ""
    var modelIdString: String = ""
    var vendorIdString: String = ""
    var isConnected: Bool = false

    var numIsoInPlugs: UInt32 = 0
    var numIsoOutPlugs: UInt32 = 0
    var numExtInPlugs: UInt32 = 0
    var numExtOutPlugs: UInt32 = 0
    var isoInputPlugs: [AudioPlugInfo] = []
    var isoOutputPlugs: [AudioPlugInfo] = []
    var externalInputPlugs: [AudioPlugInfo] = []
    var externalOutputPlugs: [AudioPlugInfo] = []

    var audioSubunit: AudioSubunitInfo? = nil
    var musicSubunit: MusicSubunitInfo? = nil

    static func == (lhs: DeviceInfo, rhs: DeviceInfo) -> Bool {
        lhs.guid == rhs.guid
    }

    func hash(into hasher: inout Hasher) {
        hasher.combine(guid)
    }
}

// --- placeholders for future detailed InfoBlockData types ---
// struct GeneralMusicStatus { ... }
// struct RoutingStatus { ... }
// struct SubunitPlugInfo { ... }
// struct ClusterInfo { ... }
// struct MusicPlugInfo { ... }
