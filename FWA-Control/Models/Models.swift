// === FWA-Control/Models/Models.swift ===
// Consolidated models file replacing multiple scattered model files

import Foundation
import ServiceManagement

// MARK: - System Status

struct SystemStatus {
    let isXpcConnected: Bool
    let isDriverConnected: Bool
    let daemonInstallStatus: SMAppService.Status
    let showDriverInstallPrompt: Bool
    let showDaemonInstallPrompt: Bool
    
    init(
        isXpcConnected: Bool = false,
        isDriverConnected: Bool = false,
        daemonInstallStatus: SMAppService.Status = .notFound,
        showDriverInstallPrompt: Bool = false,
        showDaemonInstallPrompt: Bool = false
    ) {
        self.isXpcConnected = isXpcConnected
        self.isDriverConnected = isDriverConnected
        self.daemonInstallStatus = daemonInstallStatus
        self.showDriverInstallPrompt = showDriverInstallPrompt
        self.showDaemonInstallPrompt = showDaemonInstallPrompt
    }
}

// MARK: - Domain Models (Audio Device Information)

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
        let prefix: String
        if subunitAddress == 0xFF {
            prefix = "Unit"
        } else {
            prefix = SubunitType(rawValue: subunitAddress)?.description ?? "Unknown"
        }
        return "\(prefix) \(direction.description) \(usage.description) #\(plugNumber)"
    }
}

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

// MARK: - Enums

enum PlugDirection: String, Codable, CaseIterable, CustomStringConvertible {
    case input = "Input"
    case output = "Output"
    var description: String { rawValue }
}

enum PlugUsage: String, Codable, CaseIterable, CustomStringConvertible {
    case isochronous = "Isochronous"
    case external = "External"
    case musicSubunit = "MusicSubunit"
    case audioSubunit = "AudioSubunit"
    case unknown
    var description: String { rawValue }
}

enum FormatType: String, Codable, CaseIterable, CustomStringConvertible {
    case compoundAM824 = "CompoundAM824"
    case am824 = "AM824"
    case unknown
    var description: String { rawValue }
}

enum SampleRate: String, Codable, CaseIterable, CustomStringConvertible {
    case sr_44100 = "44.1kHz"
    case sr_48000 = "48kHz"
    case sr_88200 = "88.2kHz"
    case sr_96000 = "96kHz"
    case dontCare = "Don't Care"
    case unknown
    var description: String { rawValue }
}

enum StreamFormatCode: String, Codable, CaseIterable, CustomStringConvertible {
    case mbla = "MBLA"
    case syncStream = "SyncStream"
    case unknown
    var description: String { rawValue }
}

enum SubunitType: UInt8, Codable, CaseIterable, CustomStringConvertible {
    case audio = 0x08
    case music = 0x60
    case unknown = 0xFF
    var description: String {
        switch self {
        case .audio: return "Audio"
        case .music: return "Music"
        case .unknown: return "Unknown"
        }
    }
}

enum InfoBlockType: UInt16, Codable, CaseIterable, CustomStringConvertible {
    case rawText = 0x0a
    case generalMusicStatus = 0x8100
    case routingStatus = 0x8108
    case subunitPlugInfo = 0x8109
    case clusterInfo = 0x810a
    case musicPlugInfo = 0x810b
    case unknown = 0xFFFF
    var description: String {
        switch self {
        case .rawText: return "Raw Text"
        case .generalMusicStatus: return "General Music Status"
        case .routingStatus: return "Routing Status"
        case .subunitPlugInfo: return "Subunit Plug Info"
        case .clusterInfo: return "Cluster Info"
        case .musicPlugInfo: return "Music Plug Info"
        case .unknown: return "Unknown"
        }
    }
}

// MARK: - JSON Decodables (for parsing daemon responses)

struct JsonDeviceData: Codable {
    let deviceName: String
    let guid: String
    let modelId: String
    let subunits: JsonSubunitsData?
    let unitPlugs: JsonUnitPlugsData?
    let vendorId: String
    let vendorName: String
}

struct JsonSubunitsData: Codable {
    let audio: JsonAudioSubunitContainer?
    let music: JsonMusicSubunitContainer?
}

struct JsonAudioSubunitContainer: Codable {
    let destPlugs: [JsonPlugData]?
    let id: Int?
    let numDestPlugs: UInt32?
    let numSourcePlugs: UInt32?
    let sourcePlugs: [JsonPlugData]?
}

struct JsonMusicSubunitContainer: Codable {
    let destPlugs: [JsonPlugData]?
    let id: Int?
    let numDestPlugs: UInt32?
    let numSourcePlugs: UInt32?
    let sourcePlugs: [JsonPlugData]?
    let statusDescriptorParsed: [JsonAVCInfoBlock]?
    let statusDescriptorRaw: String?
}

