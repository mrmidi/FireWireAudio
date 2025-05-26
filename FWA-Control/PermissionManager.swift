import Foundation
import AVFoundation
import Combine // For PassthroughSubject if publishing status directly
import Logging
import SwiftUI // For NSAlert

@MainActor // Ensure methods interacting with AVFoundation or UI are on main actor
class PermissionManager: ObservableObject { // Make Observable if views need to react directly

    @Published var cameraPermissionStatus: AVAuthorizationStatus = .notDetermined

    private let logger = Logger(label: "net.mrmidi.fwa-control.PermissionManager") // Specific logger

    init() {
        logger.info("PermissionManager initialized.")
        // Get initial status on init
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

    /// Checks the current status and requests permission if it's not determined.
    /// Calls the completion handler with the result (true if authorized, false otherwise).
    func checkAndRequestCameraPermission(completion: ((Bool) -> Void)? = nil) {
        // This method itself is already on the MainActor due to the class annotation.
        checkCameraPermissionStatus() // Update status first

        switch self.cameraPermissionStatus {
        case .notDetermined:
            logger.info("Requesting camera (FireWire/Thunderbolt) permission...")
            AVCaptureDevice.requestAccess(for: .video) { [weak self] granted_from_avkit in
                // This completion handler is called on a background thread by the system.
                
                // Schedule UI updates and state changes on the MainActor.
                // This Task is "fire-and-forget" for UI/state work.
                Task { @MainActor in
                    guard let strongSelf = self else { return }
                    strongSelf.cameraPermissionStatus = granted_from_avkit ? .authorized : .denied // Update state
                    if granted_from_avkit {
                        strongSelf.logger.info("✅ Camera (FireWire/Thunderbolt) permission granted by user.")
                    } else {
                        strongSelf.logger.error("❌ Camera (FireWire/Thunderbolt) permission denied by user.")
                        // Show alert AFTER updating status.
                        strongSelf.showPermissionRequiredAlert()
                    }
                }
                
                // Call the original completion handler (which is likely continuation.resume).
                // This is safe to call from any thread.
                completion?(granted_from_avkit)
            }

        case .restricted, .denied:
            // Already on MainActor.
            logger.error("Camera (FireWire/Thunderbolt) permission is \(statusString(self.cameraPermissionStatus)).")
            showPermissionRequiredAlert() // Show explanation
            completion?(false) // Permission is not granted

        case .authorized:
            // Already on MainActor.
            logger.info("Camera (FireWire/Thunderbolt) permission already authorized.")
            completion?(true) // Permission is granted

        @unknown default:
            // Already on MainActor.
            logger.warning("Unknown camera permission status: \(self.cameraPermissionStatus)")
            completion?(false) // Treat unknown as not granted
        }
    }

    // Helper to show an alert when permission is missing/denied
    private func showPermissionRequiredAlert() {
        // Ensure alert runs on main thread (already on MainActor)
        let message = """
        FWA-Control needs permission to access FireWire/Thunderbolt devices (categorized as 'Camera' by macOS) to communicate with your audio interface.

        Please grant access in System Settings > Privacy & Security > Camera.
        """
        // Use a shared alert helper if available, otherwise define locally or pass context
        showAlert(title: "Permission Required", message: message, style: .critical)
    }

     // Helper function to make status readable in logs (optional)
     private func statusString(_ status: AVAuthorizationStatus) -> String {
         switch status {
         case .notDetermined: return "Not Determined"
         case .restricted: return "Restricted"
         case .denied: return "Denied"
         case .authorized: return "Authorized"
         @unknown default: return "Unknown"
         }
     }

     // Temporary Alert Helper (move to a shared location later)
     private func showAlert(title: String, message: String, style: NSAlert.Style) {
        let alert = NSAlert()
        alert.messageText = title
        alert.informativeText = message
        alert.alertStyle = style
        alert.addButton(withTitle: "OK")
        alert.runModal()
    }
}