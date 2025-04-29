#pragma once

#include <IOKit/firewire/IOFireWireLibIsoch.h>
#include <IOKit/firewire/IOFireWireFamilyCommon.h>

namespace FWA {
namespace Isoch {

// Use the system's IOVirtualRange instead of defining our own
// This prevents type conflicts with system FireWire libraries
using IOVirtualRange = ::IOVirtualRange;

// Common DCL-related type definitions
using DCLCommandPtr = NuDCLRef;  // Changed to use NuDCLRef which is the correct type
using SendPacketRef = NuDCLSendPacketRef;

// Common struct for cycle information
struct CycleInfo {
    IOVirtualRange ranges[2];  // One for CIP header, one for data
    uint32_t numRanges{0};
    bool isEventPending{false};
};

// Message types for AMDTP communication
enum class AmdtpMessageType : uint32_t {
    DataPull = 1,
    TimeStampAdjust,
    DCLOverrunAutoRestartFailed,
    AllocateIsochPort,
    ReleaseIsochPort
};

} // namespace Isoch
} // namespace FWA