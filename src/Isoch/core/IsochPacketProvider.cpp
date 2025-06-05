// IsochPacketProvider.cpp - Direct SHM Implementation with Safety Margin
#include "Isoch/core/IsochPacketProvider.hpp"
#include <CoreServices/CoreServices.h>
#include <cstring>
#include <algorithm> 

// for min and max
#include <limits>

namespace FWA {
namespace Isoch {

IsochPacketProvider::IsochPacketProvider(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)),
      fillLevelHistogram_(HISTOGRAM_MAX_BINS), // CORRECTED: Default constructs atomics to 0
      fillLevelOverflowCount_(0)
{
    if (!logger_) {
        logger_ = spdlog::default_logger();
    }

    currentChunk_.invalidate();
    lastStatsTime_ = std::chrono::steady_clock::now();
    lastSafetyAdjustTime_ = std::chrono::steady_clock::now();
    lastHistogramLogTime_ = std::chrono::steady_clock::now();

    if (logger_) {
        logger_->debug("IsochPacketProvider created in direct SHM mode with safety margin support");
        logger_->debug("IsochPacketProvider histogram initialized ({} bins)", HISTOGRAM_MAX_BINS);
    }

    reset();
}

IsochPacketProvider::~IsochPacketProvider() {
    unbindSharedMemory();
    if (logger_) {
        logger_->debug("IsochPacketProvider destroyed");
    }
}

bool IsochPacketProvider::bindSharedMemory(RTShmRing::ControlBlock_POD* controlBlock,
                                           RTShmRing::AudioChunk_POD* ringArray) {
    std::lock_guard<std::mutex> lock(bindMutex_);  // Thread safety

    if (!controlBlock || !ringArray) {
        if (logger_) {
            logger_->error("bindSharedMemory: Invalid parameters (null pointers)");
        }
        return false;
    }
    
    // Validate SHM format before binding
    if (!RTShmRing::ValidateFormat(*controlBlock)) {
        formatValidationErrors_++;
        if (logger_) {
            logger_->error("bindSharedMemory: SHM format validation failed - ABI: {}, sampleRate: {}, channels: {}, bytesPerFrame: {}",
                          controlBlock->abiVersion, controlBlock->sampleRateHz, 
                          controlBlock->channelCount, controlBlock->bytesPerFrame);
        }
        return false;
    }
    
    // Unbind any existing SHM first
    if (shmControlBlock_) {
        unbindSharedMemory();
    }
    
    shmControlBlock_ = controlBlock;
    shmRingArray_ = ringArray;
    
    // *** Reset indices for a clean start ***
    RTShmRing::WriteIndexProxy(*controlBlock).store(0, std::memory_order_relaxed);
    RTShmRing::ReadIndexProxy(*controlBlock).store(0, std::memory_order_relaxed);
    
    controlBlock->streamActive = 0;        // stays idle until startAudioStreams()
    
    // Clear any leftover chunk-cache or stats
    currentChunk_.invalidate();
    reset();  // Reset bytesConsumed_, popCount_, etc. for fresh state
    
    if (logger_) {
        logger_->info("SHM bound successfully: {} Hz, {} channels, {} bytes/frame, capacity: {}, initial safety margin: {}",
                     controlBlock->sampleRateHz, controlBlock->channelCount, 
                     controlBlock->bytesPerFrame, controlBlock->capacity, safetyMarginChunks_.load());
    }
    
    return true;
}

void IsochPacketProvider::unbindSharedMemory() {
    std::lock_guard<std::mutex> lock(bindMutex_);  // Thread safety
    if (shmControlBlock_) {
        if (logger_) {
            logger_->debug("Unbinding shared memory");
        }
        shmControlBlock_ = nullptr;
        shmRingArray_ = nullptr;
        currentChunk_.invalidate();
    }
}

// Legacy interface - not used in direct SHM mode
bool IsochPacketProvider::pushAudioData(const void* buffer, size_t bufferSizeInBytes) {
    if (logger_) {
        logger_->warn("pushAudioData called in direct SHM mode - operation not supported");
    }
    return false;
}

// PreparedPacketData IsochPacketProvider::fillPacketData(
//     uint8_t* targetBuffer,
//     size_t targetBufferSize,
//     const TransmitPacketInfo& info)
// {
//     auto startTime = std::chrono::high_resolution_clock::now();
    
//     PreparedPacketData result;
//     result.dataPtr       = targetBuffer;
//     result.dataLength    = 0;
//     result.generatedSilence = true;

//     // === 0. Early parameter checks ===
//     if (!targetBuffer || targetBufferSize == 0) {
//         if (logger_) {
//             logger_->error("fillPacketData: Invalid target buffer or size");
//         }
//         return result;
//     }

//     // === 1. Check for any underruns logged in SHM and clear the counter ===
//     if (shmControlBlock_) {
//         // Atomically read & clear the underrun counter
//         uint32_t underruns = RTShmRing::UnderrunCountProxy(*shmControlBlock_)
//                                 .exchange(0, std::memory_order_relaxed);
//         if (underruns && logger_) {
//             // silence for now - too much noise
//            logger_->warn("Audio underrun x{}", underruns);
//         }
//     }

//     // === 2. Check SHM binding ===
//     if (!shmControlBlock_ || !shmRingArray_) {
//         if (logger_) {
//             logger_->error("fillPacketData: SHM not bound; filling silence");
//         }
//         std::memset(targetBuffer, 0, targetBufferSize);
//         result.dataLength = targetBufferSize;
//         return result;
//     }

//     // === 3. Validate SHM format once in a while ===
//     if (!validateShmFormat()) {
//         formatValidationErrors_++;
//         if (logger_) {
//             logger_->error("fillPacketData: SHM format invalid; filling silence");
//         }
//         std::memset(targetBuffer, 0, targetBufferSize);
//         result.dataLength = targetBufferSize;
//         return result;
//     }

//     // === NEW 4. Safety Margin Check - Proactive Silence Generation ===
//     if (!hasMinimumFillLevel()) {
//         generateProactiveSilence(targetBuffer, targetBufferSize, result, info);
        
//         // Update performance timing and return early
//         auto endTime = std::chrono::high_resolution_clock::now();
//         auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);
//         fillPacketCallCount_++;
//         totalFillPacketTimeNs_ += duration.count();
        
//         return result;
//     }

//     // === NEW 5. Adaptive Safety Margin Adjustment ===
//     adjustSafetyMargin();

//     // === 6. Copy from SHM ring into targetBuffer (PRESERVED EXISTING LOGIC) ===
//     uint8_t* writePtr = targetBuffer;
//     size_t   remaining = targetBufferSize;
//     bool     gotAllData = true;

//     // Optional periodic stats logging
//     static thread_local uint32_t statsTicks = 0;
//     if (++statsTicks == 8000) { // ~1 s at 8 kHz IRQ rate
//         auto wr = RTShmRing::WriteIndexProxy(*shmControlBlock_)
//                       .load(std::memory_order_relaxed);
//         auto rd = RTShmRing::ReadIndexProxy(*shmControlBlock_)
//                       .load(std::memory_order_relaxed);
//         if (logger_) {
//             logger_->info("POP rd={} wr={} used={}  curChunkLeft={}B  copyThisCall={}B  safetyMargin={}",
//                           rd, wr, wr - rd,
//                           currentChunk_.remainingBytes(),
//                           targetBufferSize,
//                           safetyMarginChunks_.load());
//         }
//         statsTicks = 0;
//     }

//     while (remaining > 0) {
//         // If current chunk is exhausted, try to pop next one
//         if (currentChunk_.remainingBytes() == 0) {
//             if (!popNextChunk()) {
//                 // Underrun: no more valid audio chunks
//                 gotAllData = false;
//                 break;
//             }
//         }

//         uint32_t availableInChunk = currentChunk_.remainingBytes();
//         size_t   toCopy           = std::min(remaining, static_cast<size_t>(availableInChunk));

//         // Only count as "partial" if we're consuming < full chunk on first access
//         bool wasPartialConsumption = (currentChunk_.consumedBytes == 0 &&
//                                       toCopy < currentChunk_.totalBytes);

//         std::memcpy(
//             writePtr,
//             currentChunk_.audioDataPtr + currentChunk_.consumedBytes,
//             toCopy
//         );

//         writePtr += toCopy;
//         remaining -= toCopy;
//         currentChunk_.consumedBytes += static_cast<uint32_t>(toCopy);
//         totalBytesConsumed_ += toCopy;

//         if (wasPartialConsumption) {
//             partialChunkConsumptions_++;
//         }
//     }

//     // === 7. If we got a full buffer, convert in-place to AM824; else zero-fill & note underrun ===
//     if (gotAllData && remaining == 0) {
//         formatToAM824InPlace(targetBuffer, targetBufferSize);
//         result.generatedSilence = false;
//     } else {
//         // Partial or complete underrun: fill remainder with silence
//         if (remaining > 0) {
//             std::memset(writePtr, 0, remaining);
//         }
//         handleUnderrun(info);
//     }

//     result.dataLength = targetBufferSize;

//     // === 8. Update performance timing ===
//     auto endTime = std::chrono::high_resolution_clock::now();
//     auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);
//     fillPacketCallCount_++;
//     totalFillPacketTimeNs_ += duration.count();

//     return result;
// }

// MINIMAL - test for underrun and fill silence
PreparedPacketData IsochPacketProvider::fillPacketData(
    uint8_t*   targetBuffer,
    size_t     targetBufferSize,
    const TransmitPacketInfo& info)
{
    // --- Start: Performance Timing (Optional, but good to keep) ---
    auto startTime = std::chrono::high_resolution_clock::now();
    // --- End: Performance Timing ---

    // --- TELEMETRY: Record current fill level ---
    if (shmControlBlock_) { // Ensure control block is valid
        auto wr = RTShmRing::WriteIndexProxy(*shmControlBlock_).load(std::memory_order_acquire);
        auto rd = RTShmRing::ReadIndexProxy(*shmControlBlock_).load(std::memory_order_relaxed);
        uint64_t currentFill = (wr >= rd) ? (wr - rd) : 0; // Number of used/filled chunks

        if (currentFill < HISTOGRAM_MAX_BINS) {
            fillLevelHistogram_[static_cast<size_t>(currentFill)]
                .fetch_add(1, std::memory_order_relaxed);
        } else {
            fillLevelOverflowCount_.fetch_add(1, std::memory_order_relaxed);
        }

        // Periodically log the histogram
        auto now = std::chrono::steady_clock::now();
        if (now - lastHistogramLogTime_ > HISTOGRAM_LOG_INTERVAL) {
            logFillLevelHistogram(); // Call helper to print
            lastHistogramLogTime_ = now;
        }
    }

    // --- END TELEMETRY ---

     PreparedPacketData result;
    result.dataPtr       = targetBuffer;
    result.dataLength    = targetBufferSize; // We always "fill" the target buffer
    result.generatedSilence = true;     // Assume silence until proven otherwise

    if (!targetBuffer || targetBufferSize == 0 || !shmControlBlock_ || !shmRingArray_) {
        if (logger_) logger_->error("fillPacketData: Preconditions failed (null buffers/SHM not bound). Generating silence.");
        std::memset(targetBuffer, 0, targetBufferSize);
        // No need to call handleUnderrun here, it's a setup issue.
        return result;
    }

    // --- LEAKY BUCKET GUARD ---
    if (!hasSufficientDataForPop()) {
        // Not enough data in SHM above our minimum threshold.
        // Generate silence for this FireWire packet, do NOT advance SHM read pointer.
        std::memset(targetBuffer, 0, targetBufferSize);
        result.generatedSilence = true; // Already true, but for clarity
        
        // This is not a "true" SHM underrun (rd == wr), but a proactive hold.
        // You might want a separate counter for "proactive silence" vs "true underrun".
        // For now, let's use handleUnderrun but be aware of its meaning.
        // Or, create a new logging mechanism for "proactive_silence_due_to_low_fill"
        // safetyMarginHolds_++; // If you had such a counter
        if (logger_) {
            static thread_local uint32_t holdLog = 0;
            if ((holdLog++ % 1000) == 0) { // Throttle this log
                 auto wr = RTShmRing::WriteIndexProxy(*shmControlBlock_).load(std::memory_order_acquire);
                 auto rd = RTShmRing::ReadIndexProxy(*shmControlBlock_).load(std::memory_order_relaxed);
                 logger_->debug("fillPacketData: Leaky bucket hold. Available: {}, Needed > {}. Generating silence.", 
                               (wr >= rd ? wr-rd : 0), kMinChunksToPop);
            }
        }
        // Important: We format even silence into AM824 NO_DATA, which still carries SYT
        formatToAM824InPlace(targetBuffer, targetBufferSize); // Ensures FDF=0xFF
        return result;
    }
    // --- END LEAKY BUCKET GUARD ---

    // If we are here, hasSufficientDataForPop() was true.
    // Attempt to get data.
    // If current cached chunk is exhausted, try to pop a new one.
    if (remainingBytesInCurrentChunk() == 0) {
        if (!popNextChunk()) {
            // popNextChunk failed. This means RTShmRing::pop() found rd == wr (true empty)
            // OR if popNextChunk *still* had its own internal hasMinimumFillLevel check
            // (which it shouldn't in this simplified model), it could fail there.
            // Assuming popNextChunk now only fails on true empty.
            std::memset(targetBuffer, 0, targetBufferSize);
            handleUnderrun(info); // Log as a true SHM underrun
            formatToAM824InPlace(targetBuffer, targetBufferSize); // Format silence
            return result;
        }
    }

    // We have a valid chunk (or part of it) in currentChunk_
    size_t remaining_in_chunk_size_t = static_cast<size_t>(remainingBytesInCurrentChunk()); // Cast to size_t

    size_t toCopy = std::min(remaining_in_chunk_size_t, targetBufferSize); 
    const std::byte* srcDataForMemcpy = currentChunkReadPtr();

    if (!srcDataForMemcpy || toCopy == 0) { // Should ideally not happen if remainingBytes > 0
        logger_->error("fillPacketData: Logic error or empty chunk after successful pop. Generating silence.");
        std::memset(targetBuffer, 0, targetBufferSize);
        handleUnderrun(info); // Treat as underrun
        formatToAM824InPlace(targetBuffer, targetBufferSize);
        return result;
    }

    std::memcpy(targetBuffer, srcDataForMemcpy, toCopy);
    currentChunk_.consumedBytes += static_cast<uint32_t>(toCopy);
    totalBytesConsumed_ += toCopy;

    if (toCopy < targetBufferSize) {
        // Current SHM chunk didn't fill the entire FireWire packet.
        std::memset(targetBuffer + toCopy, 0, targetBufferSize - toCopy);
        handleUnderrun(info); // Log this partial fill as an underrun event
        result.generatedSilence = true; // Since part of it is silence
    } else {
        result.generatedSilence = false; // Full data packet
    }

    formatToAM824InPlace(targetBuffer, targetBufferSize);
    
    return result;
}

// without safety margin
bool IsochPacketProvider::popNextChunk() {
    if (!shmControlBlock_ || !shmRingArray_) {
        currentChunk_.invalidate(); // Ensure cache is invalid
        return false;
    }

 
    AudioTimeStamp timestamp;
    uint32_t dataBytes;
    const std::byte* audioPtr;

    if (!RTShmRing::pop(*shmControlBlock_, shmRingArray_, timestamp, dataBytes, audioPtr)) {
        // This is now a "true" underrun from the ring buffer's perspective (rd == wr).
        // The UnderrunCountProxy is incremented inside RTShmRing::pop.
        currentChunk_.invalidate();
        return false;
    }

    // Cache the chunk data
    currentChunk_.timeStamp = timestamp;
    currentChunk_.totalBytes = dataBytes;
    currentChunk_.audioDataPtr = audioPtr;
    currentChunk_.consumedBytes = 0;
    currentChunk_.valid = true;
    
    shmPopCount_++;
    return true;
}

// bool IsochPacketProvider::popNextChunk() {
//     if (!shmControlBlock_ || !shmRingArray_) {
//         return false;
//     }

//     // === NEW: Secondary Safety Check (Defense in Depth) ===
//     if (!hasMinimumFillLevel()) {
//         if (logger_) {
//             // Throttled logging to avoid spam
//             static thread_local uint32_t logCounter = 0;
//             if ((logCounter++ % 1000) == 0) {
//                 logger_->debug("popNextChunk called when below safety margin (count: {})", logCounter);
//             }
//         }
//         currentChunk_.invalidate();
//         return false;
//     }

//     AudioTimeStamp timestamp;
//     uint32_t dataBytes;
//     const std::byte* audioPtr;

//     // Use your zero-copy pop API
//     if (!RTShmRing::pop(*shmControlBlock_, shmRingArray_, timestamp, dataBytes, audioPtr)) {
//         shmUnderrunCount_++;
//         currentChunk_.invalidate();
//         return false;
//     }

//     // Cache the chunk data
//     currentChunk_.timeStamp = timestamp;
//     currentChunk_.totalBytes = dataBytes;
//     currentChunk_.audioDataPtr = audioPtr;
//     currentChunk_.consumedBytes = 0;
//     currentChunk_.valid = true;
    
//     shmPopCount_++;
//     return true;
// }


// simplified version of isReadyForStreaming - testing
bool IsochPacketProvider::isReadyForStreaming() const {
    if (!shmControlBlock_) return false;
    return StreamActiveProxy(*shmControlBlock_).load(std::memory_order_acquire) == 1;
}

// bool IsochPacketProvider::isReadyForStreaming() const {
//     if (!shmControlBlock_) {
//         return false;
//     }
    
//     auto wr = RTShmRing::WriteIndexProxy(*shmControlBlock_).load(std::memory_order_acquire);
//     auto rd = RTShmRing::ReadIndexProxy(*shmControlBlock_).load(std::memory_order_acquire);
    
//     if (shmControlBlock_->capacity == 0) {
//         return false;
//     }
    
//     // === NEW: Align startup condition with safety margin ===
//     uint64_t available = (wr >= rd) ? (wr - rd) : 0;
//     uint32_t safetyMargin = safetyMarginChunks_.load();
//     uint32_t startupBuffer = safetyMargin + 2; // Safety margin plus small priming buffer
    
//     return available > startupBuffer;
// }

void IsochPacketProvider::reset() {
    currentChunk_.invalidate();
    totalBytesConsumed_ = 0;
    shmPopCount_ = 0;
    shmUnderrunCount_ = 0;
    formatValidationErrors_ = 0;
    partialChunkConsumptions_ = 0;
    fillPacketCallCount_ = 0;
    totalFillPacketTimeNs_ = 0;
    lastStatsTime_ = std::chrono::steady_clock::now();

    // === NEW: Reset safety margin state ===
    safetyMarginChunks_ = 4; // Reset to default
    safetyMarginHolds_ = 0;
    safetyMarginAdjustments_ = 0;
    lastSafetyAdjustTime_ = std::chrono::steady_clock::now();

    // === NEW: Reset histogram data ===
    resetFillLevelHistogram();
    lastHistogramLogTime_ = std::chrono::steady_clock::now();

    if (logger_) {
        logger_->info("IsochPacketProvider reset, including fill level histogram.");
    }
}

void IsochPacketProvider::handleUnderrun(const TransmitPacketInfo& info) {
    shmUnderrunCount_++;
    
    // Log periodically to avoid spam
    if ((shmUnderrunCount_ % 100) == 1) {
        uint32_t fillLevel = getCurrentShmFillLevel();
        uint32_t safetyMargin = safetyMarginChunks_.load();
        if (logger_) {
            logger_->warn("SHM underrun detected at Seg={}, Pkt={}, AbsPkt={}. Total Count={}, SHM Fill={}%, Safety Margin={}",
                         info.segmentIndex, info.packetIndexInGroup, info.absolutePacketIndex, 
                         shmUnderrunCount_.load(), fillLevel, safetyMargin);
        }
    }
}

// void IsochPacketProvider::formatToAM824InPlace(uint8_t* buffer, size_t bufferSize) const {
//     int32_t* samplesPtr = reinterpret_cast<int32_t*>(buffer);
//     size_t numSamples = bufferSize / sizeof(int32_t);

//     for (size_t i = 0; i < numSamples; ++i) {
//         int32_t sample = samplesPtr[i];
//         sample &= 0x00FFFFFF;  // Mask to 24-bit
//         uint32_t am824Sample = (AM824_LABEL << LABEL_SHIFT) | sample;
//         samplesPtr[i] = OSSwapHostToBigInt32(am824Sample);
//     }
// }

void IsochPacketProvider::formatToAM824InPlace(uint8_t* buffer, size_t bufferSize) const {
    // Assuming buffer contains interleaved SInt32 samples,
    // where each SInt32 holds an MSB-aligned 24-bit audio sample.
    int32_t* samplesPtr = reinterpret_cast<int32_t*>(buffer);
    size_t numInt32Samples = bufferSize / sizeof(int32_t); // Total L,R,L,R... samples

    for (size_t i = 0; i < numInt32Samples; ++i) {
        int32_t msb_aligned_sint24_in_sint32 = samplesPtr[i];

        // 1. Extract the 24 significant MSB bits by right-shifting.
        //    Cast to uint32_t first to ensure logical right shift (zeros fill from left)
        //    if msb_aligned_sint24_in_sint32 was negative.
        //    This results in a 24-bit value, effectively LSB-aligned in a uint32_t.
        uint32_t audio_data_24bit = (static_cast<uint32_t>(msb_aligned_sint24_in_sint32) >> 8);
        
        // 2. Mask to ensure it's purely 24-bit (good practice, though the shift should achieve this
        //    if the lower 8 bits of the original msb_aligned_sint24_in_sint32 were indeed zero
        //    or correctly sign-extended).
        audio_data_24bit &= 0x00FFFFFF;

        // 3. Construct the 32-bit AM824 word in host endian:
        //    [AM824_LABEL (8 MSBs)] [audio_data_24bit (24 LSBs)]
        uint32_t am824_word_host_endian = (AM824_LABEL << 24) | audio_data_24bit;
        // Note: If LABEL_SHIFT was 24, your original `(AM824_LABEL << LABEL_SHIFT) | sample_lsb_24bit`
        // also achieved this structure, but `sample_lsb_24bit` must be correctly derived.

        // 4. Convert the full AM824 word to Big Endian for FireWire transmission
        //    and write it back in place.
        samplesPtr[i] = OSSwapHostToBigInt32(am824_word_host_endian);
        // Or, using C++23:
        // samplesPtr[i] = static_cast<int32_t>(std::byteswap(am824_word_host_endian));
    }
}

uint32_t IsochPacketProvider::getCurrentShmFillLevel() const {
    if (!shmControlBlock_) {
        return 0;
    }
    
    auto wr = RTShmRing::WriteIndexProxy(*shmControlBlock_).load(std::memory_order_acquire);
    auto rd = RTShmRing::ReadIndexProxy(*shmControlBlock_).load(std::memory_order_acquire);
    
    if (shmControlBlock_->capacity == 0) {
        return 0;
    }
    
    uint64_t used = (wr >= rd) ? (wr - rd) : 0;
    return static_cast<uint32_t>((used * 100) / shmControlBlock_->capacity);
}

bool IsochPacketProvider::validateShmFormat() const {
    if (!shmControlBlock_) {
        return false;
    }
    
    return RTShmRing::ValidateFormat(*shmControlBlock_);
}

// === NEW: Safety Margin Implementation ===

bool IsochPacketProvider::hasMinimumFillLevel() const {
    if (!shmControlBlock_) {
        return false;
    }
    
    auto wr = RTShmRing::WriteIndexProxy(*shmControlBlock_).load(std::memory_order_acquire);
    auto rd = RTShmRing::ReadIndexProxy(*shmControlBlock_).load(std::memory_order_relaxed);
    
    uint64_t availableChunks = (wr >= rd) ? (wr - rd) : 0;
    uint32_t safetyMargin = safetyMarginChunks_.load(std::memory_order_relaxed);
    
    // Core safety logic: we need MORE than safety margin available to consider it safe to pop
    return availableChunks > safetyMargin;
}

void IsochPacketProvider::adjustSafetyMargin() {
    if (!shmControlBlock_) {
        return;
    }
    
    // Only adjust periodically to avoid oscillation
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSafetyAdjustTime_);
    if (elapsed.count() < SAFETY_ADJUST_INTERVAL_MS) {
        return;
    }
    lastSafetyAdjustTime_ = now;
    
