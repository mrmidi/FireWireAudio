import Foundation
import Logging

/// Handles direct hardware interactions via the C API, called by the XPC layer.
class HardwareInterface {
    private weak var engineService: EngineService?
    private let logger = AppLoggers.deviceManager

    init(engineService: EngineService) {
        self.engineService = engineService
        logger.info("HardwareInterface initialized (delegating to EngineService).")
    }

    func performHardwareSampleRateSet(guid: UInt64, rate: Double) async -> Bool {
        logger.info("--> HW Interface: Delegating Set Sample Rate \(rate) for GUID 0x\(String(format: "%llX", guid)) to EngineService")
        guard let service = engineService else {
            logger.error("Cannot delegate set sample rate, engineService ref is nil.")
            return false
        }
        return await service.setSampleRate(guid: guid, rate: rate)
    }

    func performHardwareClockSourceSet(guid: UInt64, sourceID: UInt32) async -> Bool {
        logger.info("--> HW Interface: Delegating Set Clock Source \(sourceID) for GUID 0x\(String(format: "%llX", guid)) to EngineService")
        guard let service = engineService else {
            logger.error("Cannot delegate set clock source, engineService ref is nil.")
            return false
        }
        return await service.setClockSource(guid: guid, sourceID: sourceID)
    }

    func performHardwareVolumeSet(guid: UInt64, scope: UInt32, element: UInt32, value: Float) async -> Bool {
        logger.info("--> HW Interface: Delegating Set Volume \(value) for GUID 0x\(String(format: "%llX", guid)) to EngineService")
        guard let service = engineService else {
            logger.error("Cannot delegate set volume, engineService ref is nil.")
            return false
        }
        return await service.setVolume(guid: guid, scope: scope, element: element, value: value)
    }

    func performHardwareMuteSet(guid: UInt64, scope: UInt32, element: UInt32, state: Bool) async -> Bool {
        logger.info("--> HW Interface: Delegating Set Mute \(state) for GUID 0x\(String(format: "%llX", guid)) to EngineService")
        guard let service = engineService else {
            logger.error("Cannot delegate set mute, engineService ref is nil.")
            return false
        }
        return await service.setMute(guid: guid, scope: scope, element: element, state: state)
    }

    func performHardwareStartIO(guid: UInt64) async -> Bool {
        logger.info("--> HW Interface: Delegating Start IO for GUID 0x\(String(format: "%llX", guid)) to EngineService")
        guard let service = engineService else {
            logger.error("Cannot delegate start IO, engineService ref is nil.")
            return false
        }
        return await service.startIO(guid: guid)
    }

    func performHardwareStopIO(guid: UInt64) async {
        logger.info("--> HW Interface: Delegating Stop IO for GUID 0x\(String(format: "%llX", guid)) to EngineService")
        guard let service = engineService else {
            logger.error("Cannot delegate stop IO, engineService ref is nil.")
            return
        }
        await service.stopIO(guid: guid)
    }
}
