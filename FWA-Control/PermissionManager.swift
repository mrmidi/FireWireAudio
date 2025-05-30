import Foundation
import AVFoundation
import Combine
import Logging
import SwiftUI

@MainActor
final class PermissionManager: ObservableObject {
    @Published var cameraPermissionStatus: AVAuthorizationStatus = .notDetermined

    private let logger = Logger(label: "net.mrmidi.fwa-control.PermissionManager")

    init() {
        logger.info("PermissionManager initialized.")
        checkCameraPermissionStatus()
    }

    /// Checks the current authorization status without requesting.
    func checkCameraPermissionStatus() {
        let currentStatus = AVCaptureDevice.authorizationStatus(for: .video)
        if currentStatus != self.cameraPermissionStatus {
            logger.info("Camera (FireWire/Thunderbolt) permission status is: \(statusString(currentStatus))")
            self.cameraPermissionStatus = currentStatus
        } else {
            logger.trace("Camera permission status remains: \(statusString(currentStatus))")
        }
    }

    /// FIXED: Swift 6 compliant async permission handling
    func checkAndRequestCameraPermission() async -> Bool {
        // Check current status first
        checkCameraPermissionStatus()

        switch self.cameraPermissionStatus {
        case .notDetermined:
            logger.info("Requesting camera (FireWire/Thunderbolt) permission...")
            
            // FIXED: Use async/await directly without completion handlers
            let granted = await requestCameraPermissionAsync()
            
            // Update status after permission request
            self.cameraPermissionStatus = granted ? .authorized : .denied
            
            if granted {
                logger.info("✅ Camera (FireWire/Thunderbolt) permission granted by user.")
            } else {
                logger.error("❌ Camera (FireWire/Thunderbolt) permission denied by user.")
                showPermissionRequiredAlert()
            }
            
            return granted

        case .restricted, .denied:
            logger.error("Camera (FireWire/Thunderbolt) permission is \(statusString(self.cameraPermissionStatus)).")
            showPermissionRequiredAlert()
            return false

        case .authorized:
            logger.info("Camera (FireWire/Thunderbolt) permission already authorized.")
            return true

        @unknown default:
            logger.warning("Unknown camera permission status: \(self.cameraPermissionStatus)")
            return false
        }
    }

    /// FIXED: Separate async function for permission request
    private func requestCameraPermissionAsync() async -> Bool {
        return await withCheckedContinuation { (continuation: CheckedContinuation<Bool, Never>) in
            // This is safe because we're not capturing self or any MainActor state
            AVCaptureDevice.requestAccess(for: .video) { @Sendable granted in
                // The @Sendable annotation ensures this closure is safe for concurrency
                continuation.resume(returning: granted)
            }
        }
    }

    /// FIXED: Legacy completion-based method for compatibility (if still needed)
    func checkAndRequestCameraPermission(completion: @escaping @Sendable (Bool) -> Void) {
        Task { @MainActor in
            let result = await checkAndRequestCameraPermission()
            completion(result)
        }
    }

    private func showPermissionRequiredAlert() {
        let message = """
        FWA-Control needs permission to access FireWire/Thunderbolt devices (categorized as 'Camera' by macOS) to communicate with your audio interface.

        Please grant access in System Settings > Privacy & Security > Camera.
        """
        showAlert(title: "Permission Required", message: message, style: .critical)
    }

    private func statusString(_ status: AVAuthorizationStatus) -> String {
        switch status {
        case .notDetermined: return "Not Determined"
        case .restricted: return "Restricted"
        case .denied: return "Denied"
        case .authorized: return "Authorized"
        @unknown default: return "Unknown"
        }
    }

    private func showAlert(title: String, message: String, style: NSAlert.Style) {
        let alert = NSAlert()
        alert.messageText = title
        alert.informativeText = message
        alert.alertStyle = style
        alert.addButton(withTitle: "OK")
        alert.runModal()
    }
}