    auto wr = RTShmRing::WriteIndexProxy(*shmControlBlock_).load(std::memory_order_acquire);
    auto rd = RTShmRing::ReadIndexProxy(*shmControlBlock_).load(std::memory_order_relaxed);
    
    uint64_t currentFillChunks = (wr >= rd) ? (wr - rd) : 0;
    uint32_t capacityChunks = shmControlBlock_->capacity;
    
    if (capacityChunks == 0) {
        return; // Avoid division by zero
    }
    
    uint32_t highWaterMark = (capacityChunks * HIGH_WATER_MARK_PERCENT) / 100;
    uint32_t lowWaterMark = (capacityChunks * LOW_WATER_MARK_PERCENT) / 100;
    uint32_t maxSafetyMargin = (capacityChunks * MAX_SAFETY_MARGIN_PERCENT) / 100;
    
    uint32_t currentSafety = safetyMarginChunks_.load();
    uint32_t newSafety = currentSafety;
    
    // Increase safety margin if buffer is consistently full
    if (currentFillChunks > highWaterMark && currentSafety < maxSafetyMargin) {
        newSafety = std::min(maxSafetyMargin, currentSafety + 1);
    }
    // Decrease safety margin if buffer is running low (but not too low)
    else if (currentFillChunks < lowWaterMark && currentSafety > MIN_SAFETY_MARGIN) {
        newSafety = std::max(MIN_SAFETY_MARGIN, currentSafety - 1);
    }
    
