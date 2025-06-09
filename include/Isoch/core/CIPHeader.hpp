#pragma once

#include <cstdint>

namespace FWA {
namespace Isoch {

// Simple endian conversion for our specific use case
inline uint16_t hostToBigEndian16(uint16_t value) {
    return __builtin_bswap16(value);
}

namespace CIP {
    // SYT conversion helper that avoids namespace conflicts
    inline uint16_t makeBigEndianSyt(uint16_t hostValue) {
        return hostToBigEndian16(hostValue);
    }
}

// Also provide helper for IsochHeaderData
inline uint16_t makeBigEndianDataLength(uint16_t hostValue) {
    return hostToBigEndian16(hostValue);
}

// Per IEC 61883-6 and reverse-engineering of Apple's driver, the CIP header for
// Multi-bit Linear Audio (MBLA) is an 8-byte structure.
// Packing must be enforced to prevent compiler padding.
#pragma pack(push, 1)
struct CIPHeader
{
    uint8_t  sid;                // 0
    uint8_t  dbs;                // 1
    uint8_t  fn_qpc_sph_rsv;     // 2
    uint8_t  dbc;                // 3   <── moved here
    uint8_t  fmt_eoh;            // 4   <── was at 3
    uint8_t  fdf;                // 5   <── was at 4
    uint16_t syt;                // 6-7 (big-endian)
};
#pragma pack(pop)
static_assert(sizeof(CIPHeader) == 8, "CIPHeader must be 8 bytes");

static_assert(sizeof(CIPHeader) == 8, "CIPHeader must be packed to 8 bytes");

// Constants for filling the CIP Header
namespace CIP {
    constexpr uint8_t kDataBlockSizeStereo = 0x02;
    // 0x24 is MBLA (per IEC 61883-6)
    constexpr uint8_t kFmtMBLA   = 0x24;
    constexpr uint8_t kEOH       = 0x00;               // 0 for ordinary 8-byte CIP
    constexpr uint8_t kFmtEoh    = (kFmtMBLA << 2) | kEOH;   // == 0x90

    constexpr uint8_t kFDF_44k1  = 0x01;
    constexpr uint8_t kFDF_48k   = 0x02;
    constexpr uint8_t kFDF_NoDat = 0xFF;
    
    // Special values for NO_DATA packets
    constexpr uint16_t kSytNoData = 0xFFFF;
}

} // namespace Isoch
} // namespace FWA