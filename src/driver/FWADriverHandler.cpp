#include "FWADriverHandler.hpp"
#include <os/log.h>
#include <sys/mman.h>   // For mmap, munmap, shm_open, shm_unlink
#include <fcntl.h>      // For O_RDWR
#include <unistd.h>     // For close
#include <stdexcept>
#include <cerrno>
#include <cstring>

constexpr const char* LogPrefix = "FWADriverASPL: ";

FWADriverHandler::FWADriverHandler() {
    localOverrunCounter_ = 0;
}

FWADriverHandler::~FWADriverHandler() {
    TeardownSharedMemory();
}

bool FWADriverHandler::SetupSharedMemory(const std::string& shmName) {
    os_log(OS_LOG_DEFAULT, "%sFWADriverHandler: Setting up shared memory '%s'", LogPrefix, shmName.c_str());
    if (shmPtr_) {
        os_log(OS_LOG_DEFAULT, "%sFWADriverHandler: Shared memory already set up.", LogPrefix);
        return true;
    }
    shmFd_ = shm_open(shmName.c_str(), O_RDWR, 0);
    if (shmFd_ == -1) {
        os_log_error(OS_LOG_DEFAULT, "%sFWADriverHandler: shm_open failed for '%s': %{errno}d", LogPrefix, shmName.c_str(), errno);
        return false;
    }
    shmSize_ = sizeof(RTShmRing::SharedRingBuffer_POD);
    shmPtr_ = mmap(nullptr, shmSize_, PROT_READ | PROT_WRITE, MAP_SHARED, shmFd_, 0);
    if (shmPtr_ == MAP_FAILED) {
        os_log_error(OS_LOG_DEFAULT, "%sFWADriverHandler: mmap failed: %{errno}d", LogPrefix, errno);
        close(shmFd_);
        shmFd_ = -1;
        shmPtr_ = nullptr;
        return false;
    }
    if (mlock(shmPtr_, shmSize_) != 0) {
        os_log_error(OS_LOG_DEFAULT, "%sFWADriverHandler: WARNING - mlock failed: %{errno}d. Real-time performance may suffer.", LogPrefix, errno);
    }
    os_log_info(OS_LOG_DEFAULT, "%sFWADriverHandler: Hinting kernel to prefetch pages (MADV_WILLNEED).", LogPrefix);
    if (madvise(shmPtr_, shmSize_, MADV_WILLNEED) != 0) {
        os_log_error(OS_LOG_DEFAULT, "%sFWADriverHandler: WARNING - madvise(MADV_WILLNEED) failed: %{errno}d", LogPrefix, errno);
        // This is just a hint, so failure isn't critical, just log it.
    }
    RTShmRing::SharedRingBuffer_POD* sharedRegion = static_cast<RTShmRing::SharedRingBuffer_POD*>(shmPtr_);
    controlBlock_ = &(sharedRegion->control);
    ringBuffer_ = sharedRegion->ring;
    if (controlBlock_->abiVersion != kShmVersion || controlBlock_->capacity != kRingCapacityPow2) {
        os_log_error(OS_LOG_DEFAULT, "%sFWADriverHandler: ERROR - Shared memory header mismatch (abiVersion: %u, capacity: %u). Tearing down.",
            LogPrefix, controlBlock_->abiVersion, controlBlock_->capacity);
        TeardownSharedMemory();
        return false;
    }
    os_log(OS_LOG_DEFAULT, "%sFWADriverHandler: Shared memory setup successful (Capacity: %u, ABI: %u).", LogPrefix, controlBlock_->capacity, controlBlock_->abiVersion);
    return true;
}

void FWADriverHandler::TeardownSharedMemory() {
    os_log(OS_LOG_DEFAULT, "%sFWADriverHandler: Tearing down shared memory.", LogPrefix);
    if (shmPtr_ != nullptr) {
        if (munlock(shmPtr_, shmSize_) != 0) {
            os_log_error(OS_LOG_DEFAULT, "%sFWADriverHandler: WARNING - munlock failed: %{errno}d", LogPrefix, errno);
        }
        if (munmap(shmPtr_, shmSize_) != 0) {
            os_log_error(OS_LOG_DEFAULT, "%sFWADriverHandler: munmap failed: %{errno}d", LogPrefix, errno);
        }
        shmPtr_ = nullptr;
    }
    if (shmFd_ != -1) {
        close(shmFd_);
        shmFd_ = -1;
    }
    controlBlock_ = nullptr;
    ringBuffer_ = nullptr;
    shmSize_ = 0;
    os_log(OS_LOG_DEFAULT, "%sFWADriverHandler: Shared memory teardown complete.", LogPrefix);
}

