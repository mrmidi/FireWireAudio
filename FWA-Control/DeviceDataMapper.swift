// === FWA-Control/DeviceDataMapper.swift ===
import Foundation
import Logging

/// Utility struct responsible for mapping JSON Decodables to Domain Models.
struct DeviceDataMapper {

    // Use a static logger or pass one if needed for detailed mapping logs
    private static let logger = AppLoggers.deviceManager // Or a dedicated mapper logger

    /// Maps the raw decoded JSON data structure to the application's domain model.
    /// Static function as it doesn't rely on instance state.
    static func mapJsonToDomainDevice(jsonData: JsonDeviceData, guidForLog: UInt64) -> DeviceInfo? {
        var deviceInfo = DeviceInfo() // Use Domain Model

        let guidString = jsonData.guid.hasPrefix("0x") ? String(jsonData.guid.dropFirst(2)) : jsonData.guid
        guard let guid = UInt64(guidString, radix: 16), guid != 0 else {
            logger.error("GUID 0x\(String(format: "%llX", guidForLog)): Failed to parse GUID string: '\(jsonData.guid)'. Cannot create DeviceInfo.")
            return nil // Critical failure
        }
        deviceInfo.guid = guid // Use parsed GUID

        deviceInfo.deviceName = jsonData.deviceName
        deviceInfo.vendorName = jsonData.vendorName
        deviceInfo.vendorIdString = jsonData.vendorId
        deviceInfo.modelIdString = jsonData.modelId
        deviceInfo.isConnected = true // Assume connected since we are fetching info

        let unitAddress: UInt8 = 0xFF // Address for top-level unit plugs
        if let jsonUnitPlugs = jsonData.unitPlugs {
            deviceInfo.numIsoInPlugs = jsonUnitPlugs.numIsoInput ?? UInt32(jsonUnitPlugs.isoInputPlugs?.count ?? 0)
            deviceInfo.numIsoOutPlugs = jsonUnitPlugs.numIsoOutput ?? UInt32(jsonUnitPlugs.isoOutputPlugs?.count ?? 0)
            deviceInfo.numExtInPlugs = jsonUnitPlugs.numExternalInput ?? UInt32(jsonUnitPlugs.externalInputPlugs?.count ?? 0)
            deviceInfo.numExtOutPlugs = jsonUnitPlugs.numExternalOutput ?? UInt32(jsonUnitPlugs.externalOutputPlugs?.count ?? 0)

            // Call static mapping methods
            deviceInfo.isoInputPlugs = jsonUnitPlugs.isoInputPlugs?.compactMap { mapJsonToDomainPlug(jsonPlug: $0, defaultSubunitAddress: unitAddress, guid: guid) } ?? []
            deviceInfo.isoOutputPlugs = jsonUnitPlugs.isoOutputPlugs?.compactMap { mapJsonToDomainPlug(jsonPlug: $0, defaultSubunitAddress: unitAddress, guid: guid) } ?? []
            deviceInfo.externalInputPlugs = jsonUnitPlugs.externalInputPlugs?.compactMap { mapJsonToDomainPlug(jsonPlug: $0, defaultSubunitAddress: unitAddress, guid: guid) } ?? []
            deviceInfo.externalOutputPlugs = jsonUnitPlugs.externalOutputPlugs?.compactMap { mapJsonToDomainPlug(jsonPlug: $0, defaultSubunitAddress: unitAddress, guid: guid) } ?? []
        } else {
            logger.warning("GUID 0x\(String(format: "%llX", guid)): No 'unitPlugs' data found in JSON.")
        }

        if let jsonSubunits = jsonData.subunits {
            deviceInfo.audioSubunit = mapJsonToDomainAudioSubunit(jsonContainer: jsonSubunits.audio, guid: guid)
            deviceInfo.musicSubunit = mapJsonToDomainMusicSubunit(jsonContainer: jsonSubunits.music, guid: guid)
        } else {
            logger.warning("GUID 0x\(String(format: "%llX", guid)): No 'subunits' data found in JSON.")
        }

        return deviceInfo
    }

