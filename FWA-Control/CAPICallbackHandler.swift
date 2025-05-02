import Foundation
import Logging
import FWA_CAPI

/// Manages the setup and storage of C API callbacks.
class CAPICallbackHandler {
    private weak var engineService: EngineService?
    private let logger = AppLoggers.deviceManager
    private var cLogCallbackClosure: FWALogCallback?
    private var cDeviceCallbackClosure: FWADeviceNotificationCallback?

    init(engineService: EngineService) {
        self.engineService = engineService
        logger.info("CAPICallbackHandler initialized.")
    }

    // --- C Log Callback ---
    func setupCAPILoggingCallback(engine: FWAEngineRef) {
        let context = Unmanaged.passUnretained(self).toOpaque()
        func mapCAPILogLevel(_ cLevel: FWALogLevel) -> Logging.Logger.Level {
            switch cLevel {
            case FWA_LOG_LEVEL_TRACE:    return .trace
            case FWA_LOG_LEVEL_DEBUG:    return .debug
            case FWA_LOG_LEVEL_INFO:     return .info
            case FWA_LOG_LEVEL_WARN:     return .warning
            case FWA_LOG_LEVEL_ERROR:    return .error
            case FWA_LOG_LEVEL_CRITICAL: return .critical
            default:                     return .debug
            }
        }
        let swiftLogCallback: FWALogCallback = { (contextPtr, cLevel, messagePtr) in
            guard let messagePtr = messagePtr else {
                AppLoggers.system.error("Received C log callback with nil message.")
                return
            }
            let message = String(cString: messagePtr)
            let swiftLevel = mapCAPILogLevel(cLevel)
            AppLoggers.deviceManager.log(level: swiftLevel, "[C_API] \(message)")
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
        let context = Unmanaged.passUnretained(self).toOpaque()
        let swiftDeviceCallback: FWADeviceNotificationCallback = { (contextPtr, deviceRef, connected) in
            guard let context = contextPtr else {
                AppLoggers.system.error("Received device callback with null context.")
                return
            }
            let handler = Unmanaged<CAPICallbackHandler>.fromOpaque(context).takeUnretainedValue()
            guard let service = handler.engineService else {
                AppLoggers.system.error("Device callback context handler has nil engineService.")
                return
            }
            var guid: UInt64 = 0
            let guidResult = FWADevice_GetGUID(deviceRef, &guid)
            if guidResult == kIOReturnSuccess && guid != 0 {
                AppLoggers.deviceManager.debug("[C_API_Callback] Device Event: GUID 0x\(String(format: "%llX", guid)), Connected: \(connected)")
                Task {
                    await service.handleDeviceUpdateFromC(guid: guid, added: connected)
                }
            } else {
                AppLoggers.deviceManager.error("[C_API_Callback] Failed to get GUID from deviceRef. Connected: \(connected), Error: \(guidResult)")
            }
        }
        self.cDeviceCallbackClosure = swiftDeviceCallback
        return self.cDeviceCallbackClosure
    }
}