bool FWADriverHandler::PushToSharedMemory(const AudioBufferList* src, const AudioTimeStamp& ts, uint32_t frames, uint32_t bytesPerFrame) {
    if (!controlBlock_ || !ringBuffer_) return false;

    // Capture SHM state before any operations for diagnostic logging
    auto pre_wr = RTShmRing::WriteIndexProxy(*controlBlock_).load(std::memory_order_relaxed);
    auto pre_rd = RTShmRing::ReadIndexProxy(*controlBlock_).load(std::memory_order_relaxed);
    uint64_t pre_used = pre_wr - pre_rd;
    bool streamActiveState = (controlBlock_->streamActive == 1);

    // Static counter for throttled verbose logging
    static uint32_t pushCallCount = 0;
    pushCallCount++;

    // Verbose logging every 1000 calls or when there are issues to track producer behavior
    bool shouldLogVerbose = ((pushCallCount % 1000) == 0) || !streamActiveState || (pre_used == 0) || (pre_used >= controlBlock_->capacity);
    
    if (shouldLogVerbose) {
        os_log_info(OS_LOG_DEFAULT, "%sPUSHING call #%u: frames=%u, bytesPerFrame=%u. SHM before: wr=%llu, rd=%llu, used=%llu, cap=%u, active=%u",
            LogPrefix, pushCallCount, frames, bytesPerFrame, pre_wr, pre_rd, pre_used, controlBlock_->capacity, controlBlock_->streamActive);
    }

    // ----------------------------------------------
    // ➊ if ring full *and* streams not yet active →
    //    advance rd one slot (overwrite oldest)
    // ----------------------------------------------
    if (controlBlock_->streamActive == 0) {
        auto wr = RTShmRing::WriteIndexProxy(*controlBlock_).load(std::memory_order_relaxed);
        auto rd = RTShmRing::ReadIndexProxy(*controlBlock_).load(std::memory_order_relaxed);
        if (wr - rd >= controlBlock_->capacity) {
            // Drop the oldest chunk to make room
            RTShmRing::ReadIndexProxy(*controlBlock_)
                .store(rd + 1, std::memory_order_release);
            
            if (shouldLogVerbose) {
                os_log_info(OS_LOG_DEFAULT, "%sDropped oldest chunk while stream inactive. rd advanced from %llu to %llu",
                    LogPrefix, rd, rd + 1);
            }
        }
    }

    bool success = RTShmRing::push(*controlBlock_, ringBuffer_, src, ts, frames, bytesPerFrame);
    
    // Capture SHM state after push attempt for diagnostic logging
    auto post_wr = RTShmRing::WriteIndexProxy(*controlBlock_).load(std::memory_order_relaxed);
    auto post_rd = RTShmRing::ReadIndexProxy(*controlBlock_).load(std::memory_order_relaxed);
    uint64_t post_used = post_wr - post_rd;

    if (shouldLogVerbose) {
        os_log_info(OS_LOG_DEFAULT, "%sPUSHED call #%u: success=%d. SHM after: wr=%llu, rd=%llu, used=%llu. Delta: wr+%lld, rd+%lld, used+%lld",
            LogPrefix, pushCallCount, success, post_wr, post_rd, post_used, 
            (int64_t)(post_wr - pre_wr), (int64_t)(post_rd - pre_rd), (int64_t)(post_used - pre_used));
    }
    
    // Log push failures when streams are active (existing logic)
    if (!success && controlBlock_->streamActive) {
        os_log_error(OS_LOG_DEFAULT,
            "%sPUSH FAIL  wr=%llu rd=%llu used=%llu  frames=%u bytesPerFrame=%u",
            LogPrefix, post_wr, post_rd, post_used, frames, bytesPerFrame);
        
        localOverrunCounter_++;
        if ((localOverrunCounter_ & 0xFF) == 0) {
            os_log_error(OS_LOG_DEFAULT, "%sPushToSharedMemory: Ring buffer OVERRUN! Count: %u", LogPrefix, localOverrunCounter_);
        }
    }
    return success;
}

