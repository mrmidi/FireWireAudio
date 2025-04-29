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
    bool success = RTShmRing::push(*controlBlock_, ringBuffer_, src, ts, frames, bytesPerFrame);
    if (!success) {
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
        os_log_error(OS_LOG_DEFAULT, "%sFWADriverHandler: ERROR - Cannot StartIO, shared memory not set up.", LogPrefix);
        return kAudioHardwareUnspecifiedError;
    }
    localOverrunCounter_ = 0;
    os_log(OS_LOG_DEFAULT, "%sFWADriverHandler: STUB - Assuming Daemon started IO successfully.", LogPrefix);
    return kAudioHardwareNoError;
}

void FWADriverHandler::OnStopIO() {
    os_log(OS_LOG_DEFAULT, "%sFWADriverHandler: OnStopIO called.", LogPrefix);
    // Optionally update shared atomic counters here
}
