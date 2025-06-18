#pragma once

#include <atomic>
#include <thread>
#include <array>
#include <chrono>
#include <expected>
#include <mutex>
#include <mach/mach_time.h>
#include <pthread.h>
#include "Isoch/core/CIPHeader.hpp"
#include "Isoch/core/SpscRing.hpp"
#include "Isoch/core/TransmitterTypes.hpp"
#include "FWA/Error.h"

namespace FWA {
namespace Isoch {

// Pre-calculated packet + header info for lock-free consumption
struct PreCalculatedPacket {
    CIPHeader header;       // 8 bytes
    bool      isNoData;     // true ⇒ NO-DATA packet
    uint8_t   dbcIncrement; // number of DBC blocks to advance after this packet
    uint8_t   reserved[6];  // padding → total 16 bytes
};
static_assert(sizeof(PreCalculatedPacket) == 16, "PreCalculatedPacket must be 16 bytes");

// New POD for one pre-calculated group
struct alignas(16) PreCalcGroup {
    static constexpr uint32_t MaxPacketsPerGroup = 64;  // Must accommodate max config
    PreCalculatedPacket packets[MaxPacketsPerGroup];
    uint8_t finalDbc;
    bool    finalWasNoData;
};

// Compile-time safety checks
static_assert(std::is_trivially_copyable_v<PreCalcGroup>, "Ring assumes memcpy-safe group");
static_assert(std::is_trivially_copyable_v<PreCalculatedPacket>, "Ring assumes memcpy-safe packet");

class CIPPreCalculator {
private:
    // constants - must be declared before use
    static constexpr size_t   kBufferDepth = 16;  // Must be power of 2 for SPSC ring
    static_assert((kBufferDepth & (kBufferDepth-1))==0, "kBufferDepth must be power of two");

public:
    CIPPreCalculator() = default;
    ~CIPPreCalculator();
    CIPPreCalculator(const CIPPreCalculator&) = delete;
    CIPPreCalculator& operator=(const CIPPreCalculator&) = delete;

    // Initialize with transmitter config & local node ID
    void initialize(const TransmitterConfig& config, uint16_t nodeID);
    // Start/Stop background calculation thread
    void start();
    void stop();

    // Preferred API: pop next pre-calculated group
    bool popNextGroup(PreCalcGroup& group) { return groupRing_.pop(group); }
    
    // TEMPORARY: Direct ring access for compatibility during migration
    SpscRing<PreCalcGroup, kBufferDepth> groupRing_;
    
    // Legacy compatibility (will be removed)
    struct alignas(64) GroupState {
        std::atomic<uint32_t> version{0};               // even→ready, odd→writing  
        std::atomic<uint64_t> preparedAtTime{0};
        std::atomic<uint32_t> groupNumber{UINT32_MAX};  // Track actual group number
        std::array<PreCalculatedPacket, 32> packets;
        uint8_t finalDbc;
        uint8_t packetCount;
        // Pad to cache line boundary (64 bytes)
        uint8_t _pad[64 - (sizeof(std::atomic<uint32_t>) + sizeof(std::atomic<uint64_t>) + 
                           sizeof(std::atomic<uint32_t>) + 32*16 + 2) % 64];
    };

    [[deprecated("Use SPSC ring instead - remove after 2025-Q3")]]
    const GroupState* getGroupState(uint32_t groupIdx) const;
    
    [[deprecated("Use SPSC ring instead - remove after 2025-Q3")]]
    void markGroupConsumed(uint32_t groupIdx);
    void logStatistics() const;
    
    // Force sync DBC state between transmitter and pre-calc
    void forceSync(uint8_t dbc, bool prevWasNoData);
    bool emergencyCalculateCIP(CIPHeader* header, uint8_t packetIndex);
    void syncEmergencyState();  // Synchronize emergency state with main state

    // Public state structure for emergency calculation
    struct CalcState {
        uint8_t  dbc{0};
        bool     prevWasNoData{true};
        // for 44.1 kHz
        uint32_t sytOffset{0};
        uint32_t sytPhase{0};
        // for 48 kHz
        uint32_t phase480{0};
    };

private:
    void calculateNextGroup();
    std::chrono::microseconds getSleepDuration() const;
    void configureCPUAffinity();

    // constants
    static constexpr uint32_t TICKS_PER_CYCLE = 3072;
    static constexpr uint32_t PHASE_MOD = 147;            // 44.1 kHz jitter period
    static constexpr uint32_t BASE_INC_441 = 1386;
    static constexpr uint8_t  SYT_INTERVAL = 8;           // frames per packet
    static constexpr uint64_t kMaxPreparedAge = 2'000'000ULL; // ~2ms

    // Thread & indices
    std::thread calcThread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> nextGroup_{0};      // absolute counter

    // config & node
    TransmitterConfig config_;
    uint16_t nodeID_{0};

    // thread-local state
    CalcState calcState_;
    
    // Mutex to protect calcState_ during sync operations
    mutable std::mutex syncMutex_;

    // performance
    struct PerfStats {
        std::atomic<uint64_t> totalCalcs{0};
        std::atomic<uint64_t> slowCalcs{0};
        std::atomic<uint64_t> maxNs{0};
    } perfStats_;
};

} // namespace Isoch
} // namespace FWA
