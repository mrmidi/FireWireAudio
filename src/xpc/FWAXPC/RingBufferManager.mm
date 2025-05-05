#include <spdlog/spdlog.h>
#include "xpc/FWAXPC/RingBufferManager.hpp" // Updated path
#include "xpc/FWAXPC/ShmIsochBridge.hpp" // Updated path
#include <sys/mman.h>
#include <unistd.h>
#include <os/log.h>
#include <cstring> // For std::memset
#include <new>     // Potentially for placement new if Option A is needed later
#include <thread>
#include <atomic>
#include <exception> // For std::exception

// Define kLog if not already defined globally for this file
static const char *kLog = "[FWADaemon]";

// --- RingBufferManager Implementation ---

RingBufferManager& RingBufferManager::instance()
{
    static RingBufferManager gInstance; // Use a static local for singleton
    return gInstance;
}

bool RingBufferManager::map(int shmFd, bool isCreator)
{
    os_log_info(OS_LOG_DEFAULT, "%s map: Entered function. shmFd: %d", kLog, shmFd); // Log Entry + fd

    if (shm_) {
        os_log_info(OS_LOG_DEFAULT, "%s map: Already mapped (shm_ = %p). Skipping.", kLog, shm_);
        return true; // Already mapped
    }

    shmSize_ = sizeof(RTShmRing::SharedRingBuffer_POD);
    os_log_info(OS_LOG_DEFAULT, "%s map: Calculated size: %zu", kLog, shmSize_); // Log Size

    void *ptr = ::mmap(nullptr, shmSize_,
                       PROT_READ | PROT_WRITE, MAP_SHARED,
                       shmFd, 0);
    os_log_info(OS_LOG_DEFAULT, "%s map: mmap call completed.", kLog); // Log After mmap call

    if (ptr == MAP_FAILED)
    {
        SPDLOG_ERROR("map: mmap failed: {}", errno);
        // shmFd is closed by caller (FWADaemon.mm)
        return false;
    }
    os_log_info(OS_LOG_DEFAULT, "%s map: mmap successful, received ptr: %p", kLog, ptr); // Log Success ptr

    // mlock is advisory
    if (::mlock(ptr, shmSize_) != 0) {
        os_log(OS_LOG_DEFAULT, "%s map: mlock warning: %{errno}d", kLog, errno);
    } else {
         os_log_info(OS_LOG_DEFAULT, "%s map: mlock successful.", kLog);
    }

    // Assign pointer
    shm_ = static_cast<RTShmRing::SharedRingBuffer_POD*>(ptr);
    os_log_info(OS_LOG_DEFAULT, "%s map: shm_ assigned pointer value: %p (should match ptr)", kLog, shm_); // Log shm_ VALUE

    if (isCreator) {
        os_log_info(OS_LOG_DEFAULT, "%s map: Initializing (zeroing) mapped memory region as creator...", kLog);
        std::memset(shm_, 0, shmSize_);
        shm_->control.abiVersion = kShmVersion;
        shm_->control.capacity   = kRingCapacityPow2;
        os_log_info(OS_LOG_DEFAULT, "%s map: Set abiVersion=%u, capacity=%u", kLog, shm_->control.abiVersion, shm_->control.capacity);
    } else {
        os_log_info(OS_LOG_DEFAULT, "%s map: Attacher mode, verifying header fields...", kLog);
        if (shm_->control.abiVersion != kShmVersion || shm_->control.capacity != kRingCapacityPow2) {
            os_log_error(OS_LOG_DEFAULT, "%s map: ERROR - SHM header mismatch: abiVersion=%u (expected %u), capacity=%u (expected %zu)",
                kLog, shm_->control.abiVersion, kShmVersion, shm_->control.capacity, kRingCapacityPow2);
            ::munlock(ptr, shmSize_);
            ::munmap(ptr, shmSize_);
            shm_ = nullptr;
            return false;
        }
    }

    // Start the reader thread only after successful initialization
    running_ = true;
    os_log_info(OS_LOG_DEFAULT, "%s map: running_ flag set true.", kLog);

    try {
         reader_ = std::thread(&RingBufferManager::readerLoop, this);
         os_log_info(OS_LOG_DEFAULT, "%s map: std::thread object created.", kLog);
         // reader_.detach(); // <-- REMOVED
         os_log_info(OS_LOG_DEFAULT, "%s map: reader thread started (joinable).", kLog);
    } catch (const std::exception& e) {
         os_log_error(OS_LOG_DEFAULT, "%s map: Exception during thread creation: %{public}s. Aborting map.", kLog, e.what());
         running_ = false;
         if (shm_) {
            ::munlock(shm_, shmSize_);
            ::munmap(shm_, shmSize_);
            shm_ = nullptr;
         } else if (ptr != MAP_FAILED) {
            ::munlock(ptr, shmSize_);
            ::munmap(ptr, shmSize_);
         }
         return false;
    } catch (...) {
         os_log_error(OS_LOG_DEFAULT, "%s map: Unknown exception during thread creation. Aborting map.", kLog);
         running_ = false;
         if (shm_) {
            ::munlock(shm_, shmSize_);
            ::munmap(shm_, shmSize_);
            shm_ = nullptr;
         } else if (ptr != MAP_FAILED) {
            ::munlock(ptr, shmSize_);
            ::munmap(ptr, shmSize_);
         }
         return false;
    }

    SPDLOG_INFO("map: Exiting function successfully."); // Log successful exit
    return true;
}

