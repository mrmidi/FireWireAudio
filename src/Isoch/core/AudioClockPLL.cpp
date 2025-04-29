#include "Isoch/core/AudioClockPLL.hpp"
#include <mach/mach_time.h>
#include <time.h> // For clock_gettime_nsec_np if needed elsewhere
#include <cmath>  // For fabs
#include <algorithm> // for std::clamp

// Define nanoseconds per second for clarity
constexpr double NANOS_PER_SECOND = 1e9;
// Define FireWire cycle timer frequency
constexpr double FW_CYCLE_TIMER_HZ = 24576000.0;

namespace FWA {
namespace Isoch {

// Constructor
AudioClockPLL::AudioClockPLL(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)),
      targetSampleRate_(44100.0) // Default
{
    initializeHostClockInfo();
    resetState();
    if (logger_) logger_->debug("AudioClockPLL created.");
}

// Destructor
AudioClockPLL::~AudioClockPLL() {
     if (logger_) logger_->debug("AudioClockPLL destroyed.");
}

// --- Configuration ---
void AudioClockPLL::setSampleRate(double rate) {
    if (rate > 0) {
        targetSampleRate_ = rate;
        if (logger_) logger_->info("PLL Target Sample Rate set to: {:.2f} Hz", targetSampleRate_);
        // Optionally reset PLL state when rate changes?
        // resetState();
    } else {
        if (logger_) logger_->error("PLL Invalid sample rate specified: {}", rate);
    }
}

void AudioClockPLL::setPllGains(double kp, double ki) {
    pllProportionalGain_ = kp;
    pllIntegralGain_ = ki;
    if (logger_) logger_->info("PLL Gains set: Kp={}, Ki={}", kp, ki);
    // Optionally set accumulator limits based on gains
    // integralMax_ = ...; integralMin_ = ...;
}

// --- Control ---
void AudioClockPLL::initializeHostClockInfo() {
    mach_timebase_info(&timebaseInfo_);
    if (timebaseInfo_.denom == 0) { // Avoid division by zero
         if (logger_) logger_->error("PLL: Invalid timebase denominator!");
         // Set defaults
         hostTicksPerSecond_ = 1000000000; // Assume 1 tick = 1 ns as fallback
    } else {
        hostTicksPerSecond_ = static_cast<uint64_t>(NANOS_PER_SECOND *
            (static_cast<double>(timebaseInfo_.denom) / static_cast<double>(timebaseInfo_.numer)));
    }
    if (logger_) logger_->debug("PLL Host Clock Info: Rate ~{} ticks/sec, {}/{} ns ratio",
                                hostTicksPerSecond_, timebaseInfo_.numer, timebaseInfo_.denom);
}

void AudioClockPLL::resetState() {
    initialized_ = false;
    initialHostTimeAbs_ = 0;
    initialFwTimestamp_ = 0;
    lastHostTimeAbs_ = 0;
    lastFwTimestamp_ = 0;
    lastSYT_ = 0xFFFF;
    lastSYT_FWTimestamp_ = 0;
    lastSYT_AbsSampleIndex_ = 0;
    lastSYT_HostTimeAbs_ = 0;
    lastPacketEndAbsSampleIndex_ = 0;
    currentRatio_ = 1.0;
    phaseErrorAccumulator_ = 0.0;
    frequencyAdjustment_ = 0.0;
    if (logger_) logger_->info("PLL state reset.");
}

void AudioClockPLL::initialize(uint64_t initialHostTimeAbs, uint32_t initialFwTimestamp) {
    resetState();
    if (logger_) logger_->info("PLL Initializing: HostTimeAbs={}, FWTimestamp={:#0x}", initialHostTimeAbs, initialFwTimestamp);
    initialHostTimeAbs_ = initialHostTimeAbs;
    initialFwTimestamp_ = initialFwTimestamp;
    lastHostTimeAbs_ = initialHostTimeAbs;
    lastFwTimestamp_ = initialFwTimestamp;
    lastPacketEndAbsSampleIndex_ = 0;
    lastSYT_HostTimeAbs_ = initialHostTimeAbs; // Initialize SYT anchor host time
    initialized_ = true;
}

// Called separately after initialize() when the first valid SYT arrives
void AudioClockPLL::updateInitialSYT(uint16_t firstSyt, uint32_t firstSytFwTimestamp, uint64_t firstSytAbsSampleIndex) {
    if (!initialized_) {
        if (logger_) logger_->warn("PLL: updateInitialSYT called before initialize!");
        return;
    }
    if (lastSYT_ == 0xFFFF) { // Only update if we haven't captured one yet
        lastSYT_ = firstSyt;
        lastSYT_FWTimestamp_ = firstSytFwTimestamp;
        lastSYT_AbsSampleIndex_ = firstSytAbsSampleIndex;
        lastSYT_HostTimeAbs_ = mach_absolute_time(); // Host time when this SYT is processed
        if (logger_) logger_->info("PLL Initial SYT Captured: SYT={}, FW_TS={:#0x}, AbsSampleIdx={}, HostAbs={}",
                                  lastSYT_, lastSYT_FWTimestamp_, lastSYT_AbsSampleIndex_, lastSYT_HostTimeAbs_);
    }
}

void AudioClockPLL::update(const PacketTimingInfo& timing, uint64_t currentHostTimeAbs) {
    if (!initialized_) {
        // Initialization now happens externally via synchronizeAndInitializePLL
        // or by calling initialize directly if the first packet has a valid FW TS.
        // We still might receive an update call before the *first SYT* has been processed.
        if (timing.fwTimestamp != 0) {
            // If initialize wasn't called yet, do it now.
            initialize(currentHostTimeAbs, timing.fwTimestamp);
            if (timing.syt != 0xFFFF) {
                // If this first packet also has SYT, anchor it.
                updateInitialSYT(timing.syt, timing.fwTimestamp, timing.firstAbsSampleIndex);
            }
        } else {
            if (logger_) logger_->warn("PLL Update: Still waiting for valid FW Timestamp to initialize.");
            return; // Can't do anything without initial sync
        }
    }

    // Only update if host time has advanced
    if (currentHostTimeAbs <= lastHostTimeAbs_ || timing.numSamplesInPacket == 0) {
        lastPacketEndAbsSampleIndex_ = timing.firstAbsSampleIndex + timing.numSamplesInPacket;
        return;
    }

    // --- PLL Update Logic using SYT ---
    if (timing.syt != 0xFFFF && lastSYT_ != 0xFFFF && timing.syt != lastSYT_) {
        // We have two consecutive valid SYTs
        uint64_t samplesSinceLastSYT = timing.firstAbsSampleIndex - lastSYT_AbsSampleIndex_;
        if (timing.firstAbsSampleIndex < lastSYT_AbsSampleIndex_) { /* Handle wrap */ samplesSinceLastSYT = 0; }

        if (samplesSinceLastSYT > 0 && targetSampleRate_ > 0) {
            double expectedFwTicksForSamples = (static_cast<double>(samplesSinceLastSYT) / targetSampleRate_)
                                               * fwClock_NominalRateHz_;

            int32_t fwTicksBetweenSYTs = static_cast<int32_t>(timing.fwTimestamp - lastSYT_FWTimestamp_);
            const int32_t oneSecondTicks = static_cast<int32_t>(fwClock_NominalRateHz_);
            const int32_t halfSecondTicks = oneSecondTicks / 2;
            if (fwTicksBetweenSYTs < -halfSecondTicks) { fwTicksBetweenSYTs += oneSecondTicks; }
            else if (fwTicksBetweenSYTs > halfSecondTicks) { fwTicksBetweenSYTs -= oneSecondTicks; }

            double phaseErrorTicks = static_cast<double>(fwTicksBetweenSYTs) - expectedFwTicksForSamples;

            // Update PI Controller
            phaseErrorAccumulator_ += phaseErrorTicks * pllIntegralGain_;
            phaseErrorAccumulator_ = std::clamp(phaseErrorAccumulator_, integralMin_, integralMax_); // Clamp integral term

            double frequencyAdjustment = (phaseErrorTicks * pllProportionalGain_) + phaseErrorAccumulator_;

            // Update the ratio: Ratio = NominalRate / AdjustedRate = 1.0 / (1.0 + adjustment)
            // Or simpler: directly adjust ratio based on relative freq adjustment
            // We define currentRatio_ as DeviceRate/HostRate.
            // If device is fast, phaseError is positive, we want to *increase* ratio estimate.
            // If device is slow, phaseError is negative, we want to *decrease* ratio estimate.
            // Let's adjust ratio directly proportional to freq adjustment.
            // Adjustment represents fractional deviation from nominal FW ticks per host tick equivalent.
            // Need to relate phaseErrorTicks to host time elapsed.
            uint64_t hostTicksElapsedSinceSYT = currentHostTimeAbs - lastSYT_HostTimeAbs_;
            double hostSecondsElapsed = static_cast<double>(absolute_to_nanoseconds(hostTicksElapsedSinceSYT)) / NANOS_PER_SECOND;

            if (hostSecondsElapsed > 1e-9) { // Avoid division by zero
                // Freq Error (Hz deviation) = Phase Error (ticks) / Time Elapsed (s) / (Ticks/Sec)
                double freqErrorHz = (phaseErrorTicks / hostSecondsElapsed) / fwClock_NominalRateHz_;
                // Adjust ratio: ratio' = ratio * (1 + Kp*error + Ki*integral)
                // Simpler: Adjust the ratio based on the normalized frequency adjustment
                double adjustmentFactor = freqErrorHz * pllProportionalGain_ + phaseErrorAccumulator_ * pllIntegralGain_; // Simplified gain application

                double newRatio = currentRatio_ * (1.0 + adjustmentFactor); // Adjust multiplicatively

                // Clamp and smooth the ratio
                newRatio = std::clamp(newRatio, 0.999, 1.001); // +/- 1000 ppm clamp
                double alpha = 0.1; // Smoothing factor
                currentRatio_ = alpha * newRatio + (1.0 - alpha) * currentRatio_;

                if (logger_ && logger_->should_log(spdlog::level::debug)) {
                    logger_->debug("PLL SYT Update: Samples={}, FW Tick Delta={}, Expected Delta={:.1f}, PhaseError={:.1f}, FreqErrHz={:.4f}, NewRatio={:.8f}",
                                  samplesSinceLastSYT, fwTicksBetweenSYTs, expectedFwTicksForSamples, phaseErrorTicks, freqErrorHz, currentRatio_);
                }
            }

            // Update anchor points
            lastSYT_ = timing.syt;
            lastSYT_FWTimestamp_ = timing.fwTimestamp;
            lastSYT_AbsSampleIndex_ = timing.firstAbsSampleIndex;
            lastSYT_HostTimeAbs_ = currentHostTimeAbs; // Anchor to current host time

        } // else log trace (no samples or rate 0)
    } // else log trace (no valid consecutive SYT)

    // Update general state
    lastFwTimestamp_ = timing.fwTimestamp;
    lastHostTimeAbs_ = currentHostTimeAbs;
    lastPacketEndAbsSampleIndex_ = timing.firstAbsSampleIndex + timing.numSamplesInPacket;
}

uint64_t AudioClockPLL::getPresentationTimeNs(uint64_t absoluteSampleIndex) {
    if (!initialized_) {
        if (logger_) logger_->warn("PLL getPresentationTimeNs called before initialization!");
        return clock_gettime_nsec_np(CLOCK_UPTIME_RAW) + 5000000; // Return future time
    }

    // Use the last SYT arrival as the most reliable anchor point
    uint64_t anchorHostTimeAbs = lastSYT_HostTimeAbs_;
    uint64_t anchorAbsSampleIndex = lastSYT_AbsSampleIndex_;

    if (lastSYT_ == 0xFFFF || lastSYT_HostTimeAbs_ == 0) { // Fallback to initial if no SYT seen yet or anchor invalid
        anchorHostTimeAbs = initialHostTimeAbs_;
        anchorAbsSampleIndex = 0;
    }

    uint64_t samplesSinceAnchor = 0;
    if (absoluteSampleIndex >= anchorAbsSampleIndex) {
        samplesSinceAnchor = absoluteSampleIndex - anchorAbsSampleIndex;
    } else {
        if (logger_) logger_->warn("getPresentationTimeNs: Target sample index {} is before anchor {}", absoluteSampleIndex, anchorAbsSampleIndex);
        // Return anchor time? Or time slightly after?
        return absolute_to_nanoseconds(anchorHostTimeAbs);
    }

    // Calculate expected host ticks delta based on target rate and CURRENT ratio
    if (targetSampleRate_ <= 0) {
        if (logger_) logger_->error("PLL: Invalid targetSampleRate_ ({})", targetSampleRate_);
        return absolute_to_nanoseconds(anchorHostTimeAbs); // Cannot predict
    }
    // Host Ticks = Samples * (HostTicks/Sec) / (Samples/Sec * Ratio)
    // Ratio = DeviceRate / HostRate => Host Ticks = Samples * HostTicksPerSec / (DeviceRate) = Samples * HostTicksPerSample / Ratio
    double hostTicksPerSec = static_cast<double>(NANOS_PER_SECOND * timebaseInfo_.denom) / timebaseInfo_.numer;
    double hostTicksPerSample_Nominal = hostTicksPerSec / targetSampleRate_;

    // Adjust expected host ticks based on the current clock ratio estimate
    // If currentRatio_ > 1 (device faster), we need *fewer* host ticks per device sample.
    // If currentRatio_ < 1 (device slower), we need *more* host ticks per device sample.
    double estimatedHostTickDelta = static_cast<double>(samplesSinceAnchor) * hostTicksPerSample_Nominal / currentRatio_;

    uint64_t estimatedHostTimeAbs = anchorHostTimeAbs + static_cast<uint64_t>(estimatedHostTickDelta);

    uint64_t estimatedHostTimeNano = absolute_to_nanoseconds(estimatedHostTimeAbs);

    if (logger_ && logger_->should_log(spdlog::level::trace)) {
        uint64_t anchorHostNano = absolute_to_nanoseconds(anchorHostTimeAbs);
        logger_->trace("GetPresTime: AbsIdx={}, SamplesSinceAnchor={}, AnchorHostNano={}, DeltaTicks={:.0f}, Ratio={:.8f}, ResultHostAbs={}, ResultHostNano={}",
                      absoluteSampleIndex, samplesSinceAnchor, anchorHostNano, estimatedHostTickDelta, currentRatio_, estimatedHostTimeAbs, estimatedHostTimeNano);
    }

    return estimatedHostTimeNano;
}

// Helper: Convert absolute time to nanoseconds
uint64_t AudioClockPLL::absolute_to_nanoseconds(uint64_t mach_time) const {
    if (timebaseInfo_.denom == 0) return 0;
    // Use floating point for potentially better precision with large numbers
    long double conversion_factor = static_cast<long double>(timebaseInfo_.numer) / timebaseInfo_.denom;
    return static_cast<uint64_t>(mach_time * conversion_factor);
}

// Helper: Convert nanoseconds to absolute time
uint64_t AudioClockPLL::nanoseconds_to_absolute(uint64_t nano_time) const {
    if (timebaseInfo_.numer == 0) return 0;
    long double conversion_factor = static_cast<long double>(timebaseInfo_.denom) / timebaseInfo_.numer;
    return static_cast<uint64_t>(nano_time * conversion_factor);
}

} // namespace Isoch
} // namespace FWA