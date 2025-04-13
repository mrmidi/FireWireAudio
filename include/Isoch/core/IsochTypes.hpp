// TODO: Seems that it's not used anywhere

#pragma once

#include <cstdint>
#include <IOKit/IOTypes.h>
#include <IOKit/firewire/IOFireWireLib.h>
#include <IOKit/firewire/IOFireWireLibIsoch.h>

namespace FWA {
namespace Isoch {

#include <IOKit/IOReturn.h>
#include <cstdint>

// Placeholder structs for legacy types
// TODO: Remove these when possible
struct UniversalTransmitter;


// VERY simplified... May sense to implement a real one
struct CIPHeader {
    UInt32 header[2];
};

// Placeholder for UniversalTransmitterCycleInfo
// TODO: remove
struct UniversalTransmitterCycleInfo
{
    UInt32 index;                    // Cycle index within the transmission program

    UInt32 numRanges;                // Number of valid memory ranges
    IOVirtualRange ranges[5];        // Memory ranges for audio data (up to 5 ranges)
	// for FireWire Isochronous Header
	UInt8 sy;						// only low 4 bits valid!
	UInt8 tag;						// only low 2 bits valid!

    CIPHeader cipHeader;             // Precomputed CIP header for this cycle
    unsigned int nevents;            // Number of events (frames) generated for this cycle

    UInt16 nodeID;                   // Node ID for the FireWire device
    UInt32 expectedTransmitCycleTime; // Expected cycle time in FireWire clock ticks
    
    UInt64 transmitTimeInNanoSeconds; // Calculated transmission time in nanoseconds

	bool isBlocking;                 // Indicates blocking transmission for AM824 data

	UInt32 currentCycleTime;         // Current FireWire cycle time

    // Indicates if the DCL program is running:
    // - If true: "seconds field" in `expectedTransmitCycleTime` is valid.
    // - If false: "seconds field" in `expectedTransmitCycleTime` is not guaranteed.
    // Also, `transmitTimeInNanoSeconds` is valid only when true.
    bool dclProgramRunning;
};

// Other Isoch types will be added as needed

} // namespace Isoch
} // namespace FWA