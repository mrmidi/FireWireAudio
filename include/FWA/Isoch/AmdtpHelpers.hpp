#pragma once

// Proper IOKit includes
#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#include <IOKit/firewire/IOFireWireLib.h>
#include <IOKit/firewire/IOFireWireLibIsoch.h>
#include <IOKit/firewire/IOFireWireFamilyCommon.h>

// Additional standard headers
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <mach/mach_time.h>

#include <memory>
#include <string>
#include <sstream>
#include <format>
#include <expected>
#include <spdlog/spdlog.h>
#include "FWA/Error.h"  // Defines FWA::IOKitError and error-code conversion

namespace FWA {
namespace Isoch {

    // Abstract interface for remote port callbacks.
    class AmdtpRemotePortHandler {
    public:
        virtual ~AmdtpRemotePortHandler() = default;

        virtual IOReturn RemotePort_GetSupported(IOFireWireLibIsochPortRef interface,
                                                 IOFWSpeed* outMaxSpeed,
                                                 UInt64* outChanSupported) = 0;

        virtual IOReturn RemotePort_AllocatePort(IOFireWireLibIsochPortRef interface,
                                                 IOFWSpeed maxSpeed,
                                                 UInt32 channel) = 0;

        virtual IOReturn RemotePort_ReleasePort(IOFireWireLibIsochPortRef interface) = 0;

        virtual IOReturn RemotePort_Start(IOFireWireLibIsochPortRef interface) = 0;

        virtual IOReturn RemotePort_Stop(IOFireWireLibIsochPortRef interface) = 0;
    };

    // AmdtpHelpers collects shared helper functions used by both transmitter and receiver.
    class AmdtpHelpers {
    public:
        // --- Remote Port Callback Helpers ---
        static IOReturn RemotePort_GetSupported_Helper(IOFireWireLibIsochPortRef interface,
                                                       IOFWSpeed* outMaxSpeed,
                                                       UInt64* outChanSupported);

        static IOReturn RemotePort_AllocatePort_Helper(IOFireWireLibIsochPortRef interface,
                                                       IOFWSpeed maxSpeed,
                                                       UInt32 channel);

        static IOReturn RemotePort_ReleasePort_Helper(IOFireWireLibIsochPortRef interface);

        static IOReturn RemotePort_Start_Helper(IOFireWireLibIsochPortRef interface);

        static IOReturn RemotePort_Stop_Helper(IOFireWireLibIsochPortRef interface);

        // --- DCL Callback Helpers ---
        static void UniversalReceiveDCLCallback_Helper(void* refcon, NuDCLRef dcl);
        static void UniversalReceiveOverrunDCLCallback_Helper(void* refcon, NuDCLRef dcl);
        static IOReturn UniversalReceiveFinalizeCallback_Helper(void* refcon);

        // --- Time Helper ---
        // These functions use the macOS types AbsoluteTime and Nanoseconds, so no new datatypes are defined.
        static AbsoluteTime GetUpTime();
        static Nanoseconds AbsoluteTimeToNanoseconds(AbsoluteTime at);

        // --- Additional Remote Port & DCL Helpers ---
#ifdef kAVS_Enable_ForceStop_Handler
        static void UniversalReceiveForceStopHandler_Helper(IOFireWireLibIsochChannelRef interface, UInt32 stopCondition);
#endif

        static void NoDataTimeoutHelper(CFRunLoopTimerRef timer, void* data);

        // --- Shared Logging and Error Reporting ---
        static inline std::shared_ptr<spdlog::logger> DefaultLogger = spdlog::default_logger();

        static void LogError(const std::string& msg) {
            if (DefaultLogger)
                DefaultLogger->error(msg);
        }

        static void LogWarning(const std::string& msg) {
            if (DefaultLogger)
                DefaultLogger->warn(msg);
        }

        static void LogInfo(const std::string& msg) {
            if (DefaultLogger)
                DefaultLogger->info(msg);
        }

        static constexpr const char* DefaultErrorPrefix() noexcept {
            return "AMDTP Error:";
        }

        static std::string IOReturnToString(IOReturn code);

        // --- Initialization Functions Using std::expected ---
        static std::expected<IOFireWireLibRemoteIsochPortRef, IOKitError>
        CreateRemoteIsochPort(IOFireWireLibNubRef nub, bool talker = false) {
            IOFireWireLibRemoteIsochPortRef port =
                (*nub)->CreateRemoteIsochPort(nub, talker, CFUUIDGetUUIDBytes(kIOFireWireRemoteIsochPortInterfaceID));
            if (!port)
                return std::unexpected(IOKitError::NoMemory);
            return port;
        }

        static std::expected<IOFireWireLibLocalIsochPortRef, IOKitError>
        CreateLocalIsochPort(IOFireWireLibNubRef nub,
                             bool talking,
                             const CFUUIDBytes& uuid,
                             CFRunLoopRef runLoop,
                             IOVirtualRange* bufferRange,
                             UInt32 rangeCount)
        {
            IOFireWireLibLocalIsochPortRef port =
                (*nub)->CreateLocalIsochPort(nub,
                                             talking,
                                             nullptr,
                                             0,
                                             0,
                                             0,
                                             nullptr,
                                             0,
                                             bufferRange,
                                             rangeCount,
                                             uuid);
            if (!port)
                return std::unexpected(IOKitError::NoMemory);
            return port;
        }

        static std::expected<IOFireWireLibNuDCLPoolRef, IOKitError>
        CreateNuDCLPool(IOFireWireLibNubRef nub) {
            IOFireWireLibNuDCLPoolRef pool =
                (*nub)->CreateNuDCLPool(nub, 0, CFUUIDGetUUIDBytes(kIOFireWireNuDCLPoolInterfaceID));
            if (!pool)
                return std::unexpected(IOKitError::NoMemory);
            return pool;
        }
    };

} // namespace Isoch
} // namespace FWA