    // Make helper mapping functions static as well
    static private func mapJsonToDomainPlug(jsonPlug: JsonPlugData, defaultSubunitAddress: UInt8, guid: UInt64) -> AudioPlugInfo? {
        guard let direction = PlugDirection(rawValue: jsonPlug.direction) else {
            logger.warning("GUID 0x\(String(format: "%llX", guid)): Failed to map plug direction: '\(jsonPlug.direction)' for plug #\(jsonPlug.plugNumber) @ 0x\(String(format: "%02X", defaultSubunitAddress)). Skipping plug.")
            return nil
        }
        let usage = PlugUsage(rawValue: jsonPlug.usage) ?? .unknown
        if usage == .unknown {
            logger.warning("GUID 0x\(String(format: "%llX", guid)): Unknown plug usage: '\(jsonPlug.usage)' for plug #\(jsonPlug.plugNumber) @ 0x\(String(format: "%02X", defaultSubunitAddress)). Using .unknown.")
        }

        let connectionInfo = mapJsonToDomainConnection(jsonConnInfo: jsonPlug.connectionInfo, guid: guid)
        let currentFormat = mapJsonToDomainStreamFormat(jsonFormat: jsonPlug.currentFormat, guid: guid)
        let supportedFormats = jsonPlug.supportedFormats?.compactMap { mapJsonToDomainStreamFormat(jsonFormat: $0, guid: guid) } ?? []

        var info = AudioPlugInfo(
            subunitAddress: defaultSubunitAddress,
            plugNumber: jsonPlug.plugNumber,
            direction: direction,
            usage: usage,
            plugName: jsonPlug.name,
            connectionInfo: connectionInfo,
            currentStreamFormat: currentFormat,
            supportedStreamFormats: supportedFormats
        )
        if defaultSubunitAddress == 0xFF, info.plugName == nil {
            info.plugName = "Unit \(direction.description) #\(jsonPlug.plugNumber)"
        }
        return info
    }

    static private func mapJsonToDomainConnection(jsonConnInfo: JsonConnectionInfo?, guid: UInt64) -> PlugConnectionInfo? {
         guard let jsonInfo = jsonConnInfo else { return nil }
         var domainInfo = PlugConnectionInfo()
         let subunitString = jsonInfo.sourceSubUnit.hasPrefix("0x") ? String(jsonInfo.sourceSubUnit.dropFirst(2)) : jsonInfo.sourceSubUnit
         domainInfo.sourceSubUnitAddress = UInt8(subunitString, radix: 16) ?? 0xFF
         if domainInfo.sourceSubUnitAddress == 0xFF && !subunitString.isEmpty { logger.warning("GUID 0x\(String(format: "%llX", guid)): Failed to parse sourceSubUnit hex: '\(jsonInfo.sourceSubUnit)'.") }
         let statusString = jsonInfo.sourcePlugStatus.hasPrefix("0x") ? String(jsonInfo.sourcePlugStatus.dropFirst(2)) : jsonInfo.sourcePlugStatus
         domainInfo.sourcePlugStatusValue = UInt8(statusString, radix: 16) ?? 0xFF
         if domainInfo.sourcePlugStatusValue == 0xFF && !statusString.isEmpty { logger.warning("GUID 0x\(String(format: "%llX", guid)): Failed to parse sourcePlugStatus hex: '\(jsonInfo.sourcePlugStatus)'.") }
         domainInfo.sourcePlugNumber = jsonInfo.sourcePlugNum
         domainInfo.isConnected = (domainInfo.sourceSubUnitAddress < 0xEF)
         return domainInfo
    }

    static private func mapJsonToDomainStreamFormat(jsonFormat: JsonAudioStreamFormat?, guid: UInt64) -> AudioStreamFormat? {
         guard let jsonFmt = jsonFormat else { return nil }
         guard let formatType = FormatType(rawValue: jsonFmt.formatType) else { logger.warning("GUID 0x\(String(format: "%llX", guid)): Failed to map format type: '\(jsonFmt.formatType)'. Skipping format."); return nil }
         let sampleRate = SampleRate(rawValue: jsonFmt.sampleRate) ?? .unknown
         if sampleRate == .unknown { logger.warning("GUID 0x\(String(format: "%llX", guid)): Unknown sample rate: '\(jsonFmt.sampleRate)'. Using .unknown.") }
         let channels = jsonFmt.channels?.compactMap { mapJsonToDomainChannelInfo(jsonChan: $0, guid: guid) } ?? []
         return AudioStreamFormat(formatType: formatType, sampleRate: sampleRate, syncSource: jsonFmt.syncSource, channels: channels)
    }

