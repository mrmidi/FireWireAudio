#include "FWADriverInit.hpp"
#include "FWADriverHandler.hpp"
#include <os/log.h>
// Include the XPC Manager
#include "DriverXPCManager.hpp"

constexpr const char* LogPrefix = "FWADriverASPL: ";

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
        // TODO: Add logic here to get SHM name via XPC and call SetupSharedMemory on handler
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