struct JsonPlugData: Codable {
    let connectionInfo: JsonConnectionInfo?
    let currentFormat: JsonAudioStreamFormat?
    let destConnectionInfo: JsonConnectionInfo?
    let direction: String
    let name: String?
    let plugNumber: UInt8
    let supportedFormats: [JsonAudioStreamFormat]?
    let usage: String
}

struct JsonConnectionInfo: Codable {
    let sourcePlugNum: UInt8
    let sourcePlugStatus: String
    let sourceSubUnit: String
}

struct JsonAudioStreamFormat: Codable {
    let channels: [JsonChannelFormatInfo]?
    let formatType: String
    let sampleRate: String
    let syncSource: Bool
}

struct JsonChannelFormatInfo: Codable {
    let channelCount: UInt8
    let formatCode: String
}

struct JsonUnitPlugsData: Codable {
    let externalInputPlugs: [JsonPlugData]?
    let externalOutputPlugs: [JsonPlugData]?
    let isoInputPlugs: [JsonPlugData]?
    let isoOutputPlugs: [JsonPlugData]?
    let numExternalInput: UInt32?
    let numExternalOutput: UInt32?
    let numIsoInput: UInt32?
    let numIsoOutput: UInt32?
}

struct JsonAVCInfoBlock: Codable {
    let compoundLength: UInt16?
    let nestedBlocks: [JsonAVCInfoBlock]?
    let primaryFieldsLength: UInt16?
    let primaryFieldsParsed: [String: AnyCodable]?
    let primaryFieldsRaw: String?
    let type: String
    let typeName: String?
}

// MARK: - AnyCodable for mixed/unknown types
struct AnyCodable: Codable, Equatable {
    let value: Any
    init<T>(_ value: T?) {
        if let value = value {
            self.value = value
        } else {
            self.value = ()
        }
    }
    init(from decoder: Decoder) throws {
        let container = try decoder.singleValueContainer()
        if container.decodeNil() {
            self.value = ()
        } else if let bool = try? container.decode(Bool.self) {
            self.value = bool
        } else if let int = try? container.decode(Int.self) {
            self.value = int
        } else if let uint = try? container.decode(UInt.self) {
            self.value = uint
        } else if let double = try? container.decode(Double.self) {
            self.value = double
        } else if let string = try? container.decode(String.self) {
            self.value = string
        } else if let array = try? container.decode([AnyCodable].self) {
            self.value = array.map { $0.value }
        } else if let dictionary = try? container.decode([String: AnyCodable].self) {
            self.value = dictionary.mapValues { $0.value }
        } else {
            throw DecodingError.dataCorruptedError(in: container, debugDescription: "AnyCodable value cannot be decoded")
        }
    }
    func encode(to encoder: Encoder) throws {
        var container = encoder.singleValueContainer()
        if value is () {
            try container.encodeNil()
            return
        }
        switch self.value {
            case let bool as Bool:
                try container.encode(bool)
            case let int as Int:
                try container.encode(int)
            case let int as Int8:
                try container.encode(int)
            case let int as Int16:
                try container.encode(int)
            case let int as Int32:
                try container.encode(int)
            case let int as Int64:
                try container.encode(int)
            case let uint as UInt:
                try container.encode(uint)
            case let uint as UInt8:
                try container.encode(uint)
            case let uint as UInt16:
                try container.encode(uint)
            case let uint as UInt32:
                try container.encode(uint)
            case let uint as UInt64:
                try container.encode(uint)
            case let double as Double:
                try container.encode(double)
            case let string as String:
                try container.encode(string)
            case let date as Date:
                try container.encode(date)
            case let url as URL:
                try container.encode(url)
            case let data as Data:
                try container.encode(data)
            case let array as [Any?]:
                try container.encode(array.map { AnyCodable($0) })
            case let dictionary as [String: Any?]:
                try container.encode(dictionary.mapValues { AnyCodable($0) })
            case is Void, is ():
                try container.encodeNil()
            default:
                let context = EncodingError.Context(codingPath: container.codingPath, debugDescription: "AnyCodable value cannot be encoded")
                throw EncodingError.invalidValue(self.value, context)
        }
    }
    static func == (lhs: AnyCodable, rhs: AnyCodable) -> Bool {
        switch (lhs.value, rhs.value) {
        case (let left as Bool, let right as Bool): return left == right
        case (let left as Int, let right as Int): return left == right
        case (let left as UInt, let right as UInt): return left == right
        case (let left as Double, let right as Double): return left == right
        case (let left as String, let right as String): return left == right
        case (is Void, is Void): return true
        case (is (), is ()): return true
        default: return false
        }
    }
}

// MARK: - Extensions

extension SignalInfo: Identifiable {
     public var id: Int { hashValue }
}