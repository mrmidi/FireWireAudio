// === FWA-Control/Models/DiagnosticsModels.swift ===

import Foundation

// MARK: - Histogram Data Models

struct HistogramData: Sendable, Equatable {
    let timestamp: Date
    let bins: [HistogramBin]
    let overflowCount: UInt64
    let totalSamples: UInt64
    
    init(timestamp: Date = Date(), rawData: [UInt32: UInt64]) {
        self.timestamp = timestamp
        
        // Find overflow count (typically the highest bin number)
        let maxBin = rawData.keys.max() ?? 0
        self.overflowCount = rawData[maxBin] ?? 0
        
        // Convert to bins, excluding overflow
        var bins: [HistogramBin] = []
        var totalSamples: UInt64 = 0
        
        for (binIndex, count) in rawData.sorted(by: { $0.key < $1.key }) {
            if binIndex != maxBin || rawData.count == 1 {
                let bin = HistogramBin(index: binIndex, count: count)
                bins.append(bin)
                totalSamples += count
            }
        }
        
        self.bins = bins
        self.totalSamples = totalSamples + overflowCount
    }
    
    var isEmpty: Bool {
        totalSamples == 0
    }
    
    var averageFillLevel: Double {
        guard totalSamples > 0 else { return 0.0 }
        
        let weightedSum = bins.reduce(0.0) { sum, bin in
            sum + (Double(bin.index) * Double(bin.count))
        }
        
        return weightedSum / Double(totalSamples)
    }
    
    var maxFillLevel: UInt32 {
        bins.max(by: { $0.count < $1.count })?.index ?? 0
    }
}

struct HistogramBin: Sendable, Equatable, Identifiable {
    let id: UInt32
    let index: UInt32
    let count: UInt64
    
    init(index: UInt32, count: UInt64) {
        self.id = index
        self.index = index
        self.count = count
    }
    
    var fillLevelPercentage: Double {
        // Assuming maximum fill level is around 512 (adjust based on your SHM ring size)
        Double(index) / 512.0 * 100.0
    }
}

// MARK: - Diagnostics State

@MainActor
final class DiagnosticsState: ObservableObject, Sendable {
    @Published private(set) var currentHistogram: HistogramData?
    @Published private(set) var historicalData: [HistogramData] = []
    @Published private(set) var isMonitoring: Bool = false
    @Published private(set) var selectedDevice: DeviceInfo?
    @Published private(set) var lastUpdateTime: Date?
    @Published private(set) var errorMessage: String?
    
    private let maxHistoricalEntries = 60 // Keep 60 seconds of data
    
    nonisolated init() {}
    
    func updateHistogram(_ histogram: HistogramData) {
        currentHistogram = histogram
        lastUpdateTime = histogram.timestamp
        errorMessage = nil
        
        // Add to historical data
        historicalData.append(histogram)
        
        // Trim historical data to keep only recent entries
        if historicalData.count > maxHistoricalEntries {
            historicalData.removeFirst(historicalData.count - maxHistoricalEntries)
        }
    }
    
    func setMonitoring(_ monitoring: Bool) {
        isMonitoring = monitoring
        if !monitoring {
            errorMessage = nil
        }
    }
    
    func setSelectedDevice(_ device: DeviceInfo?) {
        selectedDevice = device
        // Clear data when device changes
        currentHistogram = nil
        historicalData.removeAll()
        errorMessage = nil
    }
    
    func setError(_ error: String) {
        errorMessage = error
        lastUpdateTime = Date()
    }
    
    func clearHistoricalData() {
        historicalData.removeAll()
    }
}