    if (newSafety != currentSafety) {
        safetyMarginChunks_.store(newSafety);
        safetyMarginAdjustments_++;
        
        if (logger_) {
            // Throttled logging
            static thread_local uint32_t adjustLogCounter = 0;
            if ((adjustLogCounter++ % 10) == 0) {
                logger_->info("Adjusted safety margin: {} -> {} chunks (fill: {}/{})", 
                             currentSafety, newSafety, currentFillChunks, capacityChunks);
            }
        }
    }
}

void IsochPacketProvider::generateProactiveSilence(uint8_t* targetBuffer, size_t targetBufferSize, 
                                                   PreparedPacketData& result, const TransmitPacketInfo& info) {
    safetyMarginHolds_++;
    
    // Generate silence
    std::memset(targetBuffer, 0, targetBufferSize);
    result.generatedSilence = true;
    result.dataLength = targetBufferSize;
    
    // Throttled logging to avoid spam
    static thread_local uint32_t holdLogCounter = 0;
    if ((holdLogCounter++ % 1000) == 0) {
        uint32_t fillLevel = getCurrentShmFillLevel();
        uint32_t safetyMargin = safetyMarginChunks_.load();
        if (logger_) {
            logger_->debug("Safety margin hold: Seg={}, Pkt={}, Fill={}%, Safety={} chunks (count: {})",
                          info.segmentIndex, info.packetIndexInGroup, fillLevel, safetyMargin, holdLogCounter);
        }
    }
}

