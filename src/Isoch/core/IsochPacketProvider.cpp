#define DEBUG 0
// IsochPacketProvider.cpp - Direct SHM Implementation with Safety Margin
#include "Isoch/core/IsochPacketProvider.hpp"
#include <CoreServices/CoreServices.h>
#include <cstring>
#include <algorithm>    
#include <os/log.h>

namespace FWA {
namespace Isoch {

thread_local IsochPacketProvider::ShmStateCache IsochPacketProvider::shmCache_;

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
    
    // Validate SHM format before binding - log every field for debugging
    if (logger_) {
        logger_->info("bindSharedMemory: Validating SHM format - ABI: {}, capacity: {}, sampleRate: {}, channels: {}, bytesPerFrame: {}",
                      controlBlock->abiVersion, controlBlock->capacity, controlBlock->sampleRateHz, 
                      controlBlock->channelCount, controlBlock->bytesPerFrame);
    }
    
    if (!RTShmRing::ValidateFormat(*controlBlock)) {
        formatValidationErrors_++;
        if (logger_) {
            logger_->error("bindSharedMemory: SHM format validation failed - ABI: {}, capacity: {}, sampleRate: {}, channels: {}, bytesPerFrame: {}, expected ABI: {}",
                          controlBlock->abiVersion, controlBlock->capacity, controlBlock->sampleRateHz, 
                          controlBlock->channelCount, controlBlock->bytesPerFrame, kShmVersion);
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
    PreparedPacketData result;
    result.dataPtr = targetBuffer;
    result.dataLength = 0;
    result.generatedSilence = true;

    // === 0. Early parameter checks ===
    if (!targetBuffer || targetBufferSize == 0) {
        return result;
    }

    // === 1. Check SHM binding ===
    if (!shmControlBlock_ || !shmRingArray_) {
        std::memset(targetBuffer, 0, targetBufferSize);
        result.dataLength = targetBufferSize;
        return result;
    }

    // --- 1. Refresh SHM State from Cache if needed ---
    if (++shmCache_.updateCounter >= kCacheUpdateInterval || shmCache_.availableChunks <= kSafetyHedgeChunks) {
        if (shmControlBlock_) {
            shmCache_.writeIndex = RTShmRing::WriteIndexProxy(*shmControlBlock_).load(std::memory_order_acquire);
            const auto readIndex = RTShmRing::ReadIndexProxy(*shmControlBlock_).load(std::memory_order_relaxed);
            shmCache_.availableChunks = (shmCache_.writeIndex > readIndex) ? (shmCache_.writeIndex - readIndex) : 0;
        } else {
            shmCache_.availableChunks = 0;
        }
        shmCache_.updateCounter = 0;
    }

    // --- 2. Core Consumption Loop ---
    uint8_t* writePtr = targetBuffer;
    size_t remaining = targetBufferSize;
    bool underrunOccurred = false;

    while (remaining > 0) {
        // CORRECTED HEDGE CHECK: Check if current chunk has data FIRST.
        if (currentChunk_.remainingBytes() == 0) {
            if (shmCache_.availableChunks > kSafetyHedgeChunks) {
                if (popNextChunk()) {
                    // CLAMP FIX: Prevent underflow on race.
                    if (shmCache_.availableChunks > 0) {
                        shmCache_.availableChunks--;
                    }
                } else { 
                    underrunOccurred = true; 
                    break; 
                }
            } else { 
                underrunOccurred = true; 
                break; 
            }
        }
        
        uint32_t availableInChunk = currentChunk_.remainingBytes();
        size_t toCopy = std::min(remaining, static_cast<size_t>(availableInChunk));

        // The data is already in the correct format. This is now just a memcpy.
        std::memcpy(writePtr, currentChunk_.audioDataPtr + currentChunk_.consumedBytes, toCopy);
        
        writePtr += toCopy;
        remaining -= toCopy;
        currentChunk_.consumedBytes += static_cast<uint32_t>(toCopy);
        totalBytesConsumed_ += toCopy;
    }

    // --- 3. Finalize: Fill silence only on a true underrun ---
    if (remaining > 0) {
        std::memset(writePtr, 0, remaining);
        result.generatedSilence = true;
        if (underrunOccurred) {
            handleUnderrun(info); // Log the true underrun event.
        }
    } else {
        result.generatedSilence = false;
    }

    result.dataLength = targetBufferSize;
    return result;
}

bool IsochPacketProvider::popNextChunk() {
    if (!shmControlBlock_ || !shmRingArray_) {
        return false;
    }

    AudioTimeStamp timestamp;
    uint32_t dataBytes;
    const std::byte* audioPtr;

    // Use your zero-copy pop API - this only checks if wr > rd
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
    
    auto wr = RTShmRing::WriteIndexProxy(*shmControlBlock_).load(std::memory_order_acquire);
    auto rd = RTShmRing::ReadIndexProxy(*shmControlBlock_).load(std::memory_order_acquire);
    
    if (shmControlBlock_->capacity == 0) {
        return false;
    }
    
    uint64_t available = (wr >= rd) ? (wr - rd) : 0;
    return available > kSafetyHedgeChunks;
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
