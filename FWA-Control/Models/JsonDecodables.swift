import Foundation

// MARK: - Top-level Device JSON
struct JsonDeviceData: Codable {
    let deviceName: String
    let guid: String
    let modelId: String
    let subunits: JsonSubunitsData?
    let unitPlugs: JsonUnitPlugsData?
    let vendorId: String
    let vendorName: String
}

// MARK: - Subunits
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

// MARK: - Plugs
struct JsonPlugData: Codable {
    let connectionInfo: JsonConnectionInfo?
    let currentFormat: JsonAudioStreamFormat?
    let destConnectionInfo: JsonConnectionInfo? // present in JSON, sometimes null
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

// MARK: - Audio Stream Format
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

// MARK: - Unit Plugs
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

// MARK: - AV/C Info Block (recursive)
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