    static private func mapJsonToDomainChannelInfo(jsonChan: JsonChannelFormatInfo?, guid: UInt64) -> ChannelFormatInfo? {
         guard let jsonCh = jsonChan else { return nil }
         guard let formatCode = StreamFormatCode(rawValue: jsonCh.formatCode) else { logger.warning("GUID 0x\(String(format: "%llX", guid)): Failed to map channel format code: '\(jsonCh.formatCode)'. Skipping channel."); return nil }
         return ChannelFormatInfo(channelCount: jsonCh.channelCount, formatCode: formatCode)
    }

    static private func mapJsonToDomainAudioSubunit(jsonContainer: JsonAudioSubunitContainer?, guid: UInt64) -> AudioSubunitInfo? {
         guard let container = jsonContainer else { return nil }
         var audioSubunitInfo = AudioSubunitInfo()
         let audioAddress: UInt8 = SubunitType.audio.rawValue
         audioSubunitInfo.subunitType = .audio
         audioSubunitInfo.audioDestPlugCount = container.numDestPlugs ?? UInt32(container.destPlugs?.count ?? 0)
         audioSubunitInfo.audioSourcePlugCount = container.numSourcePlugs ?? UInt32(container.sourcePlugs?.count ?? 0)
         audioSubunitInfo.audioDestPlugs = container.destPlugs?.compactMap { mapJsonToDomainPlug(jsonPlug: $0, defaultSubunitAddress: audioAddress, guid: guid) } ?? []
         audioSubunitInfo.audioSourcePlugs = container.sourcePlugs?.compactMap { mapJsonToDomainPlug(jsonPlug: $0, defaultSubunitAddress: audioAddress, guid: guid) } ?? []
         return audioSubunitInfo
    }

    static private func mapJsonToDomainMusicSubunit(jsonContainer: JsonMusicSubunitContainer?, guid: UInt64) -> MusicSubunitInfo? {
         guard let container = jsonContainer else { return nil }
         var musicSubunitInfo = MusicSubunitInfo()
         let musicAddress: UInt8 = SubunitType.music.rawValue
         musicSubunitInfo.subunitType = .music
         musicSubunitInfo.musicDestPlugCount = container.numDestPlugs ?? UInt32(container.destPlugs?.count ?? 0)
         musicSubunitInfo.musicSourcePlugCount = container.numSourcePlugs ?? UInt32(container.sourcePlugs?.count ?? 0)
         musicSubunitInfo.musicDestPlugs = container.destPlugs?.compactMap { mapJsonToDomainPlug(jsonPlug: $0, defaultSubunitAddress: musicAddress, guid: guid) } ?? []
         musicSubunitInfo.musicSourcePlugs = container.sourcePlugs?.compactMap { mapJsonToDomainPlug(jsonPlug: $0, defaultSubunitAddress: musicAddress, guid: guid) } ?? []
         musicSubunitInfo.statusDescriptorInfoBlocks = container.statusDescriptorParsed?.compactMap { mapJsonToDomainInfoBlock(jsonBlock: $0, guid: guid) }
         return musicSubunitInfo
    }

    static private func mapJsonToDomainInfoBlock(jsonBlock: JsonAVCInfoBlock?, guid: UInt64) -> AVCInfoBlockInfo? {
         guard let block = jsonBlock else { return nil }
         var domainBlock = AVCInfoBlockInfo()
         let typeString = block.type.hasPrefix("0x") ? String(block.type.dropFirst(2)) : block.type
         if let typeVal = UInt16(typeString, radix: 16), let infoType = InfoBlockType(rawValue: typeVal) {
             domainBlock.type = infoType
         } else {
             logger.warning("GUID 0x\(String(format: "%llX", guid)): Unknown or invalid InfoBlockType string: '\(block.type)'. Using .unknown.")
             domainBlock.type = .unknown
         }
         domainBlock.compoundLength = block.compoundLength ?? 0
         domainBlock.primaryFieldsLength = block.primaryFieldsLength ?? 0
         domainBlock.nestedBlocks = block.nestedBlocks?.compactMap { mapJsonToDomainInfoBlock(jsonBlock: $0, guid: guid) } ?? []
         domainBlock.parsedData = parsePrimaryFields(type: domainBlock.type, parsedDict: block.primaryFieldsParsed, guid: guid)
         return domainBlock
    }

