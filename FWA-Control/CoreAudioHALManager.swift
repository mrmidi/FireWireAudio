// FWA-Control/CoreAudioHALManager.swift
import Foundation
import CoreAudio
import SimplyCoreAudio
import os.log

actor CoreAudioHALManager {
    private let logger = AppLoggers.system

    func fetchInfo(for deviceID: AudioObjectID) -> HALInfo? {
        guard let device = AudioDevice.lookup(by: deviceID) else {
            logger.error("CoreAudioHALManager: Could not find device with ID \(deviceID)")
            return nil
        }
        
        logger.info("CoreAudioHALManager: Fetching HAL info for device '\(device.name)' (ID: \(deviceID))")

        // Fetch buffer size range
        let bufferRange = device.bufferFrameSizeRange(scope: .output) ?? [0, 4096]

        // Fetch available sample rates
        let availableRates = device.nominalSampleRates ?? [device.nominalSampleRate ?? 44100.0]

        // Fetch stream information
        let inputStreams = fetchStreamInfo(for: device.streams(scope: .input) ?? [])
        let outputStreams = fetchStreamInfo(for: device.streams(scope: .output) ?? [])

        // Assemble the main HALInfo struct
        let info = HALInfo(
            id: device.id,
            deviceName: device.name,
            deviceUID: device.uid ?? "N/A",
            modelName: device.manufacturer ?? "N/A",
            bufferSize: device.bufferFrameSize(scope: .output) ?? 0,
            bufferSizeRange: UInt32(bufferRange.first ?? 0)...UInt32(bufferRange.last ?? 4096),
            latencyFrames: (device.latency(scope: .output)) + (device.latency(scope: .input)),
            safetyOffset: (device.safetyOffset(scope: .output) ?? 0) + (device.safetyOffset(scope: .input) ?? 0),
            nominalSampleRate: device.nominalSampleRate ?? 44100.0,
            availableSampleRates: availableRates,
            streams: inputStreams + outputStreams
        )
        
        return info
    }

    private func fetchStreamInfo(for streams: [AudioStream]) -> [HALInfo.HALStreamInfo] {
        return streams.map { stream in
            HALInfo.HALStreamInfo(
                id: stream.id,
                direction: stream.scope == .output ? "Output" : "Input",
                isActive: stream.active,
                latency: stream.latency ?? 0,
                virtualFormat: stream.virtualFormat?.description ?? "N/A",
                physicalFormat: stream.physicalFormat?.description ?? "N/A"
            )
        }
    }
    
    func findDriverDeviceID() -> AudioObjectID? {
        // Look for our FWA driver device by searching for devices with our known characteristics
        let devices = SimplyCoreAudio().allDevices
        
        // First, try to find by UID pattern (if your driver has a consistent UID pattern)
        for device in devices {
            if let uid = device.uid, uid.contains("FWA") || uid.contains("FireWire") {
                logger.info("Found potential FWA device by UID: \(device.name) (\(uid))")
                return device.id
            }
        }
        
        // Fallback: look for devices with "FireWire" in the name
        for device in devices {
            if device.name.contains("FireWire") || device.name.contains("FWA") {
                logger.info("Found potential FWA device by name: \(device.name)")
                return device.id
            }
        }
        
        logger.warning("Could not find FWA driver device")
        return nil
    }
}

// Add a helper description to the format for cleaner display
extension AudioStreamBasicDescription {
    public var description: String {
        let formatID = self.mFormatID.fourCharString
        let flags = self.mFormatFlags
        var formatDesc = "\(self.mChannelsPerFrame)ch, \(Int(self.mSampleRate))Hz, \(formatID)"

        if formatID == "lpcm" {
            let isFloat = (flags & kAudioFormatFlagIsFloat) != 0
            let bitDepth = self.mBitsPerChannel
            formatDesc += " (\(isFloat ? "Float" : "Int")\(bitDepth))"
        }
        return formatDesc
    }
}

extension FourCharCode {
    /// Utility to convert a FourCharCode to a string for debugging.
    var fourCharString: String {
        let bytes: [CChar] = [
            CChar((self >> 24) & 0xFF),
            CChar((self >> 16) & 0xFF),
            CChar((self >> 8) & 0xFF),
            CChar(self & 0xFF),
            0
        ]
        return String(cString: bytes)
    }
}
