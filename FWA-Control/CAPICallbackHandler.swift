import Foundation
import Logging
import FWA_CAPI

/// Manages the setup and storage of C API callbacks.
class CAPICallbackHandler {
    private weak var manager: DeviceManager?
    private let logger = AppLoggers.deviceManager
    private var cLogCallbackClosure: FWALogCallback?
    private var cDeviceCallbackClosure: FWADeviceNotificationCallback?

    init(manager: DeviceManager) {
        self.manager = manager
        logger.info("CAPICallbackHandler initialized.")
    }

    // --- C Log Callback ---
    func setupCAPILoggingCallback(engine: FWAEngineRef) {
        guard let manager = self.manager else {
            logger.error("Cannot setup C log callback, manager is nil.")
            return
        }
        let context = Unmanaged.passUnretained(manager).toOpaque()
        func mapCAPILogLevel(_ cLevel: FWALogLevel) -> Logging.Logger.Level {
            switch cLevel {
            case FWA_LOG_LEVEL_TRACE:    return .trace
            case FWA_LOG_LEVEL_DEBUG:    return .debug
            case FWA_LOG_LEVEL_INFO:     return .info
            case FWA_LOG_LEVEL_WARN:     return .warning
            case FWA_LOG_LEVEL_ERROR:    return .error
            case FWA_LOG_LEVEL_CRITICAL: return .critical
            default:
                Logging.Logger(label: "mapCAPILogLevel").warning("Unknown FWALogLevel encountered: \(cLevel)")
                return .debug
            }
        }
        let swiftLogCallback: FWALogCallback = { (contextPtr, cLevel, messagePtr) in
            guard let context = contextPtr, let messagePtr = messagePtr else {
                Logging.Logger(label: "CAPILogCallback.Error").error("Received C log callback with nil context or message.")
                return
            }
            let mgr = Unmanaged<DeviceManager>.fromOpaque(context).takeUnretainedValue()
            let message = String(cString: messagePtr)
            let swiftLevel = mapCAPILogLevel(cLevel)
            mgr.logger.log(level: swiftLevel, "[C_API] \(message)")
        }
        self.cLogCallbackClosure = swiftLogCallback
        let result = FWAEngine_SetLogCallback(engine, swiftLogCallback, context)
        if result != kIOReturnSuccess {
            logger.error("Failed to set C log callback: IOReturn \(result)")
        } else {
            logger.info("C log callback registered.")
        }
    }

    // --- C Device Callback ---
    func getCDeviceCallback() -> FWADeviceNotificationCallback? {
        guard let manager = self.manager else {
            logger.error("Cannot get C device callback, manager is nil.")
            return nil
        }
        let _ = Unmanaged.passUnretained(manager).toOpaque()
        let swiftDeviceCallback: FWADeviceNotificationCallback = { (contextPtr, deviceRef, connected) in
            guard let context = contextPtr else {
                Logging.Logger(label: "DeviceCallback.Error").error("Received device callback with null context.")
                return
            }
            let mgr = Unmanaged<DeviceManager>.fromOpaque(context).takeUnretainedValue()
            var guid: UInt64 = 0
            let guidResult = FWADevice_GetGUID(deviceRef, &guid)
            if guidResult == kIOReturnSuccess && guid != 0 {
                mgr.logger.debug("[C_API_Callback] Device Event: GUID 0x\(String(format: "%llX", guid)), Connected: \(connected)")
                Task { @MainActor in
                    await mgr.handleDeviceUpdate(guid: guid, added: connected)
                }
            } else {
                mgr.logger.error("[C_API_Callback] Failed to get GUID from deviceRef. Connected: \(connected), Error: \(guidResult)")
            }
        }
        self.cDeviceCallbackClosure = swiftDeviceCallback
        return self.cDeviceCallbackClosure
    }
}