OSStatus FWADriverHandler::OnStartIO() {
    os_log(OS_LOG_DEFAULT, "%sFWADriverHandler: OnStartIO called.", LogPrefix);
    if (!controlBlock_ || !ringBuffer_) {
        os_log(OS_LOG_DEFAULT, "%sFWADriverHandler: ERROR - Cannot StartIO, shared memory not set up.", LogPrefix);
        return kAudioHardwareUnspecifiedError;
    }

    localOverrunCounter_ = 0; 

    // 1. Make sure the ring is empty, then pre-fill four 8-frame chunks
    RTShmRing::WriteIndexProxy(*controlBlock_).store(0, std::memory_order_relaxed);
    RTShmRing::ReadIndexProxy(*controlBlock_).store(0, std::memory_order_relaxed);

    // preFillChunk() writes 8 frames of silence
    for (int i = 0; i < 4; ++i) {
        if (!preFillChunk()) {
            os_log_error(OS_LOG_DEFAULT, "%sFWADriverHandler: Failed to pre-fill chunk %d", LogPrefix, i);
            return kAudioHardwareUnspecifiedError;
        }
    }

    os_log_info(OS_LOG_DEFAULT, "%sFWADriverHandler: Pre-filled 4 chunks with silence", LogPrefix);

    // 2. NOW tell the consumer it may start
    StreamActiveProxy(*controlBlock_).store(1, std::memory_order_release);

    os_log_info(OS_LOG_DEFAULT, "%sFWADriverHandler: OnStartIO completed. Stream marked active (streamActive = 1). Daemon can now attempt to consume.", LogPrefix);
    return kAudioHardwareNoError;
}

bool FWADriverHandler::preFillChunk() {
    if (!controlBlock_ || !ringBuffer_) {
        return false;
    }

    // Create a minimal AudioBufferList with 8 frames of silence (stereo, 4 bytes per sample)
    const uint32_t framesPerChunk = 8;
    const uint32_t bytesPerFrame = 8; // 2 channels * 4 bytes per sample
    const uint32_t totalBytes = framesPerChunk * bytesPerFrame;
    
    // Static buffer for silence - zeroed by default
    static uint8_t silenceBuffer[8 * 8] = {0}; // 8 frames * 8 bytes per frame
    
    AudioBufferList silenceABL;
    silenceABL.mNumberBuffers = 1;
    silenceABL.mBuffers[0].mNumberChannels = 2;
    silenceABL.mBuffers[0].mDataByteSize = totalBytes;
    silenceABL.mBuffers[0].mData = silenceBuffer;
    
    // Create a dummy timestamp
    AudioTimeStamp silenceTimestamp = {};
    silenceTimestamp.mFlags = kAudioTimeStampSampleTimeValid;
    silenceTimestamp.mSampleTime = 0.0; // Will be updated by actual stream later
    
    // Use the existing push function to add the silence chunk
    bool success = RTShmRing::push(*controlBlock_, ringBuffer_, &silenceABL, silenceTimestamp, framesPerChunk, bytesPerFrame);
    
    if (success) {
        os_log_debug(OS_LOG_DEFAULT, "%sFWADriverHandler: Pre-filled silence chunk (%u frames, %u bytes)", 
                    LogPrefix, framesPerChunk, totalBytes);
    } else {
        os_log_error(OS_LOG_DEFAULT, "%sFWADriverHandler: Failed to pre-fill silence chunk", LogPrefix);
    }
    
    return success;
}

void FWADriverHandler::OnStopIO() {
    os_log(OS_LOG_DEFAULT, "%sFWADriverHandler: OnStopIO called.", LogPrefix);
    if (controlBlock_) {
        StreamActiveProxy(*controlBlock_).store(0, std::memory_order_release); // Mark as inactive
        os_log_info(OS_LOG_DEFAULT, "%sFWADriverHandler: Stream marked inactive (streamActive = 0).", LogPrefix);
    }
    // Optionally, could also send an XPC message to daemon if it needs explicit stop notification beyond SHM flag
}
