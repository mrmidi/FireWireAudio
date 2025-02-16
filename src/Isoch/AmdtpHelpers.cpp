#include "FWA/Isoch/AmdtpHelpers.hpp"
#include <sstream>

// Additional required standard headers
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

namespace FWA {
namespace Isoch {

    // --- Remote Port Helpers Implementation ---
    IOReturn AmdtpHelpers::RemotePort_GetSupported_Helper(IOFireWireLibIsochPortRef interface,
                                                          IOFWSpeed* outMaxSpeed,
                                                          UInt64* outChanSupported)
    {
        auto handler = static_cast<AmdtpRemotePortHandler*>( (*interface)->GetRefCon(interface) );
        return handler->RemotePort_GetSupported(interface, outMaxSpeed, outChanSupported);
    }

    IOReturn AmdtpHelpers::RemotePort_AllocatePort_Helper(IOFireWireLibIsochPortRef interface,
                                                          IOFWSpeed maxSpeed,
                                                          UInt32 channel)
    {
        auto handler = static_cast<AmdtpRemotePortHandler*>( (*interface)->GetRefCon(interface) );
        return handler->RemotePort_AllocatePort(interface, maxSpeed, channel);
    }

    IOReturn AmdtpHelpers::RemotePort_ReleasePort_Helper(IOFireWireLibIsochPortRef interface)
    {
        auto handler = static_cast<AmdtpRemotePortHandler*>( (*interface)->GetRefCon(interface) );
        return handler->RemotePort_ReleasePort(interface);
    }

    IOReturn AmdtpHelpers::RemotePort_Start_Helper(IOFireWireLibIsochPortRef interface)
    {
        auto handler = static_cast<AmdtpRemotePortHandler*>( (*interface)->GetRefCon(interface) );
        return handler->RemotePort_Start(interface);
    }

    IOReturn AmdtpHelpers::RemotePort_Stop_Helper(IOFireWireLibIsochPortRef interface)
    {
        auto handler = static_cast<AmdtpRemotePortHandler*>( (*interface)->GetRefCon(interface) );
        return handler->RemotePort_Stop(interface);
    }

    // --- DCL Callback Helpers ---
    void AmdtpHelpers::UniversalReceiveDCLCallback_Helper(void* refcon, NuDCLRef dcl)
    {
        // Replace with your object's callback call.
        // Example:
        // static_cast<YourReceiverClass*>(refcon)->UniversalReceiveDCLCallback();
    }

    void AmdtpHelpers::UniversalReceiveOverrunDCLCallback_Helper(void* refcon, NuDCLRef dcl)
    {
        // Replace with your object's callback call.
        // Example:
        // static_cast<YourReceiverClass*>(refcon)->UniversalReceiveOverrunDCLCallback();
    }

    IOReturn AmdtpHelpers::UniversalReceiveFinalizeCallback_Helper(void* refcon)
    {
        // Replace with your object's finalize callback.
        // Example:
        // static_cast<YourReceiverClass*>(refcon)->UniversalReceiveFinalizeCallback();
        return kIOReturnSuccess;
    }

    // --- Time Helper ---
    // Note: AbsoluteTime and Nanoseconds are defined in the macOS headers.
    // We simply wrap the calls.
    AbsoluteTime AmdtpHelpers::GetUpTime() {
        AbsoluteTime uptime;
        uint64_t time = mach_absolute_time();

        uptime.lo = (UInt32)(time & 0xFFFFFFFF);  // Lower 32 bits
        uptime.hi = (UInt32)(time >> 32);           // Upper 32 bits

        return uptime;
    }

    Nanoseconds AmdtpHelpers::AbsoluteTimeToNanoseconds(AbsoluteTime at) {
        Nanoseconds ns;
        uint64_t time = ((uint64_t)at.hi << 32) | at.lo;

        mach_timebase_info_data_t timebase_info;
        mach_timebase_info(&timebase_info);

        uint64_t timeInNanoseconds = (time * timebase_info.numer) / timebase_info.denom;
        ns.lo = (UInt32)(timeInNanoseconds & 0xFFFFFFFF);  // Lower 32 bits
        ns.hi = (UInt32)(timeInNanoseconds >> 32);         // Upper 32 bits

        return ns;
    }

#ifdef kAVS_Enable_ForceStop_Handler
    void AmdtpHelpers::UniversalReceiveForceStopHandler_Helper(IOFireWireLibIsochChannelRef interface, UInt32 stopCondition)
    {
        // Replace with a call to your object's force-stop handler.
        // Example:
        // static_cast<YourReceiverClass*>( (*interface)->GetRefCon(interface) )->UniversalReceiveForceStop(stopCondition);
    }
#endif

    // --- No-data Timeout Helper ---
    void AmdtpHelpers::NoDataTimeoutHelper(CFRunLoopTimerRef timer, void* data)
    {
        // Replace with your object's no-data timeout callback.
        // Example:
        // static_cast<YourReceiverClass*>(data)->NoDataTimeout();
    }

    // --- Shared Logging and Error Reporting ---
    std::string AmdtpHelpers::IOReturnToString(IOReturn code)
    {
        std::ostringstream oss;
        oss << "IOReturn code: " << code;
        return oss.str();
    }

} // namespace Isoch
} // namespace FWA