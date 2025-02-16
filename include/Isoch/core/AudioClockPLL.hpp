#pragma once

#include <memory>
#include <cstdint>
#include <atomic> // For atomic boolean 'initialized_'
#include <spdlog/logger.h>
#include <mach/mach_time.h> // For mach_timebase_info_data_t
#include "Isoch/core/ReceiverTypes.hpp" // For PacketTimingInfo

namespace FWA {
namespace Isoch {

class AudioClockPLL {
public:
    explicit AudioClockPLL(std::shared_ptr<spdlog::logger> logger);
    ~AudioClockPLL();

    // Initialize the PLL with initial timing correlation
    void initialize(uint64_t initialHostTimeAbs, uint32_t initialFwTimestamp);

    // Update PLL state based on new timing info from a packet
    void update(const PacketTimingInfo& timing, uint64_t currentHostTimeAbs);

    // Calculate the estimated presentation time for a given absolute sample index
    uint64_t getPresentationTimeNs(uint64_t absoluteSampleIndex);
    
    // Set target sample rate
    void setSampleRate(double rate);
    
    // Allow tuning gains
    void setPllGains(double kp, double ki);
    
    // Reset PLL state
    void resetState();
    
    // Use atomic getter
    bool isInitialized() const { return initialized_.load(); }
    
    // Helper to be called when the first valid SYT is received AFTER initialize
    void updateInitialSYT(uint16_t firstSyt, uint32_t firstSytFwTimestamp, uint64_t firstSytAbsSampleIndex);

private:
    std::shared_ptr<spdlog::logger> logger_;
    std::atomic<bool> initialized_{false}; // Use atomic for thread safety

    // Host and device clock constants
    double targetSampleRate_ = 44100.0;
    const uint64_t fwClock_NominalRate_ = 24576000; // Ticks per second
    const double fwClock_NominalRateHz_ = 24576000.0; // Double version for calculations

    // Host timebase info
    mach_timebase_info_data_t timebaseInfo_{}; // For host clock info
    uint64_t hostTicksPerSecond_ = 0;          // Approx host ticks/sec

    // Anchor points for timing correlation
    uint64_t initialHostTimeNano_ = 0;
    uint64_t initialHostTimeAbs_ = 0;          // Initial host time in mach_absolute_time units
    uint32_t initialFwTimestamp_ = 0;

    // Last known packet info
    uint64_t lastHostTimeNano_ = 0;
    uint64_t lastHostTimeAbs_ = 0;         // Last host time (absolute ticks) PLL was updated
    uint32_t lastFwTimestamp_ = 0;         // Last FW timestamp used in update
    uint16_t lastSYT_ = 0xFFFF;            // Last valid SYT value received
    
    // For SYT-based timing correlation
    uint32_t lastSYT_FWTimestamp_ = 0;     // FW timestamp when lastSYT_ arrived
    uint64_t lastSYT_AbsSampleIndex_ = 0;  // Sample index corresponding to lastSYT_
    uint64_t lastSYT_HostTimeAbs_ = 0;     // Host time (absolute ticks) when lastSYT_ arrived/processed
    uint64_t lastAbsSampleIndex_ = 0;
    uint64_t lastPacketEndAbsSampleIndex_ = 0; // Sample index after the last processed packet

    // PLL filter state
    double currentRatio_ = 1.0;                // Ratio of FW ticks to Host ticks
    double phaseErrorAccumulator_ = 0.0;       // PI controller integral term
    double frequencyAdjustment_ = 0.0;         // PI controller output

    // PLL constants
    double pllProportionalGain_ = 0.01;
    double pllIntegralGain_ = 0.0005;
    double integralMax_ = 0.001;       // Max accumulator value (prevents windup)
    double integralMin_ = -0.001;      // Min accumulator value

    // Helper methods
    void initializeHostClockInfo();
    uint64_t absolute_to_nanoseconds(uint64_t mach_time) const;
    uint64_t nanoseconds_to_absolute(uint64_t nano_time) const;
};

} // namespace Isoch
} // namespace FWA