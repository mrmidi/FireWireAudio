// === FWA-Control/EmptyDeviceStateView.swift (Updated) ===

import SwiftUI

struct EmptyDeviceStateView: View {
    let uiManager: UIManager
    
    var body: some View {
        VStack(spacing: 20) {
            statusIcon
            messageSection
            actionSection
        }
        .padding(40)
        .frame(maxWidth: .infinity)
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 16))
    }
    
    @ViewBuilder
    private var statusIcon: some View {
        ZStack {
            Circle()
                .fill(iconBackgroundColor.opacity(0.2))
                .frame(width: 100, height: 100)
            
            Image(systemName: iconName)
                .font(.system(size: 40, weight: .medium))
                .foregroundStyle(iconColor)
        }
    }
    
    @ViewBuilder
    private var messageSection: some View {
        VStack(spacing: 8) {
            Text(primaryMessage)
                .font(.title2)
                .fontWeight(.semibold)
                .multilineTextAlignment(.center)
            
            Text(secondaryMessage)
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
        }
    }
    
    @ViewBuilder
    private var actionSection: some View {
        VStack(spacing: 12) {
            if showSystemStatusInfo {
                systemStatusInfo
            }
            
            if shouldShowActionButton {
                primaryActionButton
            }
            
            if showHelpText {
                helpText
            }
        }
    }
    
    @ViewBuilder
    private var systemStatusInfo: some View {
        VStack(spacing: 8) {
            HStack(spacing: 12) {
                StatusChip(
                    title: "Daemon",
                    isConnected: uiManager.isDaemonConnected,
                    icon: "bolt.fill"
                )
                
                StatusChip(
                    title: "Driver",
                    isConnected: uiManager.isDriverConnected,
                    icon: "puzzlepiece.extension.fill"
                )
            }
            
            if uiManager.isDaemonConnected && !uiManager.isDriverConnected {
                Text("Driver installation may be required")
                    .font(.caption)
                    .foregroundStyle(.orange)
            }
        }
    }
    
    @ViewBuilder
    private var primaryActionButton: some View {
        switch currentState {
        case .daemonDisconnected:
            Button {
                // Daemon connection is automatic, but we can trigger a check
                print("Check daemon status")
            } label: {
                Label("Check Daemon Status", systemImage: "arrow.clockwise")
            }
            .buttonStyle(.borderedProminent)
            .help("Check if the daemon is running")
            
        case .waitingForDriver:
            Button {
                uiManager.installDriver()
            } label: {
                Label("Install Driver", systemImage: "plus.circle")
            }
            .buttonStyle(.borderedProminent)
            .help("Install the FireWire audio driver")
            
        case .readyWaitingForDevices, .noDevicesSelected:
            EmptyView() // No action needed for these states
        }
    }
    
    @ViewBuilder
    private var helpText: some View {
        Text(helpMessage)
            .font(.caption)
            .foregroundStyle(.tertiary)
            .multilineTextAlignment(.center)
    }
    
    // MARK: - State Logic
    
    private enum EmptyState {
        case daemonDisconnected
        case waitingForDriver
        case readyWaitingForDevices
        case noDevicesSelected
    }
    
    private var currentState: EmptyState {
        if !uiManager.isDaemonConnected {
            return .daemonDisconnected
        } else if !uiManager.isDriverConnected {
            return .waitingForDriver
        } else if uiManager.devices.isEmpty {
            return .readyWaitingForDevices
        } else {
            return .noDevicesSelected
        }
    }
    
    // MARK: - Computed Properties
    
    private var iconName: String {
        switch currentState {
        case .daemonDisconnected:
            return "bolt.slash"
        case .waitingForDriver:
            return "puzzlepiece.extension"
        case .readyWaitingForDevices:
            return "magnifyingglass"
        case .noDevicesSelected:
            return "hifispeaker.2"
        }
    }
    
    private var iconColor: Color {
        switch currentState {
        case .daemonDisconnected:
            return .red
        case .waitingForDriver:
            return .orange
        case .readyWaitingForDevices:
            return .blue
        case .noDevicesSelected:
            return .secondary
        }
    }
    
    private var iconBackgroundColor: Color {
        iconColor
    }
    
    private var primaryMessage: String {
        switch currentState {
        case .daemonDisconnected:
            return "Daemon Disconnected"
        case .waitingForDriver:
            return "Driver Required"
        case .readyWaitingForDevices:
            return "Searching for Devices"
        case .noDevicesSelected:
            return "No Device Selected"
        }
    }
    
    private var secondaryMessage: String {
        switch currentState {
        case .daemonDisconnected:
            return "The FireWire Audio Daemon is not running. The engine will start automatically when the daemon connects."
        case .waitingForDriver:
            return "The FireWire Audio Driver needs to be installed to communicate with audio devices."
        case .readyWaitingForDevices:
            return "Engine is running and searching for FireWire audio devices. Connect a device to see detailed information."
        case .noDevicesSelected:
            return "Engine is ready. Select a device from the picker above to view its configuration."
        }
    }
    
    private var shouldShowActionButton: Bool {
        switch currentState {
        case .daemonDisconnected, .waitingForDriver:
            return true
        case .readyWaitingForDevices, .noDevicesSelected:
            return false
        }
    }
    
    private var showSystemStatusInfo: Bool {
        switch currentState {
        case .daemonDisconnected, .waitingForDriver:
            return true
        case .readyWaitingForDevices, .noDevicesSelected:
            return false
        }
    }
    
    private var showHelpText: Bool {
        currentState == .readyWaitingForDevices || currentState == .daemonDisconnected
    }
    
    private var helpMessage: String {
        switch currentState {
        case .daemonDisconnected:
            return "The daemon will be started automatically when needed. No manual intervention required."
        case .waitingForDriver:
            return ""
        case .readyWaitingForDevices:
            return "Make sure your FireWire device is connected and powered on."
        case .noDevicesSelected:
            return ""
        }
    }
}

// MARK: - StatusChip

private struct StatusChip: View {
    let title: String
    let isConnected: Bool
    let icon: String
    
    var body: some View {
        HStack(spacing: 6) {
            Image(systemName: icon)
                .foregroundColor(isConnected ? .green : .red)
                .font(.caption)
            
            Text(title)
                .font(.caption)
                .fontWeight(.medium)
            
            Text(isConnected ? "✓" : "✗")
                .font(.caption2)
                .fontWeight(.bold)
                .foregroundColor(isConnected ? .green : .red)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
        .background(Color(NSColor.quaternaryLabelColor).opacity(0.3), in: Capsule())
    }
}
