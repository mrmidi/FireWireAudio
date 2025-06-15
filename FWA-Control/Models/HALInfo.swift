// FWA-Control/Models/HALInfo.swift
import Foundation
import CoreAudio

// A clean data model to store information fetched from the Core Audio HAL.
struct HALInfo: Identifiable, Equatable {
    var id: AudioObjectID // Use the real Object ID as the stable identifier for this struct

    // Basic Device Info
    var deviceName: String
    var deviceUID: String
    var modelName: String

    // Buffer & Latency Info
    var bufferSize: UInt32
    var bufferSizeRange: ClosedRange<UInt32>
    var latencyFrames: UInt32
    var safetyOffset: UInt32
    var totalLatency: UInt32 { latencyFrames + safetyOffset }

    // Sample Rate Info
    var nominalSampleRate: Double
    var availableSampleRates: [Double]

    // Stream Info
    var streams: [HALStreamInfo] = []

    // Nested struct for stream details
    struct HALStreamInfo: Identifiable, Equatable {
        var id: AudioObjectID
        var direction: String
        var isActive: Bool
        var latency: UInt32
        var virtualFormat: String
        var physicalFormat: String
    }
}