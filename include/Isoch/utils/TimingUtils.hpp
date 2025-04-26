// include/FWA/Isoch/utils/TimingUtils.hpp
#pragma once

#include <cstdint>           // uint32_t, uint64_t, int64_t
#include <mach/mach_time.h>  // mach_timebase_info, mach_absolute_time()

// Uncomment in CMakeLists if __int128 is available and desired:
// add_definitions(-DFWA_USE_INT128)

namespace FWA {
namespace Isoch {
namespace Timing {

//-----------------------------------------------------------------------------
// 0. Host timebase (macOS only)
//-----------------------------------------------------------------------------
// We cache the mach_timebase_info so we can convert between mach_absolute_time()
// ticks and wall-clock nanoseconds. Must call once during init.
inline mach_timebase_info_data_t gHostTimebaseInfo = {0, 0};

/**
 * @brief Query and cache the host timebase info.
 * 
 * macOS provides mach_absolute_time() in arbitrary ticks. To convert to
 * nanoseconds, we need the ratio numer/denom. This must be done once,
 * preferably in single-threaded startup.
 * 
 * @return true on success, false if the system call failed.
 */
inline bool initializeHostTimebase() {
    if (gHostTimebaseInfo.denom == 0) {
        // Only ask once
        kern_return_t kr = mach_timebase_info(&gHostTimebaseInfo);
        // Success if denom non-zero and call succeeded
        return (kr == KERN_SUCCESS && gHostTimebaseInfo.denom != 0);
    }
    return true;
}

//-----------------------------------------------------------------------------
// 1. FireWire cycle-time format (IEC 61883-6)
//-----------------------------------------------------------------------------
// FireWire divides time into 1/8000 s cycles, each cycle into 3072 offsets.
// The hardware exposes a 32-bit register with:
//   bits 25–31: seconds (0–127, wraps every 128 s)
//   bits 12–24: cycle index within the second (0–7999)
//   bits  0–11: offset within the cycle (0–3071)

/// Bus rate: 8 000 cycles per second (125 µs per cycle)
constexpr uint32_t kCyclesPerSecond    = 8000;
/// Offsets per cycle: 3 072 offsets → ~40.69 ns per offset
constexpr uint32_t kOffsetsPerCycle    = 3072;
/// Total offsets per second = 8000 × 3072 = 24 576 000
constexpr uint64_t kOffsetsPerSecond   = uint64_t(kCyclesPerSecond) * kOffsetsPerCycle;

/// Nanoseconds per second
constexpr uint64_t kNanosPerSecond     = 1'000'000'000ULL;
/// Nanoseconds per FireWire cycle: 1 000 000 000 / 8000 = 125 000 ns = 125 µs
constexpr uint64_t kNanosPerCycle      = kNanosPerSecond / kCyclesPerSecond;

/// Wraparound period: 128 seconds (per IEEE 1394-TA Spec)
constexpr uint32_t kFWTimeWrapSeconds  = 128;
constexpr uint64_t kFWTimeWrapNanos    = uint64_t(kFWTimeWrapSeconds) * kNanosPerSecond;
constexpr uint64_t kFWTimeWrapCycles   = uint64_t(kFWTimeWrapSeconds) * kCyclesPerSecond;

// Masks and shifts to extract each field from the 32-bit register
constexpr uint32_t kEncSecondsMask     = 0xFE000000; // bits 25–31
constexpr uint32_t kEncSecondsShift    = 25;
constexpr uint32_t kEncCyclesMask      = 0x01FFF000; // bits 12–24
constexpr uint32_t kEncCyclesShift     = 12;
constexpr uint32_t kEncOffsetsMask     = 0x00000FFF; // bits 0–11

//-----------------------------------------------------------------------------
// 2. Encoded ↔︎ Nanoseconds conversions
//-----------------------------------------------------------------------------

/**
 * @brief Decode a 32-bit FireWire cycle time into total nanoseconds.
 * 
 * Steps:
 *   1. Extract seconds, cycles, and offsets fields.
 *   2. Combine into a single 64-bit offset count: 
 *        totalOffsets = seconds*OFFSETS_PER_SECOND
 *                     + cycles*OFFSETS_PER_CYCLE
 *                     + offsets.
 *   3. Convert offsets → nanoseconds via:
 *        (totalOffsets / OFFSETS_PER_SECOND) * 1e9
 *      plus fractional leftover.
 * 
 * @param enc 32-bit encoded FireWire cycle time.
 * @return 64-bit nanoseconds since some wrap-epoch.
 */
inline uint64_t encodedFWTimeToNanos(uint32_t enc) noexcept {
    uint32_t sec     = (enc & kEncSecondsMask) >> kEncSecondsShift;
    uint32_t cyc     = (enc & kEncCyclesMask)  >> kEncCyclesShift;
    uint32_t offs    =  enc & kEncOffsetsMask;

    // Total offset ticks
    uint64_t totalOff = uint64_t(sec) * kOffsetsPerSecond
                      + uint64_t(cyc) * kOffsetsPerCycle
                      + offs;

#ifdef FWA_USE_INT128
    // Multiply totalOff * 1e9 (ns) using 128-bit accumulator, then divide by offsets/sec
    __uint128_t tmp = __uint128_t(totalOff) * kNanosPerSecond;
    return uint64_t(tmp / kOffsetsPerSecond);
#else
    // Split integer and fractional division
    uint64_t fullSecs      = (totalOff / kOffsetsPerSecond) * kNanosPerSecond;
    uint64_t remOffsets    = totalOff % kOffsetsPerSecond;
    uint64_t fracNs        = (remOffsets * kNanosPerSecond) / kOffsetsPerSecond;
    return fullSecs + fracNs;
#endif
}

/**
 * @brief Encode a nanosecond timestamp into 32-bit FireWire cycle time.
 * 
 * Steps:
 *   1. Wrap input nanos to [0, 128 s) via modulo.
 *   2. Compute total offset ticks = nanos * OFFSETS_PER_SECOND / 1e9.
 *   3. Split into sec, cycles, offsets fields.
 *   4. Pack into 32 bits: (sec<<25)|(cyc<<12)|offs.
 * 
 * @param nanos Absolute time in nanoseconds.
 * @return 32-bit encoded FireWire cycle time.
 */
inline uint32_t nanosToEncodedFWTime(uint64_t nanos) noexcept {
    // Wrap every 128 seconds
    uint64_t nsWrapped = nanos % kFWTimeWrapNanos;

#ifdef FWA_USE_INT128
    __uint128_t tmp = __uint128_t(nsWrapped) * kOffsetsPerSecond;
    uint64_t totalOff = uint64_t(tmp / kNanosPerSecond);
#else
    uint64_t fullOff = (nsWrapped / kNanosPerSecond) * kOffsetsPerSecond;
    uint64_t remNs   = nsWrapped % kNanosPerSecond;
    uint64_t partOff = (remNs * kOffsetsPerSecond) / kNanosPerSecond;
    uint64_t totalOff = fullOff + partOff;
#endif

    uint32_t sec  = uint32_t(totalOff / kOffsetsPerSecond) & 0x7F;
    uint32_t rem  = uint32_t(totalOff % kOffsetsPerSecond);
    uint32_t cyc  = rem / kOffsetsPerCycle;
    uint32_t offs = rem % kOffsetsPerCycle;

    return (sec << kEncSecondsShift)
         | (cyc << kEncCyclesShift)
         | offs;
}

/**
 * @brief Compute the signed nanosecond delta between two encoded FireWire times.
 * 
 * Because the 32-bit counter wraps every 128 s, we:
 *   1. Decode each to nanoseconds.
 *   2. Subtract to get raw diff.
 *   3. If |diff| > 64 s, adjust by ±128 s to pick the shortest path across wrap.
 * 
 * @param a Encoded time A.
 * @param b Encoded time B.
 * @return Signed delta in nanoseconds (A − B), minimal magnitude across wrap.
 */
inline int64_t deltaFWTimeNano(uint32_t a, uint32_t b) noexcept {
    int64_t na = int64_t(encodedFWTimeToNanos(a));
    int64_t nb = int64_t(encodedFWTimeToNanos(b));
    int64_t d  = na - nb;

    constexpr int64_t halfWrap = int64_t(kFWTimeWrapNanos / 2);
    if (d >  halfWrap) d -= int64_t(kFWTimeWrapNanos);
    if (d < -halfWrap) d += int64_t(kFWTimeWrapNanos);
    return d;
}

//-----------------------------------------------------------------------------
// 3. Host time conversions
//-----------------------------------------------------------------------------

/**
 * @brief Convert mach_absolute_time() ticks → nanoseconds.
 * @pre  initializeHostTimebase() returns true.
 * 
 * Uses timebase_info.numer/denom:
 *   ns = ticks * numer / denom
 * 
 * @param ticks Raw mach_absolute_time() value.
 * @return Nanoseconds since boot, or 0 if timebase not inited.
 */
inline uint64_t hostTicksToNanos(uint64_t ticks) noexcept {
    if (gHostTimebaseInfo.denom == 0) {
        return 0; // Not initialized
    }
#ifdef FWA_USE_INT128
    __uint128_t tmp = __uint128_t(ticks) * gHostTimebaseInfo.numer;
    return uint64_t(tmp / gHostTimebaseInfo.denom);
#else
    // Split 64-bit multiply to avoid overflow
    uint64_t hi = (ticks >> 32) * gHostTimebaseInfo.numer;
    uint64_t lo = (ticks & 0xFFFFFFFFULL) * gHostTimebaseInfo.numer;
    uint64_t combined = (hi << 32) + lo;
    return combined / gHostTimebaseInfo.denom;
#endif
}

/**
 * @brief Convert nanoseconds → mach_absolute_time() ticks.
 * @pre  initializeHostTimebase() returns true.
 * 
 * Inverse of hostTicksToNanos: ticks = nanos * denom / numer.
 */
inline uint64_t nanosToHostTicks(uint64_t nanos) noexcept {
    if (gHostTimebaseInfo.numer == 0) {
        return 0;
    }
#ifdef FWA_USE_INT128
    __uint128_t tmp = __uint128_t(nanos) * gHostTimebaseInfo.denom;
    return uint64_t(tmp / gHostTimebaseInfo.numer);
#else
    uint64_t hi = (nanos >> 32) * gHostTimebaseInfo.denom;
    uint64_t lo = (nanos & 0xFFFFFFFFULL) * gHostTimebaseInfo.denom;
    uint64_t combined = (hi << 32) + lo;
    return combined / gHostTimebaseInfo.numer;
#endif
}

} // namespace Timing
} // namespace Isoch
} // namespace FWA
