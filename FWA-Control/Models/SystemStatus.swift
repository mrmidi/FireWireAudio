// SystemStatus.swift
// Represents the current state of system services

import Foundation
import ServiceManagement

struct SystemStatus {
    let isXpcConnected: Bool
    let isDriverConnected: Bool
    let daemonInstallStatus: SMAppService.Status
    let showDriverInstallPrompt: Bool
    let showDaemonInstallPrompt: Bool
    
    // Default initializer
    init(
        isXpcConnected: Bool = false,
        isDriverConnected: Bool = false,
        daemonInstallStatus: SMAppService.Status = .notFound,
        showDriverInstallPrompt: Bool = false,
        showDaemonInstallPrompt: Bool = false
    ) {
        self.isXpcConnected = isXpcConnected
        self.isDriverConnected = isDriverConnected
        self.daemonInstallStatus = daemonInstallStatus
        self.showDriverInstallPrompt = showDriverInstallPrompt
        self.showDaemonInstallPrompt = showDaemonInstallPrompt
    }
}