void RingBufferManager::unmap()
{
    os_log_info(OS_LOG_DEFAULT, "%s unmap: Entered function.", kLog);

    // --- Signal and Join Thread ---
    if (running_.load()) {
        running_ = false;
        os_log_info(OS_LOG_DEFAULT, "%s unmap: Signaled reader thread to stop.", kLog);
        if (reader_.joinable()) {
            os_log_info(OS_LOG_DEFAULT, "%s unmap: Joining reader thread...", kLog);
            try {
                reader_.join();
                os_log_info(OS_LOG_DEFAULT, "%s unmap: Reader thread joined successfully.", kLog);
            } catch (const std::system_error& e) {
                os_log_error(OS_LOG_DEFAULT, "%s unmap: Exception while joining thread: %d - %{public}s", kLog, e.code().value(), e.what());
            } catch (...) {
                os_log_error(OS_LOG_DEFAULT, "%s unmap: Unknown exception while joining thread", kLog);
            }
        } else {
            os_log(OS_LOG_DEFAULT, "%s unmap: Reader thread was not joinable (already joined or not started?).", kLog);
        }
    } else {
        os_log_info(OS_LOG_DEFAULT, "%s unmap: Thread was not running.", kLog);
    }
    // --- End Thread Join ---

    if (shm_)
    {
        os_log_info(OS_LOG_DEFAULT, "%s unmap: Unmapping memory region %p.", kLog, shm_);
        // No explicit destructor call needed for memset/POD initialization (Option B)
        // If using Option A (placement new), call destructor: shm_->~SharedRingBuffer();

        if (::munlock(shm_, shmSize_) != 0) {
             os_log(OS_LOG_DEFAULT, "%s unmap: munlock warning: %{errno}d", kLog, errno);
        }
        if (::munmap(shm_, shmSize_) != 0) {
            os_log_error(OS_LOG_DEFAULT, "%s unmap: munmap failed: %{errno}d", kLog, errno);
        }
        shm_ = nullptr; // Set pointer to null after unmapping
        os_log_info(OS_LOG_DEFAULT, "%s unmap: Memory unmapped.", kLog);
    } else {
        os_log_info(OS_LOG_DEFAULT, "%s unmap: Shared memory pointer was already null.", kLog);
    }
     os_log_info(OS_LOG_DEFAULT, "%s unmap: Exiting function.", kLog);
}

// Destructor just calls unmap
RingBufferManager::~RingBufferManager() {
     os_log_info(OS_LOG_DEFAULT, "%s ~RingBufferManager: Destructor called.", kLog);
    unmap();
}

void RingBufferManager::readerLoop()
{
    os_log_info(OS_LOG_DEFAULT, "%s readerLoop: Thread started.", kLog); // Log thread start

    RTShmRing::AudioChunk_POD localChunk; // Allocate local copy buffer ONCE outside the loop

    // Ensure shm_ is valid before entering loop (defensive check)
    if (!shm_) {
        os_log_error(OS_LOG_DEFAULT, "%s readerLoop: Exiting early, shm_ is null.", kLog);
        return;
    }

    while (running_.load(std::memory_order_relaxed)) // Use atomic load
    {
        // Check shm_ again inside loop? Paranoid check, maybe remove later.
        if (!shm_) {
             os_log_error(OS_LOG_DEFAULT, "%s readerLoop: Exiting loop, shm_ became null.", kLog);
             break;
        }

        if (RTShmRing::pop(shm_->control, shm_->ring, localChunk))
        {
            // os_log_debug(OS_LOG_DEFAULT, "%s readerLoop: Popped %u frames @ hostTime %llu",
            //                kLog, localChunk.frameCount,
            //                static_cast<unsigned long long>(localChunk.timeStamp.mHostTime));

            // TODO: Ensure ShmIsochBridge::instance() is thread-safe if called from multiple places,
            // but here it's only called from this single reader thread.
            ShmIsochBridge::instance().enqueue(localChunk);

        }
        else
        {
            // Nothing ready – sleep briefly to avoid busy-waiting and burning CPU
            std::this_thread::sleep_for(std::chrono::microseconds(500)); // 500µs sleep
        }
    }

    os_log_info(OS_LOG_DEFAULT, "%s readerLoop: Thread exiting.", kLog); // Log thread exit
}