void IsochPacketProvider::setSafetyMarginChunks(uint32_t chunks) {
    uint32_t clampedChunks = std::max(MIN_SAFETY_MARGIN, chunks);
    
    // Optionally clamp to maximum based on current capacity
    if (shmControlBlock_ && shmControlBlock_->capacity > 0) {
        uint32_t maxAllowed = (shmControlBlock_->capacity * MAX_SAFETY_MARGIN_PERCENT) / 100;
        clampedChunks = std::min(clampedChunks, maxAllowed);
    }
    
    safetyMarginChunks_.store(clampedChunks);
    
    if (logger_) {
        logger_->info("Safety margin set to {} chunks", clampedChunks);
    }
}

IsochPacketProvider::DiagnosticStats IsochPacketProvider::getDiagnostics() const {
    DiagnosticStats stats;
    stats.totalBytesConsumed = totalBytesConsumed_.load();
    stats.shmPopCount = shmPopCount_.load();
    stats.shmUnderrunCount = shmUnderrunCount_.load();
    stats.formatValidationErrors = formatValidationErrors_.load();
    stats.partialChunkConsumptions = partialChunkConsumptions_.load();
    stats.currentShmFillPercent = getCurrentShmFillLevel();
    
    // === NEW: Safety margin diagnostics ===
    stats.safetyMarginHolds = safetyMarginHolds_.load();
    stats.currentSafetyMarginChunks = safetyMarginChunks_.load();
    stats.safetyMarginAdjustments = safetyMarginAdjustments_.load();
    
    // Calculate average fill packet duration
    uint64_t totalCalls = fillPacketCallCount_.load();
    uint64_t totalTimeNs = totalFillPacketTimeNs_.load();
    stats.avgFillPacketDurationUs = totalCalls > 0 ? 
        (static_cast<double>(totalTimeNs) / static_cast<double>(totalCalls)) / 1000.0 : 0.0;
    
    return stats;
}

