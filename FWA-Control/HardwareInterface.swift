import Foundation
import Logging
import FWA_CAPI

/// Handles direct hardware interactions via the C API, called by the XPC layer.
@MainActor
class HardwareInterface {
    private weak var manager: DeviceManager?
    private let logger = AppLoggers.deviceManager

    init(manager: DeviceManager) {
        self.manager = manager
        logger.info("HardwareInterface initialized.")
    }

    func performHardwareSampleRateSet(guid: UInt64, rate: Double) async -> Bool {
        guard let _engine = manager?.fwaEngineRef else {
            logger.error("Cannot set sample rate, engine ref is nil.")
            return false
        }
        logger.info("--> HW Interface: Set Sample Rate \(rate) for GUID 0x\(String(format: "%llX", guid))")
        do {
            try await Task.sleep(nanoseconds: 100_000_000)
            logger.warning("--> HW Interface: Set Sample Rate - STUBBED SUCCESS")
            return true
        } catch {
            logger.error("Task.sleep failed during sample rate set: \(error.localizedDescription)")
            return false
        }
    }

    func performHardwareClockSourceSet(guid: UInt64, sourceID: UInt32) async -> Bool {
        guard let _engine = manager?.fwaEngineRef else {
            logger.error("Cannot set clock source, engine ref is nil.")
            return false
        }
        logger.info("--> HW Interface: Set Clock Source \(sourceID) for GUID 0x\(String(format: "%llX", guid))")
        do {
            try await Task.sleep(nanoseconds: 50_000_000)
            logger.warning("--> HW Interface: Set Clock Source - STUBBED SUCCESS")
            return true
        } catch {
            logger.error("Task.sleep failed during clock source set: \(error.localizedDescription)")
            return false
        }
    }

    func performHardwareVolumeSet(guid: UInt64, scope: UInt32, element: UInt32, value: Float) async -> Bool {
        guard let _engine = manager?.fwaEngineRef else {
            logger.error("Cannot set volume, engine ref is nil.")
            return false
        }
        logger.info("--> HW Interface: Set Volume \(value) for GUID 0x\(String(format: "%llX", guid)), Scope \(scope), Elem \(element)")
        do {
            try await Task.sleep(nanoseconds: 30_000_000)
            logger.warning("--> HW Interface: Set Volume - STUBBED SUCCESS")
            return true
        } catch {
            logger.error("Task.sleep failed during volume set: \(error.localizedDescription)")
            return false
        }
    }

    func performHardwareMuteSet(guid: UInt64, scope: UInt32, element: UInt32, state: Bool) async -> Bool {
        guard let _engine = manager?.fwaEngineRef else {
            logger.error("Cannot set mute, engine ref is nil.")
            return false
        }
        logger.info("--> HW Interface: Set Mute \(state) for GUID 0x\(String(format: "%llX", guid)), Scope \(scope), Elem \(element)")
        do {
            try await Task.sleep(nanoseconds: 30_000_000)
            logger.warning("--> HW Interface: Set Mute - STUBBED SUCCESS")
            return true
        } catch {
            logger.error("Task.sleep failed during mute set: \(error.localizedDescription)")
            return false
        }
    }

    func performHardwareStartIO(guid: UInt64) async -> Bool {
        guard let _engine = manager?.fwaEngineRef else {
            logger.error("Cannot start IO, engine ref is nil.")
            return false
        }
        logger.info("--> HW Interface: Start IO for GUID 0x\(String(format: "%llX", guid))")
        do {
            try await Task.sleep(nanoseconds: 150_000_000)
            logger.warning("--> HW Interface: Start IO - STUBBED SUCCESS")
            await manager?.updateDeviceIOState(guid: guid, isRunning: true)
            return true
        } catch {
            logger.error("Task.sleep failed during start IO: \(error.localizedDescription)")
            return false
        }
    }

    func performHardwareStopIO(guid: UInt64) async {
        guard let _engine = manager?.fwaEngineRef else {
            logger.error("Cannot stop IO, engine ref is nil.")
            return
        }
        logger.info("--> HW Interface: Stop IO for GUID 0x\(String(format: "%llX", guid))")
        do {
            try await Task.sleep(nanoseconds: 50_000_000)
            logger.warning("--> HW Interface: Stop IO - STUBBED")
            await manager?.updateDeviceIOState(guid: guid, isRunning: false)
        } catch {
            logger.error("Task.sleep failed during stop IO: \(error.localizedDescription)")
        }
    }
}
