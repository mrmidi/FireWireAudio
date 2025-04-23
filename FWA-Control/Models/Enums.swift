import Foundation

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