    /// Parses the `primaryFieldsParsed` dictionary based on the `InfoBlockType`.
    static private func parsePrimaryFields(type: InfoBlockType, parsedDict: [String: AnyCodable]?, guid: UInt64) -> AVCInfoBlockInfo.ParsedInfoBlockData? {
        guard let dict = parsedDict else { return .unknown }
        switch type {
        case .rawText:
            if let text = dict["text"]?.value as? String { return .rawText(text: text) }
            else { logger.warning("GUID 0x\(String(format: "%llX", guid)): Missing 'text' field for RawText InfoBlock."); return .unknown }
        case .generalMusicStatus:
            let info = GeneralMusicStatus( currentTransmitCapability: dict["currentTransmitCapability"]?.value as? Int, currentReceiveCapability: dict["currentReceiveCapability"]?.value as? Int, currentLatencyCapability: (dict["currentLatencyCapability"]?.value as? UInt).flatMap(UInt32.init) ?? (dict["currentLatencyCapability"]?.value as? Int).flatMap(UInt32.init))
            return .generalMusicStatus(info: info)
        case .routingStatus:
             let info = RoutingStatus( numberOfSubunitSourcePlugs: dict["numberOfSubunitSourcePlugs"]?.value as? Int, numberOfSubunitDestPlugs: dict["numberOfSubunitDestPlugs"]?.value as? Int, numberOfMusicPlugs: dict["numberOfMusicPlugs"]?.value as? Int)
             return .routingStatus(info: info)
        case .subunitPlugInfo:
            let info = SubunitPlugInfo( subunitPlugId: dict["subunitPlugId"]?.value as? Int, plugType: dict["plugType"]?.value as? Int, numberOfClusters: dict["numberOfClusters"]?.value as? Int, signalFormat: dict["signalFormat"]?.value as? Int, numberOfChannels: dict["numberOfChannels"]?.value as? Int)
            return .subunitPlugInfo(info: info)
        case .clusterInfo:
            var mappedSignals: [SignalInfo]? = nil
            if let signalsAny = dict["signals"]?.value as? [Any] { mappedSignals = signalsAny.compactMap { mapAnyToSignalInfo($0, guid: guid) } }
            else if dict["signals"] != nil { logger.warning("GUID 0x\(String(format: "%llX", guid)): 'signals' field in ClusterInfo is not an array.") }
            let info = ClusterInfo( streamFormat: dict["streamFormat"]?.value as? Int, portType: dict["portType"]?.value as? Int, numberOfSignals: dict["numberOfSignals"]?.value as? Int, signals: mappedSignals)
            return .clusterInfo(info: info)
        case .musicPlugInfo:
            let info = MusicPlugInfo( musicPlugId: dict["musicPlugId"]?.value as? Int, musicPlugType: dict["musicPlugType"]?.value as? Int, routingSupport: dict["routingSupport"]?.value as? Int, source: mapAnyToPlugEndpointInfo(dict["source"]?.value, guid: guid), destination: mapAnyToPlugEndpointInfo(dict["destination"]?.value, guid: guid))
            return .musicPlugInfo(info: info)
        case .unknown:
            return .unknown
        }
    }

    /// Helper to map an `Any` dictionary (from AnyCodable) to `SignalInfo`.
    static private func mapAnyToSignalInfo(_ anyValue: Any, guid: UInt64) -> SignalInfo? {
        guard let dict = anyValue as? [String: Any] else { logger.warning("GUID 0x\(String(format: "%llX", guid)): Expected dictionary for SignalInfo, got \(type(of: anyValue))"); return nil }
        return SignalInfo( musicPlugId: dict["musicPlugId"] as? Int, streamLocation: dict["streamLocation"] as? Int, streamPosition: dict["streamPosition"] as? Int)
    }

    /// Helper to map an `Any` dictionary (from AnyCodable) to `PlugEndpointInfo`.
    static private func mapAnyToPlugEndpointInfo(_ anyValue: Any?, guid: UInt64) -> PlugEndpointInfo? {
        guard let dict = anyValue as? [String: Any] else { if anyValue != nil { logger.warning("GUID 0x\(String(format: "%llX", guid)): Expected dictionary for PlugEndpointInfo, got \(type(of: anyValue!))") }; return nil }
        return PlugEndpointInfo( plugFunctionType: dict["plugFunctionType"] as? Int, plugFunctionBlockId: dict["plugFunctionBlockId"] as? Int, plugId: dict["plugId"] as? Int, streamLocation: dict["streamLocation"] as? Int, streamPosition: dict["streamPosition"] as? Int)
    }
}