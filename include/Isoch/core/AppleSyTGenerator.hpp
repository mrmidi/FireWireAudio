#pragma once

#include <atomic>
#include <cstdint>
#include <cmath>
#include <memory>
#include <spdlog/logger.h>
#include "Isoch/utils/TimingUtils.hpp"

namespace FWA {
namespace Isoch {

class AppleSyTGenerator {
public:
    struct SyTResult {
        bool isNoData;
        uint16_t sytValue; // 0-3071 for DATA, 0xFFFF for NO_DATA
    };

    explicit AppleSyTGenerator(std::shared_ptr<spdlog::logger> logger);

    // Call once when the stream configuration (sample rate) is known.
    void initialize(double sampleRate);

    // Call once when the first valid DCL hardware timestamp is received.
    // raw_hardware_cycle_time is the 32-bit value from GetCycleTime or DCL completion.
    void seedWithHardwareTime(uint32_t raw_hardware_cycle_time);

    // Call periodically (e.g., at the start of processing each DCL group completion)
    // to re-align the internal "current time" reference with the latest hardware time.
    void updateCurrentTimeReference(uint32_t raw_hardware_cycle_time);

    // Call for each packet for which an SYT decision is needed.
    SyTResult calculateSyT();

    void reset();

private:
    // Helper for robust unwrapping of raw 32-bit FireWire time
    uint64_t convertRawHWTimeToTotalUnscaledOffsets(uint32_t raw_cycle_time);

    // Constants (reverse-engineered from Apple's driver)
    // Per-packet advance for the decision timer (1/2 cycle = 1536 offsets)
    static constexpr double   APPLE_SYT_CURRENT_TIME_ADVANCE_TICKS = static_cast<double>(Timing::kOffsetsPerCycle) / 2.0;
    static constexpr uint64_t APPLE_SYT_WRAP_VALUE_SCALED          = 491520000ULL;
    // Comparison threshold (2048 offsets, directly from disassembly)
    static constexpr uint64_t APPLE_SYT_COMPARISON_THRESHOLD_UNSCALED = 2048ULL;
    static constexpr uint32_t APPLE_SYT_SCALE_FACTOR               = 10000;
    static constexpr uint64_t APPLE_SYT_COMPARISON_THRESHOLD_SCALED = APPLE_SYT_COMPARISON_THRESHOLD_UNSCALED * APPLE_SYT_SCALE_FACTOR;

    std::shared_ptr<spdlog::logger> m_logger;

    // State variables (unscaled ticks, using double for precision)
    std::atomic<double> m_currentTimeRef_ticks{0.0};    // "sytCycleTime_" equivalent
    std::atomic<double> m_idealDataTime_ticks{0.0};     // "currentSYTTimestamp_" equivalent

    // Configuration & DDA
    double   m_sampleRate{0.0};
    uint32_t m_config_sytIntervalVal{0};       // e.g., 8 for 44.1kHz
    uint32_t m_baseOffset_ticks{2506};         // Unscaled base offset for SYT field

    int64_t  m_dda_accumulator{0};
    uint64_t m_dda_ticksWhole{0};
    uint64_t m_dda_ticksNumeratorRem{0};
    uint32_t m_dda_denominator{0};

    // For robust unwrapping of raw hardware cycle time
    std::atomic<uint32_t> m_lastRawCycleTime{0};
    std::atomic<uint64_t> m_cycleTimeEpochOffset{0}; // In total unscaled ticks

    std::atomic<bool> m_isInitialized{false}; // Has initialize() been called?
    std::atomic<bool> m_isSeeded{false};      // Has seedWithHardwareTime() been called?
};

} // namespace Isoch
} // namespace FWA