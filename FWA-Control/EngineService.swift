// EngineService.swift
import Foundation
import Combine
import Logging
import FWA_CAPI // needed for now

// MARK: - AV/C Command Builder Helper (Example Structure)
struct AVCCommandBuilder {
    static func buildSetFeatureControlCommand(guid: UInt64, subunitType: UInt8, subunitID: UInt8 = 0, feature: UInt8, controlSelector: UInt8, valueBytes: [UInt8]) -> Data? {
        let ctype: UInt8 = 0x00 // CONTROL
        let frameHeader: [UInt8] = [ctype, subunitType | (subunitID & 0x07)]
        let opcodeFeatureControl: UInt8 = 0xB8 // Placeholder opcode
        var payload: [UInt8] = [opcodeFeatureControl]
        payload.append(feature)
        payload.append(controlSelector)
        payload.append(contentsOf: valueBytes)
        logger.trace("Building AV/C Command: Header=\(frameHeader.map { String(format:"%02X", $0)}), Payload=\(payload.map { String(format:"%02X", $0)})")
        return Data(frameHeader + payload)
    }
    static func buildConnectCommand(guid: UInt64) -> Data? {
        logger.warning("AV/C CONNECT command construction not implemented.")
        return nil
    }
    static func buildDisconnectCommand(guid: UInt64) -> Data? {
        logger.warning("AV/C DISCONNECT command construction not implemented.")
        return nil
    }
    static func isResponseAccepted(response: Data?) -> Bool {
        guard let response = response, response.count > 0 else { return false }
        let responseCode = response[0]
        logger.trace("AV/C Response Code: 0x\(String(format:"%02X", responseCode))")
        return responseCode == 0x0C || responseCode == 0x0D
    }
    private static let logger = AppLoggers.deviceManager
}

