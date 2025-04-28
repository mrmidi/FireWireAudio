#include "FWADriverInit.hpp"
#include "FWADriverHandler.hpp"
#include <os/log.h>
// Include the XPC Manager
#include "DriverXPCManager.hpp"

constexpr const char* LogPrefix = "FWADriverASPL: ";

// Implement the new constructor
FWADriverInit::FWADriverInit(std::shared_ptr<FWADriverHandler> ioHandler)
    : ioHandler_(ioHandler)
{
    // Constructor body (if needed)
}

OSStatus FWADriverInit::OnInitialize() {
    os_log(OS_LOG_DEFAULT, "%sFWADriverInit: OnInitialize called.", LogPrefix);

    // Get the XPC manager singleton
    auto& xpcManager = DriverXPCManager::instance();

    // Attempt to connect to the daemon via XPC
    bool connected = xpcManager.connect();

    if (connected) {
        os_log(OS_LOG_DEFAULT, "%sFWADriverInit: Successfully connected to daemon via XPC.", LogPrefix);
        // Notify daemon that the driver is present
        xpcManager.setPresenceStatus(true);
        // --- SHM Setup Implementation ---
        std::string shmName = xpcManager.getSharedMemoryName();
        if (!shmName.empty() && ioHandler_) {
            os_log(OS_LOG_DEFAULT, "%sFWADriverInit: Attempting to set up shared memory: %s", LogPrefix, shmName.c_str());
            if (!ioHandler_->SetupSharedMemory(shmName)) {
                os_log_error(OS_LOG_DEFAULT, "%sFWADriverInit: FAILED to setup shared memory via handler.", LogPrefix);
                // Consider returning an error if SHM is critical for function
                // return kAudioHardwareUnspecifiedError;
            } else {
                os_log(OS_LOG_DEFAULT, "%sFWADriverInit: Shared memory setup call successful.", LogPrefix);
            }
        } else {
            os_log_error(OS_LOG_DEFAULT, "%sFWADriverInit: Could not get SHM name via XPC or ioHandler_ is null.", LogPrefix);
            // return kAudioHardwareUnspecifiedError;
        }
        // --- End SHM Setup Implementation ---
    } else {
        os_log_error(OS_LOG_DEFAULT, "%sFWADriverInit: FAILED to connect to daemon via XPC. Driver may not function correctly.", LogPrefix);
        // For now, let it load but log the error.
    }

    return kAudioHardwareNoError;
}

// void FWADriverInit::OnFinalize() {
//     os_log(OS_LOG_DEFAULT, "%sFWADriverInit: OnFinalize called.", LogPrefix);

//     // Get the XPC manager singleton
//     auto& xpcManager = DriverXPCManager::instance();

//     // Notify daemon that the driver is disappearing (best effort)
//     if (xpcManager.isConnected()) {
//         xpcManager.setPresenceStatus(false);
//     }
//     // Disconnect XPC
//     xpcManager.disconnect();
// }
