// DriverInstallService.swift
import Foundation
import Cocoa // For NSAppleScript
import Logging

// Define potential errors for better handling
// (Keep enum definition here, related to this service)
enum DriverInstallError: Error, LocalizedError {
    case sourceNotFound
    case scriptCreationFailed
    case executionFailed(String, Int) // Message, Error Number
    case cancelled // User cancelled the admin prompt

    var errorDescription: String? {
        switch self {
        case .sourceNotFound:
            return "Could not find the 'FWADriver.driver' file within the application bundle. Please ensure the application is complete."
        case .scriptCreationFailed:
            return "An internal error occurred while preparing the installation script."
        case .executionFailed(let message, let code):
            if code == -128 { // AppleScript user cancelled error
                return "Installation cancelled by user."
            }
            return "Failed to install the FireWire audio driver. Error: \(message) (Code: \(code)). Please check system logs or try again."
        case .cancelled:
            return "Installation cancelled by user."
        }
    }
}

struct DriverInstallerService {
    private let logger = AppLoggers.driverInstaller

    /// Installs the FWADriver from the app bundle into the HAL plugins folder asynchronously.
    /// Sets correct ownership/permissions, and restarts coreaudiod.
    /// Throws a DriverInstallError on failure.
    /// IMPORTANT: The application bundle MUST contain "FWADriver.driver" in its Resources folder.
    func installDriverFromBundle() async throws {
        logger.info("Starting driver installation process...")

        // --- 1. Locate Plugin (Synchronous, Fast) ---
        guard let sourceURL = Bundle.main.url(
            forResource: "FWADriver",
            withExtension: "driver"
        ) else {
            logger.error("❌ Could not find FWADriver.driver in bundle resources.")
            throw DriverInstallError.sourceNotFound
        }
        logger.info("FWADriver.driver found at: \(sourceURL.path)")
        let pluginName = sourceURL.lastPathComponent
        let destPath = "/Library/Audio/Plug-Ins/HAL/\(pluginName)"
        let destDir = "/Library/Audio/Plug-Ins/HAL"

        // --- 2. Build Shell Commands (Synchronous, Fast) ---
        let commands = [
            "mkdir -p '\(destDir)'",
            "rm -rf '\(destPath)'",
            "cp -Rp '\(sourceURL.path)' '\(destPath)'",
            "chown -R root:wheel '\(destPath)'",
            "find '\(destPath)' -type d -exec chmod 755 {} +",
            "find '\(destPath)' -type f -exec chmod 644 {} +",
            "killall -KILL coreaudiod"
        ]
        let escapedCommand = commands.joined(separator: " && ").replacingOccurrences(of: "\"", with: "\\\"")
        logger.info("Shell commands prepared for installation: \(escapedCommand)")

        // --- 3. Prepare AppleScript (Synchronous, Fast) ---
        let appleScriptSource = "do shell script \"\(escapedCommand)\" with administrator privileges"
        guard let script = NSAppleScript(source: appleScriptSource) else {
            logger.error("❌ Failed to create AppleScript object.")
            throw DriverInstallError.scriptCreationFailed
        }

        // --- 4. Execute AppleScript Asynchronously ---
        logger.info("🚀 Starting driver installation script execution (requires admin)...")
        let result: Result<Bool, DriverInstallError> = await Task.detached {
            var errorInfo: NSDictionary?
            script.executeAndReturnError(&errorInfo)

            if let errorDict = errorInfo {
                let errorNum = errorDict[NSAppleScript.errorNumber] as? Int ?? 0
                let errorMsg = errorDict[NSAppleScript.errorMessage] as? String ?? "No details available."
                AppLoggers.driverInstaller.error("❌ Installation script failed: Number=\(errorNum), Message=\(errorMsg)")
                if errorNum == -128 { return .failure(DriverInstallError.cancelled) }
                else { return .failure(DriverInstallError.executionFailed(errorMsg, errorNum)) }
            } else {
                AppLoggers.driverInstaller.info("✅ Installation script execution reported success.")
                return .success(true)
            }
        }.value

        // --- 5. Process Result ---
        switch result {
        case .success:
            logger.info("✅ Driver installation process completed successfully.")
        case .failure(let error):
            logger.error("❌ Driver installation process failed overall.")
            throw error
        }
    }
}