actor EngineService {
    // --- State ---
    @Published private(set) var isRunning: Bool = false
    @Published private(set) var devices: [UInt64: DeviceInfo] = [:]
    private(set) var deviceJsons: [UInt64: String] = [:]

    // --- Core Engine Components ---
    // --- FIX: Make non-optional let ---
    private let fwaEngineRef: FWAEngineRef
    private var callbackHandler: CAPICallbackHandler! // Also implicitly unwrapped

    private let logger = AppLoggers.deviceManager

    // --- Combine Integration ---
    var isRunningPublisher: AnyPublisher<Bool, Never> { $isRunning.eraseToAnyPublisher() }
    var devicesPublisher: AnyPublisher<[UInt64: DeviceInfo], Never> { $devices.eraseToAnyPublisher() }

    // Keep init failable for engine creation failure
    init?() {
        logger.info("EngineService init starting...")
        let engine = FWAEngine_Create()
        guard let validEngine = engine else {
            logger.critical("Failed to create FWA Engine C API instance! EngineService init failed.")
            return nil
        }
        // --- FIX: Assign non-optional let ---
        self.fwaEngineRef = validEngine // Assign after guard confirms non-nil
        logger.info("FWA C API Engine created successfully (Ref assigned).")
        // --- Defer handler setup ---
        Task {
            await self.setupCallbackHandler()
        }
        logger.info("EngineService init finished (Callback setup deferred).")
    }

    // --- NEW: Async setup method ---
    private func setupCallbackHandler() async {
        logger.info("Setting up callback handler...")
        let handler = CAPICallbackHandler(engineService: self)
        handler.setupCAPILoggingCallback(engine: self.fwaEngineRef)
        self.callbackHandler = handler
        logger.info("C API callbacks configured via CAPICallbackHandler.")
    }
    // --- End NEW ---

    // --- FIX: Remove cleanup from deinit ---
    deinit {
        logger.warning("Deinitializing EngineService instance. Cleanup should happen via shutdown().")
        // DO NOT call C API functions here that access self.fwaEngineRef
    }
    // --- End FIX ---

    // --- FIX: Add explicit shutdown method ---
    /// Call this before releasing the EngineService instance to clean up C resources.
    public func shutdown() async {
        logger.warning("EngineService shutdown requested.")

        // Stop the engine first (runs detached C call)
        if isRunning {
            _ = await stop() // Call the existing stop method
        }

        // Destroy the C engine - This is synchronous C call, safe within actor method
        logger.info("Destroying C API Engine...")
        FWAEngine_Destroy(self.fwaEngineRef) // Access the property safely here
        logger.info("FWA C API Engine destroyed via shutdown().")
        // Note: We don't nil out fwaEngineRef as it's a let constant
    }
    // --- End FIX ---

    func start() async -> Bool {
        let engine = self.fwaEngineRef
        logger.info("EngineService: Start requested.")
        guard let handler = await getCallbackHandler() else {
            logger.error("Engine start failed: Callback handler setup incomplete.")
            return false
        }
        guard let deviceCallback = handler.getCDeviceCallback() else {
            logger.error("Engine start failed: Could not get C device callback pointer.")
            return false
        }
        guard !isRunning else { logger.warning("Engine start requested, but already running."); return true }
        let context = Unmanaged.passUnretained(handler).toOpaque()
        nonisolated(unsafe) let capturedEngine = engine
        let capturedDeviceCallback = deviceCallback
        nonisolated(unsafe) let capturedContext = context
        let success: Bool = await Task.detached { @Sendable in
            return FWAEngine_Start(capturedEngine, capturedDeviceCallback, capturedContext) == kIOReturnSuccess
        }.value
        self.isRunning = success
        if success { logger.info("✅ FWA Engine started successfully.") }
        else { logger.error("❌ Failed to start FWA Engine.") }
        return success
    }

    // Helper to wait for callbackHandler setup if needed
    private func getCallbackHandler() async -> CAPICallbackHandler? {
        while self.callbackHandler == nil {
            await Task.yield()
        }
        return self.callbackHandler
    }

    func stop() async -> Bool {
        let engine = self.fwaEngineRef
        logger.info("EngineService: Stop requested.")
        guard isRunning else { logger.warning("Engine stop requested, but not running."); return true }
        nonisolated(unsafe) let capturedEngine = engine
        let success: Bool = await Task.detached { @Sendable in
            return FWAEngine_Stop(capturedEngine) == kIOReturnSuccess
        }.value
        self.isRunning = false
        self.devices.removeAll()
        self.deviceJsons.removeAll()
        if success { logger.info("FWA Engine stopped successfully.") }
        else { logger.error("Failed to stop FWA Engine.") }
        return success
    }

    func sendCommand(guid: UInt64, command: Data) async -> Data? {
        let engine = self.fwaEngineRef
        logger.trace("EngineService sendCommand() for GUID 0x\(String(format: "%llX", guid))")
        nonisolated(unsafe) let capturedEngine = engine
        let capturedGuid = guid
        let capturedCommand = command
        let responseData: Data? = await Task.detached { @Sendable () -> Data? in
            var responseDataPtr: UnsafeMutablePointer<UInt8>? = nil
            var responseLen: Int = 0
            let cmdResult: IOReturn = capturedCommand.withUnsafeBytes { bufPtr -> IOReturn in
                guard let base = bufPtr.baseAddress else { return kIOReturnBadArgument }
                return FWAEngine_SendCommand(capturedEngine, capturedGuid, base.assumingMemoryBound(to: UInt8.self), capturedCommand.count, &responseDataPtr, &responseLen)
            }
            defer { if let buffer = responseDataPtr { FWADevice_FreeResponseBuffer(buffer) } }
            guard cmdResult == kIOReturnSuccess, let buffer = responseDataPtr, responseLen > 0 else {
                return nil
            }
            return Data(bytes: buffer, count: responseLen)
        }.value
        if responseData == nil {
            logger.error("SendCommand failed or returned no data for GUID 0x\(String(format: "%llX", guid))")
        } else {
            logger.trace("Received response from GUID 0x\(String(format: "%llX", guid))")
        }
        return responseData
    }

    // --- Callback Handling ---
    func handleDeviceUpdateFromC(guid: UInt64, added: Bool) {
        logger.info("EngineService: Handling device update from C: GUID 0x\(String(format: "%llX", guid)), Added: \(added)")
        if added {
            Task { await fetchAndAddOrUpdateDevice(guid: guid) }
        } else {
            let removed = self.devices.removeValue(forKey: guid)
            self.deviceJsons.removeValue(forKey: guid)
            if removed != nil {
                logger.info("Device removed: GUID 0x\(String(format: "%llX", guid)).")
            }
        }
    }

    // --- Device Info Fetching ---
    func fetchAndAddOrUpdateDevice(guid: UInt64) async {
        let engine = self.fwaEngineRef
        logger.debug("EngineService: Fetching info for GUID: 0x\(String(format: "%llX", guid))...")
        nonisolated(unsafe) let capturedEngine = engine
        let capturedGuid = guid
        let jsonStringResult: String? = await Task.detached { @Sendable () -> String? in
            guard let cJsonString = FWAEngine_GetInfoJSON(capturedEngine, capturedGuid) else {
                AppLoggers.deviceManager.error("[BG] FWAEngine_GetInfoJSON NULL for GUID 0x\(String(format: "%llX", capturedGuid)).")
                return nil
            }
            defer { FWADevice_FreeString(cJsonString) }
            return String(cString: cJsonString)
        }.value
        guard let jsonString = jsonStringResult, !jsonString.isEmpty else {
            logger.error("Failed get JSON string for GUID 0x\(String(format: "%llX", guid)).")
            if self.devices.removeValue(forKey: guid) != nil { logger.info("Removed device GUID 0x\(String(format: "%llX", guid)) due to fetch failure.") }
            self.deviceJsons.removeValue(forKey: guid)
            return
        }
        self.deviceJsons[guid] = jsonString
        guard let jsonData = jsonString.data(using: .utf8) else {
            logger.error("Failed convert JSON string to Data for GUID 0x\(String(format: "%llX", guid)).")
            if self.devices.removeValue(forKey: guid) != nil { logger.info("Removed device GUID 0x\(String(format: "%llX", guid)) due to data conversion fail.") }
            return
        }
        do {
            let jsonDataWrapper = try JSONDecoder().decode(JsonDeviceData.self, from: jsonData)
            if let domainDeviceInfo = DeviceDataMapper.mapJsonToDomainDevice(jsonData: jsonDataWrapper, guidForLog: guid) {
                self.devices[domainDeviceInfo.guid] = domainDeviceInfo
                logger.info("Successfully mapped/updated DeviceInfo for GUID 0x\(String(format: "%llX", domainDeviceInfo.guid)) (\(domainDeviceInfo.deviceName))")
            } else {
                logger.error("Failed map JsonDeviceData to DeviceInfo for GUID 0x\(String(format: "%llX", guid)).")
                if self.devices.removeValue(forKey: guid) != nil { logger.info("Removed device GUID 0x\(String(format: "%llX", guid)) due to mapping fail.") }
            }
        } catch {
            logger.error("JSON Decode Error GUID 0x\(String(format: "%llX", guid)): \(error)")
            if self.devices.removeValue(forKey: guid) != nil { logger.info("Removed device GUID 0x\(String(format: "%llX", guid)) due to decode fail.") }
        }
    }

    // --- Refresh All Devices ---
    func refreshAllDevices() async {
        let currentGuids = Array(self.devices.keys)
        logger.info("EngineService: Refreshing all \(currentGuids.count) known devices...")
        if currentGuids.isEmpty {
            logger.info("No devices currently known by EngineService to refresh.")
            return
        }
        for guid in currentGuids {
            await fetchAndAddOrUpdateDevice(guid: guid)
        }
        logger.info("EngineService: Finished refreshing all devices.")
    }

    // --- Hardware Commands (Called by SystemServicesManager) ---
    func setSampleRate(guid: UInt64, rate: Double) async -> Bool {
        logger.info("EngineService: Set Sample Rate \(rate) for GUID 0x\(String(format: "%llX", guid))")
        let rateByte: UInt8
        switch rate {
            case 44100.0: rateByte = 0x01
            case 48000.0: rateByte = 0x02
            case 88200.0: rateByte = 0x03
            case 96000.0: rateByte = 0x04
            default:
                logger.error("Unsupported sample rate: \(rate)")
                return false
        }
        let subunitType: UInt8 = 0x08
        let opcodeSetFormat: UInt8 = 0xBD // Placeholder
        let avcCommandPayload = Data([0x00, subunitType, opcodeSetFormat, rateByte])
        logger.warning("Sample Rate AV/C Command construction is a placeholder!")
        let response = await self.sendCommand(guid: guid, command: avcCommandPayload)
        let success = AVCCommandBuilder.isResponseAccepted(response: response)
        if !success { logger.error("Failed to set sample rate for GUID 0x\(String(format: "%llX", guid)) (Response code not Accepted/Stable/Changed)") }
        else { logger.info("Set Sample Rate command successful for GUID 0x\(String(format: "%llX", guid))") }
        return success
    }

    func startIO(guid: UInt64) async -> Bool {
        logger.info("EngineService: Start IO for GUID 0x\(String(format: "%llX", guid))")
        logger.warning("Start IO implementation is complex and requires specific AV/C commands (Connect, Set Format, Start DCL?) - Placeholder!")
        let avcCommandPayload = Data([0x01, 0xBF, 0x01])
        let response = await self.sendCommand(guid: guid, command: avcCommandPayload)
        let success = AVCCommandBuilder.isResponseAccepted(response: response)
        if success { logger.info("Start IO command sequence initiated (placeholder) for GUID 0x\(String(format: "%llX", guid))") }
        else { logger.error("Start IO command sequence failed (placeholder) for GUID 0x\(String(format: "%llX", guid))") }
        return success
    }

    func stopIO(guid: UInt64) async {
        logger.info("EngineService: Stop IO for GUID 0x\(String(format: "%llX", guid))")
        logger.warning("Stop IO implementation requires specific AV/C commands - Placeholder!")
        let avcCommandPayload = Data([0x01, 0xBF, 0x00])
        let response = await self.sendCommand(guid: guid, command: avcCommandPayload)
        let success = AVCCommandBuilder.isResponseAccepted(response: response)
        if success { logger.info("Stop IO command sequence initiated (placeholder) for GUID 0x\(String(format: "%llX", guid))") }
        else { logger.error("Stop IO command sequence failed (placeholder) for GUID 0x\(String(format: "%llX", guid))") }
    }

    func setVolume(guid: UInt64, scope: UInt32, element: UInt32, value: Float) async -> Bool {
        logger.info("EngineService: Set Volume \(value) for GUID 0x\(String(format: "%llX", guid)) Scope \(scope) Elem \(element)")
        let volumeBytes: [UInt8] = [00, 0x11] // Placeholder
        logger.warning("Volume AV/C value conversion is a placeholder!")
        guard let command = AVCCommandBuilder.buildSetFeatureControlCommand(
            guid: guid,
            subunitType: 0x08,
            subunitID: 0,
            feature: 0x01,
            controlSelector: 0x02,
            valueBytes: volumeBytes
        ) else {
            logger.error("Failed to build Set Volume AV/C command")
            return false
        }
        let response = await self.sendCommand(guid: guid, command: command)
        let success = AVCCommandBuilder.isResponseAccepted(response: response)
        if !success { logger.error("Set Volume failed for GUID 0x\(String(format: "%llX", guid))") }
        return success
    }

    func setMute(guid: UInt64, scope: UInt32, element: UInt32, state: Bool) async -> Bool {
        logger.info("EngineService: Set Mute \(state) for GUID 0x\(String(format: "%llX", guid)) Scope \(scope) Elem \(element)")
        let muteByte: UInt8 = state ? 0x71 : 0x61
        guard let command = AVCCommandBuilder.buildSetFeatureControlCommand(
            guid: guid,
            subunitType: 0x08,
            subunitID: 0,
            feature: 0x02,
            controlSelector: 0x02,
            valueBytes: [muteByte]
        ) else {
            logger.error("Failed to build Set Mute AV/C command")
            return false
        }
        let response = await self.sendCommand(guid: guid, command: command)
        let success = AVCCommandBuilder.isResponseAccepted(response: response)
        if !success { logger.error("Set Mute failed for GUID 0x\(String(format: "%llX", guid))") }
        return success
    }

    func setClockSource(guid: UInt64, sourceID: UInt32) async -> Bool {
        logger.info("EngineService: Set Clock Source \(sourceID) for GUID 0x\(String(format: "%llX", guid))")
        logger.warning("Set Clock Source implementation is a placeholder!")
        let avcCommandPayload = Data([]) // Placeholder
        let response = await self.sendCommand(guid: guid, command: avcCommandPayload)
        let success = AVCCommandBuilder.isResponseAccepted(response: response)
        if !success { logger.error("Set Clock Source failed for GUID 0x\(String(format: "%llX", guid))") }
        return success
    }
} // End Actor EngineService
