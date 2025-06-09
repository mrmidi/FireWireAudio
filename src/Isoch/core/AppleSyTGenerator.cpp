#include "Isoch/core/AppleSyTGenerator.hpp"
#include <spdlog/spdlog.h>

namespace FWA {
namespace Isoch {

AppleSyTGenerator::AppleSyTGenerator(std::shared_ptr<spdlog::logger> logger)
    : m_logger(logger ? std::move(logger) : spdlog::default_logger()) {
    reset();
    m_logger->debug("AppleSyTGenerator created.");
}

void AppleSyTGenerator::reset() {
    m_logger->debug("AppleSyTGenerator resetting state...");
    m_currentTimeRef_ticks.store(0.0, std::memory_order_relaxed);
    m_idealDataTime_ticks.store(0.0, std::memory_order_relaxed);
    m_sampleRate = 0.0;
    m_config_sytIntervalVal = 0;
    m_dda_accumulator = 0;
    m_dda_ticksWhole = 0;
    m_dda_ticksNumeratorRem = 0;
    m_dda_denominator = 0;
    m_lastRawCycleTime.store(0, std::memory_order_relaxed);
    m_cycleTimeEpochOffset.store(0, std::memory_order_relaxed);
    m_isInitialized.store(false, std::memory_order_relaxed);
    m_isSeeded.store(false, std::memory_order_relaxed);
}

void AppleSyTGenerator::initialize(double sampleRate) {
    m_logger->info("AppleSyTGenerator initializing for sample rate: {:.0f} Hz", sampleRate);
    reset(); // Reset state before new initialization
    m_sampleRate = sampleRate;

    if (m_sampleRate == 44100.0) {
        m_config_sytIntervalVal = 8;
    } else if (m_sampleRate == 48000.0) {
        m_config_sytIntervalVal = 16;
    } else if (m_sampleRate == 88200.0) {
        m_config_sytIntervalVal = 8; // Or 4, needs confirmation for higher rates based on device behavior
    } else if (m_sampleRate == 96000.0) {
        m_config_sytIntervalVal = 16; // Or 8
    } else {
        m_logger->warn("Unsupported sample rate {:.0f} for Apple SYT m_config_sytIntervalVal. Defaulting to 8 (as for 44.1kHz).", m_sampleRate);
        m_config_sytIntervalVal = 8;
    }
    m_logger->debug("Apple SYT Config: m_config_sytIntervalVal = {}", m_config_sytIntervalVal);

    if (m_sampleRate > 0 && m_config_sytIntervalVal > 0) {
        uint64_t total_ticks_dividend_for_dda = (uint64_t)Timing::kOffsetsPerSecond * m_config_sytIntervalVal;
        m_dda_denominator = static_cast<uint32_t>(m_sampleRate);

        m_dda_ticksWhole = total_ticks_dividend_for_dda / m_dda_denominator;
        m_dda_ticksNumeratorRem = total_ticks_dividend_for_dda % m_dda_denominator;
        m_dda_accumulator = m_dda_ticksNumeratorRem / 2; // Pre-roll for rounding

        m_logger->debug("Apple SYT DDA: WholeTicks={}, RemNumerator={}, Denominator={}, InitialAccum={}",
                       m_dda_ticksWhole, m_dda_ticksNumeratorRem, m_dda_denominator, m_dda_accumulator);
    } else {
        m_logger->error("Apple SYT: Invalid sampleRate or m_config_sytIntervalVal for DDA setup during initialize().");
    }
    m_baseOffset_ticks = 2506; // From decompiled SYT_OFFSET = 0x9CA
    m_isInitialized.store(true, std::memory_order_release);
}

uint64_t AppleSyTGenerator::convertRawHWTimeToTotalUnscaledOffsets(uint32_t raw_cycle_time) {
    // This function needs to be robust. Using TimingUtils::encodedFWTimeToNanos is a good base
    // if it correctly provides a continuous nanosecond count that can be converted back.
    // For simplicity and directness, a local unwrap:
    uint32_t last_raw = m_lastRawCycleTime.load(std::memory_order_relaxed);
    uint64_t current_epoch_offset = m_cycleTimeEpochOffset.load(std::memory_order_relaxed);

    if (last_raw != 0) {
        int32_t last_secs = (last_raw & Timing::kEncSecondsMask) >> Timing::kEncSecondsShift;
        int32_t current_secs = (raw_cycle_time & Timing::kEncSecondsMask) >> Timing::kEncSecondsShift;
        if (last_secs > 120 && current_secs < 10) { // Heuristic for 128s wrap
            uint64_t new_epoch_offset = current_epoch_offset + (Timing::kFWTimeWrapCycles * Timing::kOffsetsPerCycle);
             // Only one thread will be calling this typically (the DCL callback thread).
             // If multiple threads, compare_exchange_strong would be needed.
            m_cycleTimeEpochOffset.store(new_epoch_offset, std::memory_order_relaxed);
            current_epoch_offset = new_epoch_offset;
        }
    }
    m_lastRawCycleTime.store(raw_cycle_time, std::memory_order_relaxed);

    uint32_t sec  = (raw_cycle_time & Timing::kEncSecondsMask) >> Timing::kEncSecondsShift;
    uint32_t cyc  = (raw_cycle_time & Timing::kEncCyclesMask)  >> Timing::kEncCyclesShift;
    uint32_t offs =  raw_cycle_time & Timing::kEncOffsetsMask;

    return current_epoch_offset +
           static_cast<uint64_t>(sec)  * Timing::kOffsetsPerSecond +
           static_cast<uint64_t>(cyc)  * Timing::kOffsetsPerCycle +
           offs;
}

void AppleSyTGenerator::seedWithHardwareTime(uint32_t raw_hardware_cycle_time) {
    if (!m_isInitialized.load(std::memory_order_acquire)) {
        m_logger->error("AppleSyTGenerator: Cannot seed, not initialized with sample rate yet.");
        return;
    }
    if (raw_hardware_cycle_time == 0) {
        m_logger->warn("AppleSyTGenerator: Attempted to seed with invalid raw_hardware_cycle_time (0).");
        return;
    }

    uint64_t current_fw_total_unscaled_offsets = convertRawHWTimeToTotalUnscaledOffsets(raw_hardware_cycle_time);
    double initial_time_ticks = static_cast<double>(current_fw_total_unscaled_offsets);

    m_currentTimeRef_ticks.store(initial_time_ticks, std::memory_order_relaxed);
    m_idealDataTime_ticks.store(initial_time_ticks, std::memory_order_relaxed);
    // Reset DDA accumulator as idealDataTime_ticks is fresh
    m_dda_accumulator = m_dda_ticksNumeratorRem / 2; // Pre-roll

    m_isSeeded.store(true, std::memory_order_release);
    m_logger->info("AppleSyTGenerator: SYT state seeded. Raw HW Time: {:#010x}, Unscaled Offsets: {}. Time Refs set to: {:.0f}",
                   raw_hardware_cycle_time, current_fw_total_unscaled_offsets, initial_time_ticks);
}

void AppleSyTGenerator::updateCurrentTimeReference(uint32_t raw_hardware_cycle_time) {
    if (!m_isSeeded.load(std::memory_order_acquire)) {
        // If not seeded yet, this call effectively becomes the seed.
        seedWithHardwareTime(raw_hardware_cycle_time);
        return;
    }
    if (raw_hardware_cycle_time == 0) {
         m_logger->warn("AppleSyTGenerator: Attempted to update current time with invalid raw_hardware_cycle_time (0).");
        return;
    }

    uint64_t current_fw_total_unscaled_offsets = convertRawHWTimeToTotalUnscaledOffsets(raw_hardware_cycle_time);
    m_currentTimeRef_ticks.store(static_cast<double>(current_fw_total_unscaled_offsets), std::memory_order_relaxed);
    // m_idealDataTime_ticks continues its DDA progression.
}


AppleSyTGenerator::SyTResult AppleSyTGenerator::calculateSyT() {
    SyTResult result = {true, 0xFFFF}; // Default to NO_DATA

    if (!m_isSeeded.load(std::memory_order_relaxed)) {
        m_logger->warn("AppleSyTGenerator::calculateSyT called before SYT state was seeded. Returning NO_DATA.");
        return result;
    }

    double currentDecisionTimeRef_ticks = m_currentTimeRef_ticks.load(std::memory_order_relaxed);
    double idealDataTime_ticks  = m_idealDataTime_ticks.load(std::memory_order_relaxed);

    // Advance currentDecisionTimeRef_ticks for this packet's decision point
    // This simulates the sytCycleTime_ += FIXED_INCREMENT per call in decompiled CalculatePacketHeaderData
    // The member m_currentTimeRef_ticks will be updated with the new HwTime in handleDCLComplete for the *next batch*.
    // For the current batch, it advances packet by packet.
    // So, m_currentTimeRef_ticks should be advanced and stored by this function.
    currentDecisionTimeRef_ticks = m_currentTimeRef_ticks.fetch_add(APPLE_SYT_CURRENT_TIME_ADVANCE_TICKS,
                                                                    std::memory_order_acq_rel)
                                 + APPLE_SYT_CURRENT_TIME_ADVANCE_TICKS; // fetch_add returns old, so add again for current decision

    // Convert to scaled integers for Apple's comparison logic
    uint64_t sct_scaled = static_cast<uint64_t>(round(currentDecisionTimeRef_ticks * APPLE_SYT_SCALE_FACTOR));
    uint64_t idt_scaled = static_cast<uint64_t>(round(idealDataTime_ticks * APPLE_SYT_SCALE_FACTOR));

    bool sendNoData;
    if (sct_scaled <= idt_scaled) {
        sendNoData = true;
    } else {
        if ((idt_scaled - sct_scaled + APPLE_SYT_WRAP_VALUE_SCALED) <= APPLE_SYT_COMPARISON_THRESHOLD_SCALED) {
            sendNoData = true;
        } else {
            sendNoData = false;
        }
    }

    if (sendNoData) {
        result.isNoData = true;
        result.sytValue = 0xFFFF;
    } else { // DATA Packet
        result.isNoData = false;

        double syt_calc_base_ticks = static_cast<double>(m_baseOffset_ticks) + idealDataTime_ticks;
        uint16_t syt_field_val = static_cast<uint16_t>(fmod(syt_calc_base_ticks, static_cast<double>(Timing::kOffsetsPerCycle)));
        // Ensure positive if fmod result is negative (though idealDataTime_ticks should be positive)
        if (syt_field_val >= Timing::kOffsetsPerCycle) { // fmod can sometimes give == kOffsetsPerCycle for large numbers
             syt_field_val -= Timing::kOffsetsPerCycle;
        }
        result.sytValue = syt_field_val;

        // Advance idealDataTime_ticks for the NEXT data packet using DDA
        double dda_ticks_to_add_unscaled = static_cast<double>(m_dda_ticksWhole);
        // m_dda_accumulator is a non-atomic member now, protected by SYT logic being called serially
        m_dda_accumulator += m_dda_ticksNumeratorRem;
        if (m_dda_accumulator >= static_cast<int64_t>(m_dda_denominator)) {
            dda_ticks_to_add_unscaled += 1.0;
            m_dda_accumulator -= static_cast<int64_t>(m_dda_denominator);
        }
        m_idealDataTime_ticks.store(idealDataTime_ticks + dda_ticks_to_add_unscaled, std::memory_order_release);
    }
    
    // Note: m_currentTimeRef_ticks was updated by fetch_add.
    // Normalization of m_currentTimeRef_ticks against a large wrap value could be done here
    // if it's intended to be a continuously wrapping scaled counter like in the decompiled code.
    // However, if it's re-seeded/updated by handleDCLComplete from fresh hardware time often enough,
    // it might not need explicit wrapping here, as long as differences remain valid.
    // The decompiled sytCycleTime was wrapped against APPLE_SYT_WRAP_VALUE_SCALED.
    // Let's add that:
    double final_currentTimeRef_ticks = m_currentTimeRef_ticks.load(std::memory_order_relaxed); // Value after fetch_add
    if (static_cast<uint64_t>(round(final_currentTimeRef_ticks * APPLE_SYT_SCALE_FACTOR)) >= APPLE_SYT_WRAP_VALUE_SCALED) {
        final_currentTimeRef_ticks -= static_cast<double>(APPLE_SYT_WRAP_VALUE_SCALED) / APPLE_SYT_SCALE_FACTOR;
        m_currentTimeRef_ticks.store(final_currentTimeRef_ticks, std::memory_order_relaxed);
    }

    return result;
}

} // namespace Isoch
} // namespace FWA