void IsochPacketProvider::resetDiagnostics() {
    totalBytesConsumed_ = 0;
    shmPopCount_ = 0;
    shmUnderrunCount_ = 0;
    formatValidationErrors_ = 0;
    partialChunkConsumptions_ = 0;
    fillPacketCallCount_ = 0;
    totalFillPacketTimeNs_ = 0;
    lastStatsTime_ = std::chrono::steady_clock::now();
    
    // === NEW: Reset safety margin diagnostics ===
    safetyMarginHolds_ = 0;
    safetyMarginAdjustments_ = 0;
    lastSafetyAdjustTime_ = std::chrono::steady_clock::now();
    
    if (logger_) {
        logger_->debug("IsochPacketProvider diagnostics reset");
    }
}

// IsochPacketProvider.cpp
std::map<uint32_t, uint64_t> IsochPacketProvider::getFillLevelHistogram() const {
    std::lock_guard<std::mutex> lock(histogramMutex_);
    std::map<uint32_t, uint64_t> histoMap;
    for (uint32_t i = 0; i < fillLevelHistogram_.size(); ++i) {
        uint64_t count = fillLevelHistogram_[i].load(std::memory_order_relaxed);
        if (count > 0) {
            histoMap[i] = count;
        }
    }
    uint64_t overflow = fillLevelOverflowCount_.load(std::memory_order_relaxed);
    if (overflow > 0) {
        histoMap[HISTOGRAM_MAX_BINS] = overflow;
    }
    return histoMap;
}

