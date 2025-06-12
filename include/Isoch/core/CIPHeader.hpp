#pragma once

#include <cstdint>
#include <CoreServices/CoreServices.h> // For OSSwapHostToBigInt16

namespace FWA {
namespace Isoch {

// Use a namespace to hold constants and helpers
namespace CIP {
    // Helper to ensure SYT value is correctly byte-swapped for the packet buffer
    inline uint16_t makeBigEndianSyt(uint16_t hostValue) {
        return OSSwapHostToBigInt16(hostValue);
    }
}

// Per IEC 61883-6, the CIP header for audio is an 8-byte structure.
// The order of fields is critical. Packing must be enforced to prevent
// the compiler from adding padding bytes.
#pragma pack(push, 1)
struct CIPHeader
{
    // Quadlet 0
    uint8_t  sid_byte;       // Byte 0: Source ID (Node ID of the sender)
    uint8_t  dbs;            // Byte 1: Data Block Size (in quadlets)
    uint8_t  fn_qpc_sph_rsv; // Byte 2: FN, QPC, SPH fields (usually all 0 for AMDTP)
    uint8_t  dbc;            // Byte 3: Data Block Counter
    // Quadlet 1
    uint8_t  fmt_eoh1;       // Byte 4: Format (FMT) and End-of-Header (EOH)
    uint8_t  fdf;            // Byte 5: Format Dependent Field (contains sample rate code)
    uint16_t syt;            // Bytes 6-7: Synchronization Timestamp (must be Big Endian)
};
#pragma pack(pop)

// This compile-time check ensures the struct is exactly 8 bytes. If it fails,
// the compiler is adding padding, and the pragma pack is not working correctly.
static_assert(sizeof(CIPHeader) == 8, "CIPHeader must be packed to 8 bytes");

// Constants for filling the CIP Header, improving readability
namespace CIP {
    // FDF (Format Dependent Field) values for sample rate
    // CRITICAL: FDF always contains sample rate, NEVER changes for NO-DATA packets!
    constexpr uint8_t kFDF_44k1  = 0x01;
    constexpr uint8_t kFDF_48k   = 0x02;

    // Format (FMT) and End-of-Header (EOH) values
    constexpr uint8_t kFmtMBLA   = 0x24; // FMT=0x24 for MBLA - Duet format
    constexpr uint8_t kEOH       = 0x00;  

    // Combined FMT and EOH value for AMDTP
    // constexpr uint8_t kFmtEohValue = (0x10 << 2) | 0x01; // FMT=0x10, EOH=1 -> 0x41
     constexpr uint8_t kFmtEohValue    = (kFmtMBLA << 2) | kEOH;   // == 0x90

    // Special value for the SYT field in NO_DATA packets
    constexpr uint16_t kSytNoData = 0xFFFF;
    
    // Helper to check if packet is NO-DATA based on SYT value
    inline bool isNoDataPacket(uint16_t syt) {
        return syt == kSytNoData;
    }
}

} // namespace Isoch
} // namespace FWA