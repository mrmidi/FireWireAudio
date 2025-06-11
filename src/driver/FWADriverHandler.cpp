#include "FWADriverHandler.hpp"

#define DEBUG 0
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
    // Not hot path: always log
    os_log(OS_LOG_DEFAULT, "%{public}sFWADriverHandler: Setting up shared memory '%{public}s'", LogPrefix, shmName.c_str());
    if (shmPtr_) {
        os_log(OS_LOG_DEFAULT, "%{public}sFWADriverHandler: Shared memory already set up.", LogPrefix);
        return true;
    }
    shmFd_ = shm_open(shmName.c_str(), O_RDWR, 0);
    if (shmFd_ == -1) {
        os_log_error(OS_LOG_DEFAULT, "%{public}sFWADriverHandler: shm_open failed for '%{public}s': %{errno}d", LogPrefix, shmName.c_str(), errno);
        return false;
    }
    shmSize_ = sizeof(RTShmRing::SharedRingBuffer_POD);
    shmPtr_ = mmap(nullptr, shmSize_, PROT_READ | PROT_WRITE, MAP_SHARED, shmFd_, 0);
    if (shmPtr_ == MAP_FAILED) {
        os_log_error(OS_LOG_DEFAULT, "%{public}sFWADriverHandler: mmap failed: %{errno}d", LogPrefix, errno);
        close(shmFd_);
        shmFd_ = -1;
        shmPtr_ = nullptr;
        return false;
    }
    if (mlock(shmPtr_, shmSize_) != 0) {
        os_log_error(OS_LOG_DEFAULT, "%{public}sFWADriverHandler: WARNING - mlock failed: %{errno}d. Real-time performance may suffer.", LogPrefix, errno);
    }
    os_log_info(OS_LOG_DEFAULT, "%{public}sFWADriverHandler: Hinting kernel to prefetch pages (MADV_WILLNEED).", LogPrefix);
    if (madvise(shmPtr_, shmSize_, MADV_WILLNEED) != 0) {
        os_log_error(OS_LOG_DEFAULT, "%{public}sFWADriverHandler: WARNING - madvise(MADV_WILLNEED) failed: %{errno}d", LogPrefix, errno);
        // This is just a hint, so failure isn't critical, just log it.
    }
    RTShmRing::SharedRingBuffer_POD* sharedRegion = static_cast<RTShmRing::SharedRingBuffer_POD*>(shmPtr_);
    controlBlock_ = &(sharedRegion->control);
    ringBuffer_ = sharedRegion->ring;
    if (controlBlock_->abiVersion != kShmVersion || controlBlock_->capacity != kRingCapacityPow2) {
        os_log_error(OS_LOG_DEFAULT, "%{public}sFWADriverHandler: ERROR - Shared memory header mismatch (abiVersion: %{public}u, capacity: %{public}u). Tearing down.",
            LogPrefix, controlBlock_->abiVersion, controlBlock_->capacity);
        TeardownSharedMemory();
        return false;
    }
    os_log(OS_LOG_DEFAULT, "%{public}sFWADriverHandler: Shared memory setup successful (Capacity: %{public}u, ABI: %{public}u).", LogPrefix, controlBlock_->capacity, controlBlock_->abiVersion);
    return true;
}

void FWADriverHandler::TeardownSharedMemory() {
    #if DEBUG
    os_log(OS_LOG_DEFAULT, "%sFWADriverHandler: Tearing down shared memory.", LogPrefix);
    #endif
    if (shmPtr_ != nullptr) {
        if (munlock(shmPtr_, shmSize_) != 0) {
            #if DEBUG
            os_log_error(OS_LOG_DEFAULT, "%sFWADriverHandler: WARNING - munlock failed: %{errno}d", LogPrefix, errno);
            #endif
        }
        if (munmap(shmPtr_, shmSize_) != 0) {
            #if DEBUG
            os_log_error(OS_LOG_DEFAULT, "%sFWADriverHandler: munmap failed: %{errno}d", LogPrefix, errno);
            #endif
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
    #if DEBUG
    os_log(OS_LOG_DEFAULT, "%sFWADriverHandler: Shared memory teardown complete.", LogPrefix);
    #endif
}

bool FWADriverHandler::PushToSharedMemory(const AudioBufferList* src, const AudioTimeStamp& ts, uint32_t frames, uint32_t bytesPerFrame) {
    if (!controlBlock_ || !ringBuffer_) return false;

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
        }
    }

    bool success = RTShmRing::push(*controlBlock_, ringBuffer_, src, ts, frames, bytesPerFrame);
    
    // Log only after streams are active
    #if DEBUG
    if (!success && controlBlock_->streamActive) {
        auto wr = RTShmRing::WriteIndexProxy(*controlBlock_).load(std::memory_order_relaxed);
        auto rd = RTShmRing::ReadIndexProxy(*controlBlock_).load(std::memory_order_relaxed);
        os_log_error(OS_LOG_DEFAULT,
            "%sPUSH FAIL  wr=%llu rd=%llu used=%llu  frames=%u bytesPerFrame=%u",
            LogPrefix, wr, rd, wr-rd, frames, bytesPerFrame);
        
        localOverrunCounter_++;
        if ((localOverrunCounter_ & 0xFF) == 0) {
            os_log_error(OS_LOG_DEFAULT, "%sPushToSharedMemory: Ring buffer OVERRUN! Count: %u", LogPrefix, localOverrunCounter_);
        }
    }
    #endif
    return success;
}

OSStatus FWADriverHandler::OnStartIO() {
    // Not hot path: always log
    os_log(OS_LOG_DEFAULT, "%{public}sFWADriverHandler: OnStartIO called.", LogPrefix);
    if (!controlBlock_ || !ringBuffer_) {
        os_log_error(OS_LOG_DEFAULT, "%{public}sFWADriverHandler: ERROR - Cannot StartIO, shared memory not set up.", LogPrefix);
        return kAudioHardwareUnspecifiedError;
    }
    localOverrunCounter_ = 0;
    os_log(OS_LOG_DEFAULT, "%{public}sFWADriverHandler: STUB - Assuming Daemon started IO successfully.", LogPrefix);
    return kAudioHardwareNoError;
}

void FWADriverHandler::OnStopIO() {
    // Not hot path: always log
    os_log(OS_LOG_DEFAULT, "%{public}sFWADriverHandler: OnStopIO called.", LogPrefix);
    // Optionally update shared atomic counters here
}
