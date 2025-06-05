// === FWA-Control/DiagnosticsView.swift ===

import SwiftUI
import Charts
import Logging

struct DiagnosticsView: View {
    @EnvironmentObject var uiManager: UIManager
    @StateObject private var diagnosticsState = DiagnosticsState()
    
    private let logger = Logger(label: "net.mrmidi.fwa-control.DiagnosticsView")
    private let updateInterval: TimeInterval = 1.0
    
    @State private var monitoringTask: Task<Void, Never>?
    @State private var selectedGuid: UInt64?
    
    private var sortedDevices: [DeviceInfo] {
        uiManager.devices.values.sorted { $0.deviceName < $1.deviceName }
    }
    
    private var selectedDevice: DeviceInfo? {
        guard let guid = selectedGuid else { return nil }
        return uiManager.devices[guid]
    }
    
    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            headerSection
            
            if !uiManager.isDaemonConnected {
                daemonNotConnectedView
            } else if selectedDevice == nil {
                deviceSelectionPromptView
            } else {
                diagnosticsContentView
            }
            
            Spacer()
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        .onAppear {
            setupInitialDevice()
        }
        .onChange(of: selectedGuid) { _, newGuid in
            handleDeviceChange(newGuid)
        }
        .onChange(of: uiManager.devices) { _, _ in
            validateSelectedDevice()
        }
        .onDisappear {
            stopMonitoring()
        }
    }
    
    // MARK: - Header Section
    
    @ViewBuilder
    private var headerSection: some View {
        HStack {
            Text("SHM Fill Level Diagnostics")
                .font(.title2)
                .fontWeight(.semibold)
            
            Spacer()
            
            if diagnosticsState.isMonitoring {
                HStack(spacing: 8) {
                    Circle()
                        .fill(.green)
                        .frame(width: 8, height: 8)
                        .scaleEffect(diagnosticsState.isMonitoring ? 1.2 : 1.0)
                        .animation(.easeInOut(duration: 0.8).repeatForever(autoreverses: true), value: diagnosticsState.isMonitoring)
                    
                    Text("Monitoring")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
        }
        
        devicePickerSection
    }
    
    @ViewBuilder
    private var devicePickerSection: some View {
        HStack {
            Text("Target Device:")
                .font(.headline)
            
            Picker("Select Device", selection: $selectedGuid) {
                Text("— Please Select —").tag(UInt64?.none)
                ForEach(sortedDevices) { device in
                    Text("\(device.deviceName) (0x\(String(format: "%016llX", device.guid)))")
                        .tag(UInt64?.some(device.guid))
                }
            }
            .pickerStyle(.menu)
            
            Spacer()
            
            monitoringControlsSection
        }
        .padding(.bottom)
    }
    
    @ViewBuilder
    private var monitoringControlsSection: some View {
        HStack(spacing: 12) {
            if diagnosticsState.isMonitoring {
                Button("Stop Monitoring") {
                    stopMonitoring()
                }
                .buttonStyle(.bordered)
                .controlSize(.regular)
            } else {
                Button("Start Monitoring") {
                    startMonitoring()
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.regular)
                .disabled(selectedDevice == nil)
            }
            
            Button("Reset Histogram") {
                resetHistogram()
            }
            .buttonStyle(.bordered)
            .controlSize(.regular)
            .disabled(selectedDevice == nil || !uiManager.isDaemonConnected)
            
            Button("Clear Display") {
                diagnosticsState.clearHistoricalData()
            }
            .buttonStyle(.bordered)
            .controlSize(.regular)
            .disabled(diagnosticsState.historicalData.isEmpty)
        }
    }
    
    // MARK: - Content Views
    
    @ViewBuilder
    private var daemonNotConnectedView: some View {
        ContentUnavailableView(
            "Daemon Not Connected",
            systemImage: "bolt.slash",
            description: Text("The FireWire daemon will connect automatically when available. Diagnostics monitoring requires an active daemon connection.")
        )
    }
    
    @ViewBuilder
    private var deviceSelectionPromptView: some View {
        ContentUnavailableView(
            "No Device Selected",
            systemImage: "hifispeaker.2",
            description: Text("Select a target device above to monitor its shared memory fill levels.")
        )
    }
    
    @ViewBuilder
    private var diagnosticsContentView: some View {
        ScrollView {
            VStack(spacing: 24) {
                statusSection
                currentHistogramSection
                if !diagnosticsState.historicalData.isEmpty {
                    historicalTrendsSection
                }
            }
        }
    }
    
    // MARK: - Status Section
    
    @ViewBuilder
    private var statusSection: some View {
        GroupBox("Current Status") {
            VStack(spacing: 12) {
                if let error = diagnosticsState.errorMessage {
                    Label(error, systemImage: "exclamationmark.triangle")
                        .foregroundStyle(.red)
                        .font(.caption)
                } else if let lastUpdate = diagnosticsState.lastUpdateTime {
                    Label("Last updated: \(lastUpdate, style: .time)", systemImage: "clock")
                        .foregroundStyle(.secondary)
                        .font(.caption)
                }
                
                if let histogram = diagnosticsState.currentHistogram {
                    statisticsGrid(for: histogram)
                } else {
                    Text("No data available")
                        .foregroundStyle(.secondary)
                }
            }
        }
    }
    
    @ViewBuilder
    private func statisticsGrid(for histogram: HistogramData) -> some View {
        LazyVGrid(columns: Array(repeating: GridItem(.flexible()), count: 2), spacing: 16) {
            StatisticCard(
                title: "Total Samples",
                value: "\(histogram.totalSamples)",
                icon: "number.circle"
            )
            
            StatisticCard(
                title: "Average Fill Level",
                value: String(format: "%.1f%%", histogram.averageFillLevel / 5.12), // Assuming 512 max
                icon: "chart.bar"
            )
            
            StatisticCard(
                title: "Peak Fill Level",
                value: "\(histogram.maxFillLevel)",
                icon: "arrow.up.circle"
            )
            
            StatisticCard(
                title: "Overflow Count",
                value: "\(histogram.overflowCount)",
                icon: "exclamationmark.triangle",
                isWarning: histogram.overflowCount > 0
            )
        }
    }
    
    // MARK: - Histogram Chart Section
    
    @ViewBuilder
    private var currentHistogramSection: some View {
        GroupBox("Current Fill Level Distribution") {
            if let histogram = diagnosticsState.currentHistogram, !histogram.isEmpty {
                Chart(histogram.bins) { bin in
                    BarMark(
                        x: .value("Fill Level", bin.index),
                        y: .value("Count", bin.count)
                    )
                    .foregroundStyle(colorForFillLevel(bin.index))
                    .opacity(0.8)
                }
                .frame(height: 200)
                .chartXAxis {
                    AxisMarks(position: .bottom) { _ in
                        AxisGridLine()
                        AxisTick()
                        AxisValueLabel()
                    }
                }
                .chartYAxis {
                    AxisMarks(position: .leading) { _ in
                        AxisGridLine()
                        AxisTick()
                        AxisValueLabel()
                    }
                }
                .chartXAxisLabel("Fill Level (Buffer Index)")
                .chartYAxisLabel("Sample Count")
            } else {
                Text("No histogram data available")
                    .foregroundStyle(.secondary)
                    .frame(height: 200)
                    .frame(maxWidth: .infinity)
            }
        }
    }
    
    // MARK: - Historical Trends Section
    
    @ViewBuilder
    private var historicalTrendsSection: some View {
        GroupBox("Historical Trends (Last 60 seconds)") {
            Chart {
                ForEach(Array(diagnosticsState.historicalData.enumerated()), id: \.offset) { index, histogram in
                    LineMark(
                        x: .value("Time", Double(index)),
                        y: .value("Avg Fill", histogram.averageFillLevel)
                    )
                    .foregroundStyle(.blue)
                    .lineStyle(StrokeStyle(lineWidth: 2))
                    
                    AreaMark(
                        x: .value("Time", Double(index)),
                        y: .value("Avg Fill", histogram.averageFillLevel)
                    )
                    .foregroundStyle(.blue.opacity(0.2))
                }
            }
            .frame(height: 150)
            .chartXAxisLabel("Time (seconds ago)")
            .chartYAxisLabel("Average Fill Level")
        }
    }
    
    // MARK: - Helper Methods
    
    private func setupInitialDevice() {
        if selectedGuid == nil, let firstDevice = sortedDevices.first {
            selectedGuid = firstDevice.guid
        }
    }
    
    private func handleDeviceChange(_ newGuid: UInt64?) {
        let newDevice = newGuid.flatMap { uiManager.devices[$0] }
        diagnosticsState.setSelectedDevice(newDevice)
        
        // Restart monitoring if it was active and daemon is connected
        if diagnosticsState.isMonitoring {
            stopMonitoring()
            if newDevice != nil && uiManager.isDaemonConnected {
                startMonitoring()
            }
        }
    }
    
    private func validateSelectedDevice() {
        if let guid = selectedGuid, uiManager.devices[guid] == nil {
            selectedGuid = sortedDevices.first?.guid
        } else if selectedGuid == nil, let firstDevice = sortedDevices.first {
            selectedGuid = firstDevice.guid
        }
    }
    
    // MARK: - Monitoring Control
    
    private func startMonitoring() {
        guard let device = selectedDevice else { return }
        guard uiManager.isDaemonConnected else {
            diagnosticsState.setError("Cannot start monitoring: Daemon not connected")
            return
        }
        
        logger.info("Starting diagnostics monitoring for device: \(device.deviceName)")
        diagnosticsState.setMonitoring(true)
        
        monitoringTask = Task { @MainActor in
            while !Task.isCancelled && diagnosticsState.isMonitoring && uiManager.isDaemonConnected {
                await fetchHistogramData(for: device.guid)
                
                try? await Task.sleep(for: .seconds(updateInterval))
            }
            
            // Stop monitoring if daemon disconnects
            if !uiManager.isDaemonConnected {
                diagnosticsState.setMonitoring(false)
                diagnosticsState.setError("Monitoring stopped: Daemon disconnected")
            }
        }
    }
    
    private func stopMonitoring() {
        logger.info("Stopping diagnostics monitoring")
        diagnosticsState.setMonitoring(false)
        monitoringTask?.cancel()
        monitoringTask = nil
    }
    
    private func fetchHistogramData(for guid: UInt64) async {
        guard let systemServices = uiManager.systemServicesManager else {
            diagnosticsState.setError("System services not available")
            return
        }
        
        let xpcManager = systemServices.xpcManagerAccess
        
        if let rawData = await xpcManager.getSHMFillLevelHistogram(guid: guid) {
            let histogram = HistogramData(rawData: rawData)
            diagnosticsState.updateHistogram(histogram)
        } else {
            diagnosticsState.setError("Failed to fetch histogram data")
        }
    }
    
    private func resetHistogram() {
        guard let device = selectedDevice else { return }
        
        logger.info("Resetting histogram for device: \(device.deviceName)")
        
        Task {
            guard let systemServices = uiManager.systemServicesManager else {
                diagnosticsState.setError("System services not available")
                return
            }
            
            let xpcManager = systemServices.xpcManagerAccess
            let success = await xpcManager.resetSHMFillLevelHistogram(guid: device.guid)
            if success {
                logger.info("Successfully reset histogram")
                diagnosticsState.clearHistoricalData()
            } else {
                diagnosticsState.setError("Failed to reset histogram")
            }
        }
    }
    
    private func colorForFillLevel(_ level: UInt32) -> Color {
        let percentage = Double(level) / 512.0 // Assuming 512 is max
        
        switch percentage {
        case 0..<0.5:
            return .green
        case 0.5..<0.8:
            return .yellow
        case 0.8..<0.95:
            return .orange
        default:
            return .red
        }
    }
}

// MARK: - Supporting Views

struct StatisticCard: View {
    let title: String
    let value: String
    let icon: String
    var isWarning: Bool = false
    
    var body: some View {
        VStack(spacing: 8) {
            HStack {
                Image(systemName: icon)
                    .foregroundStyle(isWarning ? .red : .blue)
                Spacer()
            }
            
            VStack(alignment: .leading, spacing: 2) {
                Text(value)
                    .font(.title2)
                    .fontWeight(.semibold)
                    .foregroundStyle(isWarning ? .red : .primary)
                
                Text(title)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .padding()
        .background(Color(NSColor.quaternaryLabelColor).opacity(0.3))
        .cornerRadius(8)
    }
}
