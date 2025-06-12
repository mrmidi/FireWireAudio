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

class CIPPreCalculator {
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

    // From the DCL callback: fetch the ready group
    struct GroupState {
        std::atomic<bool> ready{false};
        std::atomic<uint64_t> preparedAtTime{0};
        std::atomic<uint32_t> groupNumber{UINT32_MAX};  // Track actual group number
        std::array<PreCalculatedPacket, 32> packets;
        uint8_t finalDbc;
        uint8_t packetCount;
        uint8_t reserved[2];  // Adjust padding
    };

    const GroupState* getGroupState(uint32_t groupIdx) const;
    void markGroupConsumed(uint32_t groupIdx);
    void logStatistics() const;
    
    // Force sync DBC state between transmitter and pre-calc
    void forceSync(uint8_t dbc, bool prevWasNoData);

private:
    void calculateNextGroup();
    bool emergencyCalculateCIP(CIPHeader* header, uint8_t packetIndex);
    std::chrono::microseconds getSleepDuration() const;
    void configureCPUAffinity();

    // constants
    static constexpr uint32_t TICKS_PER_CYCLE = 3072;
    static constexpr uint32_t PHASE_MOD = 147;            // 44.1 kHz jitter period
    static constexpr uint32_t BASE_INC_441 = 1386;
    static constexpr uint8_t  SYT_INTERVAL = 8;           // frames per packet
    static constexpr size_t   kBufferDepth = 4;
    static constexpr uint64_t kMaxPreparedAge = 2'000'000ULL; // ~2ms

    // Triple buffer
    std::array<GroupState, kBufferDepth> groupStates_;

    // Thread & indices
    std::thread calcThread_;
    std::atomic<bool> running_{false};
    std::atomic<uint32_t> nextGroup_{0};
    std::atomic<uint32_t> lastConsumed_{0};

    // config & node
    TransmitterConfig config_;
    uint16_t nodeID_{0};

    // thread-local state
    struct CalcState {
        uint8_t  dbc{0};
        bool     prevWasNoData{true};
        // for 44.1 kHz
        uint32_t sytOffset{0};
        uint32_t sytPhase{0};
        // for 48 kHz
        uint32_t phase480{0};
    } calcState_;
    
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
