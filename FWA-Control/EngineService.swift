// EngineService.swift
import Foundation
import Combine
import Logging
// NOTE: FWA_CAPI import removed - now using XPC daemon

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

    // --- XPC Manager for daemon communication ---
    private weak var xpcManager: XPCManager?

    private let logger = AppLoggers.deviceManager

    // --- Combine Integration ---
    var isRunningPublisher: AnyPublisher<Bool, Never> { $isRunning.eraseToAnyPublisher() }
    var devicesPublisher: AnyPublisher<[UInt64: DeviceInfo], Never> { $devices.eraseToAnyPublisher() }

    // Simplified init - no longer failable since we don't create C engine here
    init() {
        logger.info("EngineService init - XPC mode (no direct C API)")
    }

    // Method to set XPCManager reference (called by UIManager or whoever creates both)
    func setXPCManager(_ xpcManager: XPCManager) {
        self.xpcManager = xpcManager
        logger.info("XPCManager reference set in EngineService")
    }

    deinit {
        logger.info("EngineService deinitializing - cleanup handled via shutdown()")
    }

    /// Shutdown method - now primarily handles XPC disconnection
    public func shutdown() async {
        logger.info("EngineService shutdown requested.")

        // Stop the engine and disconnect via XPC in one consolidated call
        if isRunning {
            if let xpcManager = self.xpcManager {
                do {
                    _ = try await xpcManager.disconnectAndStopEngine()
                    logger.info("Engine stopped and XPC disconnected successfully.")
                } catch {
                    logger.error("Error during shutdown: \(error)")
                    // Still perform local cleanup
                    await xpcManager.disconnect()
                }
            }
            // Update local state
            self.isRunning = false
            self.devices.removeAll()
            self.deviceJsons.removeAll()
        } else if let xpcManager = self.xpcManager {
            // Just disconnect if engine wasn't running
            await xpcManager.disconnect()
            logger.info("XPC disconnection completed (engine was not running).")
        }
        
        logger.info("EngineService shutdown completed.")
    }

    func start() async -> Bool {
        logger.info("EngineService: Start requested (via XPC).")
        
        guard let xpcManager = self.xpcManager else {
            logger.error("Engine start failed: No XPCManager available")
            return false
        }
        
        guard !isRunning else { 
            logger.warning("Engine start requested, but already running.")
            return true 
        }
        
        do {
            // Use the new consolidated connection + engine start method
            let success = try await xpcManager.connectAndInitializeDaemonEngine()
            self.isRunning = success
            if success { 
                logger.info("✅ FWA Engine connected and started successfully via XPC daemon.")
            } else { 
                logger.error("❌ Failed to connect and start FWA Engine via XPC daemon.")
            }
            return success
        } catch {
            logger.error("❌ Engine start failed with error: \(error)")
            return false
        }
    }

    func stop() async -> Bool {
        logger.info("EngineService: Stop requested (via XPC).")
        
        guard let xpcManager = self.xpcManager else {
            logger.error("Engine stop failed: No XPCManager available")
            return false
        }
        
        guard isRunning else { 
            logger.warning("Engine stop requested, but not running.")
            return true 
        }
        
        do {
            // Use the new consolidated disconnect + engine stop method
            let success = try await xpcManager.disconnectAndStopEngine()
            self.isRunning = false
            self.devices.removeAll()
            self.deviceJsons.removeAll()
            if success { 
                logger.info("FWA Engine disconnected and stopped successfully via XPC daemon.")
            } else { 
                logger.error("Failed to disconnect and stop FWA Engine via XPC daemon.")
            }
            return success
        } catch {
            logger.error("Engine stop failed with error: \(error)")
            // Still clear our state even if XPC call failed
            self.isRunning = false
            self.devices.removeAll()
            self.deviceJsons.removeAll()
            return false
        }
    }

    func sendCommand(guid: UInt64, command: Data) async -> Data? {
        logger.trace("EngineService sendCommand() for GUID 0x\(String(format: "%llX", guid)) (via XPC)")
        
        guard let xpcManager = self.xpcManager else {
            logger.error("SendCommand failed: No XPCManager available")
            return nil
        }
        
        let responseData = await xpcManager.sendAVCCommand(guid: guid, command: command)
        if responseData == nil {
            logger.error("SendCommand failed or returned no data for GUID 0x\(String(format: "%llX", guid))")
        } else {
            logger.trace("Received response from GUID 0x\(String(format: "%llX", guid))")
        }
        return responseData
    }

    // --- Callback Handling (called by XPCManager) ---
    func handleDeviceUpdate(guid: UInt64, added: Bool) {
        logger.info("EngineService: Handling device update from XPC: GUID 0x\(String(format: "%llX", guid)), Added: \(added)")
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

    // --- Device Info Fetching (via XPC) ---
    func fetchAndAddOrUpdateDevice(guid: UInt64) async {
        logger.debug("EngineService: Fetching info for GUID: 0x\(String(format: "%llX", guid))... (via XPC)")
        
        guard let xpcManager = self.xpcManager else {
            logger.error("Failed to fetch device info: No XPCManager available")
            return
        }
        
        guard let jsonString = await xpcManager.getDetailedDeviceInfoJSON(guid: guid) else {
            logger.error("Failed get JSON string for GUID 0x\(String(format: "%llX", guid)) via XPC.")
            if self.devices.removeValue(forKey: guid) != nil { 
                logger.info("Removed device GUID 0x\(String(format: "%llX", guid)) due to fetch failure.") 
            }
            self.deviceJsons.removeValue(forKey: guid)
            return
        }
        
        self.deviceJsons[guid] = jsonString
        guard let jsonData = jsonString.data(using: .utf8) else {
            logger.error("Failed convert JSON string to Data for GUID 0x\(String(format: "%llX", guid)).")
            if self.devices.removeValue(forKey: guid) != nil { 
                logger.info("Removed device GUID 0x\(String(format: "%llX", guid)) due to data conversion fail.") 
            }
            return
        }
        
        do {
            let jsonDataWrapper = try JSONDecoder().decode(JsonDeviceData.self, from: jsonData)
            if let domainDeviceInfo = DeviceDataMapper.mapJsonToDomainDevice(jsonData: jsonDataWrapper, guidForLog: guid) {
                self.devices[domainDeviceInfo.guid] = domainDeviceInfo
                logger.info("Successfully mapped/updated DeviceInfo for GUID 0x\(String(format: "%llX", domainDeviceInfo.guid)) (\(domainDeviceInfo.deviceName))")
            } else {
                logger.error("Failed map JsonDeviceData to DeviceInfo for GUID 0x\(String(format: "%llX", guid)).")
                if self.devices.removeValue(forKey: guid) != nil { 
                    logger.info("Removed device GUID 0x\(String(format: "%llX", guid)) due to mapping fail.") 
                }
            }
        } catch {
            logger.error("JSON Decode Error GUID 0x\(String(format: "%llX", guid)): \(error)")
            if self.devices.removeValue(forKey: guid) != nil { 
                logger.info("Removed device GUID 0x\(String(format: "%llX", guid)) due to decode fail.") 
            }
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

    // --- Stream Control (via XPC) ---
    func startStreams(guid: UInt64) async -> Bool {
        logger.info("EngineService: Start streams requested for GUID 0x\(String(format: "%llX", guid)) (via XPC).")
        
        guard let xpcManager = self.xpcManager else {
            logger.error("Start streams failed: No XPCManager available")
            return false
        }
        
        let success = await xpcManager.startAudioStreams(guid: guid)
        if success {
            logger.info("✅ Start streams command succeeded for GUID 0x\(String(format: "%llX", guid)).")
        } else {
            logger.error("❌ Start streams command failed for GUID 0x\(String(format: "%llX", guid)).")
        }
        return success
    }

    func stopStreams(guid: UInt64) async -> Bool {
        logger.info("EngineService: Stop streams requested for GUID 0x\(String(format: "%llX", guid)) (via XPC).")
        
        guard let xpcManager = self.xpcManager else {
            logger.error("Stop streams failed: No XPCManager available")
            return false
        }
        
        let success = await xpcManager.stopAudioStreams(guid: guid)
        if success {
            logger.info("✅ Stop streams command succeeded for GUID 0x\(String(format: "%llX", guid)).")
        } else {
            logger.error("❌ Stop streams command failed for GUID 0x\(String(format: "%llX", guid)).")
        }
        return success
    }
} // End Actor EngineService
