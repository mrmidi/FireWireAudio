// IsochPacketProvider.cpp - Direct SHM Implementation
#include "Isoch/core/IsochPacketProvider.hpp"
#include <CoreServices/CoreServices.h>
#include <cstring>
#include <algorithm>

namespace FWA {
namespace Isoch {

IsochPacketProvider::IsochPacketProvider(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger))
{
    if (!logger_) {
        logger_ = spdlog::default_logger();
    }
    
    currentChunk_.invalidate();
    lastStatsTime_ = std::chrono::steady_clock::now();
    
    if (logger_) {
        logger_->debug("IsochPacketProvider created in direct SHM mode");
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
    // Clear any leftover chunk-cache or stats
    currentChunk_.invalidate();
    reset();  // Reset bytesConsumed_, popCount_, etc. for fresh state
    
    if (logger_) {
        logger_->info("SHM bound successfully: {} Hz, {} channels, {} bytes/frame, capacity: {}",
                     controlBlock->sampleRateHz, controlBlock->channelCount, 
                     controlBlock->bytesPerFrame, controlBlock->capacity);
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

PreparedPacketData IsochPacketProvider::fillPacketData(
    uint8_t* targetBuffer,
    size_t targetBufferSize,
    const TransmitPacketInfo& info)
{
    auto startTime = std::chrono::high_resolution_clock::now();
    
    PreparedPacketData result;
    result.dataPtr = targetBuffer;
    result.dataLength = 0;
    result.generatedSilence = true;

    // Validate parameters
    if (!targetBuffer || targetBufferSize == 0) {
        if (logger_) {
            logger_->error("fillPacketData: Invalid target buffer");
        }
        return result;
    }

    // Check SHM binding
    if (!shmControlBlock_ || !shmRingArray_) {
        if (logger_) {
            logger_->error("fillPacketData: SHM not bound");
        }
        std::memset(targetBuffer, 0, targetBufferSize);
        result.dataLength = targetBufferSize;
        return result;
    }

    // Validate SHM format periodically
    if (!validateShmFormat()) {
        formatValidationErrors_++;
        std::memset(targetBuffer, 0, targetBufferSize);
        result.dataLength = targetBufferSize;
        return result;
    }

    uint8_t* writePtr = targetBuffer;
    size_t remaining = targetBufferSize;
    bool gotAllData = true;

    // Fill target buffer from SHM chunks
    while (remaining > 0) {
        // Ensure we have a current chunk with data
        if (currentChunk_.remainingBytes() == 0) {
            if (!popNextChunk()) {
                // Underrun - no more data available
                gotAllData = false;
                break;
            }
        }

        // Copy as much as possible from current chunk
        uint32_t availableInChunk = currentChunk_.remainingBytes();
        size_t toCopy = std::min(remaining, static_cast<size_t>(availableInChunk));

        // FIXED: Only count as partial if we're consuming part of chunk
        bool wasPartialConsumption = (currentChunk_.consumedBytes == 0 && 
                                      toCopy < currentChunk_.totalBytes);

        std::memcpy(writePtr, 
                   currentChunk_.audioDataPtr + currentChunk_.consumedBytes,
                   toCopy);

        writePtr += toCopy;
        remaining -= toCopy;
        currentChunk_.consumedBytes += static_cast<uint32_t>(toCopy);
        totalBytesConsumed_ += toCopy;

        // FIXED: Only increment counter for actual partial consumption
        if (wasPartialConsumption) {
            partialChunkConsumptions_++;
        }
    }

    if (gotAllData && remaining == 0) {
        // Successfully filled buffer - format to AM824 in place
        formatToAM824InPlace(targetBuffer, targetBufferSize);
        result.generatedSilence = false;
    } else {
        // Partial underrun - fill remainder with silence
        if (remaining > 0) {
            std::memset(writePtr, 0, remaining);
        }
        handleUnderrun(info);
    }
    
    result.dataLength = targetBufferSize;

    // Update performance timing
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);
    fillPacketCallCount_++;
    totalFillPacketTimeNs_ += duration.count();

    return result;
}

bool IsochPacketProvider::popNextChunk() {
    if (!shmControlBlock_ || !shmRingArray_) {
        return false;
    }

    AudioTimeStamp timestamp;
    uint32_t dataBytes;
    const std::byte* audioPtr;

    // Use your zero-copy pop API
    if (!RTShmRing::pop(*shmControlBlock_, shmRingArray_, timestamp, dataBytes, audioPtr)) {
        shmUnderrunCount_++;
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

bool IsochPacketProvider::isReadyForStreaming() const {
    if (!shmControlBlock_) {
        return false;
    }
    
    uint32_t fillLevel = getCurrentShmFillLevel();
    return fillLevel >= INITIAL_FILL_TARGET_PERCENT;
}

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
    
    if (logger_) {
        logger_->info("IsochPacketProvider reset (direct SHM mode)");
    }
}

void IsochPacketProvider::handleUnderrun(const TransmitPacketInfo& info) {
    shmUnderrunCount_++;
    
    // Log periodically to avoid spam
    if ((shmUnderrunCount_ % 100) == 1) {
        uint32_t fillLevel = getCurrentShmFillLevel();
        if (logger_) {
            logger_->warn("SHM underrun detected at Seg={}, Pkt={}, AbsPkt={}. Total Count={}, SHM Fill={}%",
                         info.segmentIndex, info.packetIndexInGroup, info.absolutePacketIndex, 
                         shmUnderrunCount_.load(), fillLevel);
        }
    }
}

void IsochPacketProvider::formatToAM824InPlace(uint8_t* buffer, size_t bufferSize) const {
    int32_t* samplesPtr = reinterpret_cast<int32_t*>(buffer);
    size_t numSamples = bufferSize / sizeof(int32_t);

    for (size_t i = 0; i < numSamples; ++i) {
        int32_t sample = samplesPtr[i];
        sample &= 0x00FFFFFF;  // Mask to 24-bit
        uint32_t am824Sample = (AM824_LABEL << LABEL_SHIFT) | sample;
        samplesPtr[i] = OSSwapHostToBigInt32(am824Sample);
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

IsochPacketProvider::DiagnosticStats IsochPacketProvider::getDiagnostics() const {
    DiagnosticStats stats;
    stats.totalBytesConsumed = totalBytesConsumed_.load();
    stats.shmPopCount = shmPopCount_.load();
    stats.shmUnderrunCount = shmUnderrunCount_.load();
    stats.formatValidationErrors = formatValidationErrors_.load();
    stats.partialChunkConsumptions = partialChunkConsumptions_.load();
    stats.currentShmFillPercent = getCurrentShmFillLevel();
    
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
    
    if (logger_) {
        logger_->debug("IsochPacketProvider diagnostics reset");
    }
}

} // namespace Isoch
} // namespace FWA