void IsochPacketProvider::resetFillLevelHistogram() {
    std::lock_guard<std::mutex> lock(histogramMutex_);
    for (auto& count : fillLevelHistogram_) {
        count.store(0, std::memory_order_relaxed);
    }
    fillLevelOverflowCount_.store(0, std::memory_order_relaxed);
    if (logger_) {
        logger_->debug("Fill level histogram reset.");
    }
}

void IsochPacketProvider::logFillLevelHistogram() const {
 // NOP - daemon queries histogram...
}


// Helper method for the leaky bucket check
bool IsochPacketProvider::hasSufficientDataForPop() const {
    if (!shmControlBlock_) {
        return false; // Not bound, so definitely not sufficient
    }
    // Check if the stream is even active from the driver's perspective
    if (StreamActiveProxy(*shmControlBlock_).load(std::memory_order_acquire) == 0) {
        // If you want to log this specific "not active yet" case:
        // static thread_local uint32_t notActiveLog = 0;
        // if ((notActiveLog++ % 8000) == 0 && logger_) { // Log ~once/sec
        //     logger_->debug("hasSufficientDataForPop: Stream not marked active by driver yet.");
        // }
        return false; // Don't pop if driver hasn't signaled active
    }

    auto wr = RTShmRing::WriteIndexProxy(*shmControlBlock_).load(std::memory_order_acquire);
    auto rd = RTShmRing::ReadIndexProxy(*shmControlBlock_).load(std::memory_order_relaxed);
    uint64_t availableChunks = (wr >= rd) ? (wr - rd) : 0;

    // The core logic: are there MORE chunks available than our minimum threshold?
    // If availableChunks is 5, and kMinChunksToPop is 4, (5 > 4) is true, so we can pop.
    // If availableChunks is 4, and kMinChunksToPop is 4, (4 > 4) is false, so we hold.
    return availableChunks > kMinChunksToPop;
}

} // namespace Isoch
} // namespace FWA