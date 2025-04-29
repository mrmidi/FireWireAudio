#pragma once
#include <IOKit/IOReturn.h>
#include <IOKit/firewire/IOFireWireLibIsoch.h>
#include <mach/mach_time.h>
#include <string>
#include <CoreFoundation/CoreFoundation.h>
#include "Isoch/utils/RunLoopHelper.hpp"

namespace FWA {
namespace Isoch {

// Forward declaration for the port manager we'll use in callbacks
class AmdtpPortManager;

class AmdtpHelpers {
public:
    static uint64_t GetUpTimeNanoseconds() {
        static mach_timebase_info_data_t timebase;
        if (timebase.denom == 0) {
            mach_timebase_info(&timebase);
        }
        return mach_absolute_time() * timebase.numer / timebase.denom;
    }

    static uint64_t GetTimeInNanoseconds() {
        return GetUpTimeNanoseconds();
    }


    
    static void NoDataTimeoutHelper(CFRunLoopTimerRef timer, void* data) {
        // Log the callback using RunLoopHelper
        // logCallbackThreadInfo("AmdtpHelpers", "NoDataTimeoutHelper", data);
        
        // Implementation can use the data directly as needed
    }
    
    static std::string IOReturnToString(IOReturn code);
};

} // namespace Isoch
} // namespace FWA
