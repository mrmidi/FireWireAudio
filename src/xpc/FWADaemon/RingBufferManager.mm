// RingBufferManager.mm

#include "RingBufferManager.hpp" // Assuming this includes SharedMemoryStructures.hpp
#include "ShmIsochBridge.hpp"
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

bool RingBufferManager::map(int shmFd)
{
    os_log_info(OS_LOG_DEFAULT, "%s map: Entered function. shmFd: %d", kLog, shmFd); // Log Entry + fd

    if (shm_) {
        os_log_info(OS_LOG_DEFAULT, "%s map: Already mapped (shm_ = %p). Skipping.", kLog, shm_);
        return true; // Already mapped
    }

    shmSize_ = sizeof(RTShmRing::SharedRingBuffer);
    os_log_info(OS_LOG_DEFAULT, "%s map: Calculated size: %zu", kLog, shmSize_); // Log Size

    void *ptr = ::mmap(nullptr, shmSize_,
                       PROT_READ | PROT_WRITE, MAP_SHARED,
                       shmFd, 0);
    os_log_info(OS_LOG_DEFAULT, "%s map: mmap call completed.", kLog); // Log After mmap call

    if (ptr == MAP_FAILED)
    {
        os_log_error(OS_LOG_DEFAULT, "%s map: mmap failed: %{errno}d", kLog, errno);
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
    shm_ = static_cast<RTShmRing::SharedRingBuffer*>(ptr);
    os_log_info(OS_LOG_DEFAULT, "%s map: shm_ assigned pointer value: %p (should match ptr)", kLog, shm_); // Log shm_ VALUE

    // Check and Initialize Memory (Option B: Zeroing + Default Member Init)
    if (shm_) {
         os_log_info(OS_LOG_DEFAULT, "%s map: Initializing (zeroing) mapped memory region...", kLog);

         // Zero the entire mapped region first
         std::memset(shm_, 0, shmSize_);
         os_log_info(OS_LOG_DEFAULT, "%s map: Memory zeroed using memset.", kLog);

         // Verify capacity *after* zeroing (relying on default member init in the struct)
         // The struct has: const uint32_t capacity {kRingCapacityPow2};
         uint32_t currentCapacity = shm_->control.capacity; // Read once
         os_log_info(OS_LOG_DEFAULT, "%s map: Capacity check after zeroing (should be %zu): %u", kLog, kRingCapacityPow2, currentCapacity);

         // Explicit check and error if capacity isn't right after initialization attempt
         if (currentCapacity != kRingCapacityPow2) {
             os_log_error(OS_LOG_DEFAULT, "%s map: CRITICAL - Capacity field is NOT %zu after init attempt! Value: %u. Aborting map.", kLog, kRingCapacityPow2, currentCapacity);
             // Optional: Try placement new as a fallback? Or just fail.
             // new (shm_) RTShmRing::SharedRingBuffer(); // Option A attempt
             // if (shm_->control.capacity != kRingCapacityPow2) { ... fail ... }

             // Cleanup before failing
             ::munlock(ptr, shmSize_); // Try to unlock, ignore error
             ::munmap(ptr, shmSize_);
             shm_ = nullptr; // Ensure shm_ is null on failure path
             return false;
         }
         os_log_info(OS_LOG_DEFAULT, "%s map: Memory initialization successful.", kLog);

    } else {
         // This case means assignment failed even though mmap returned valid ptr
         os_log_error(OS_LOG_DEFAULT, "%s map: CRITICAL - shm_ is NULL immediately after assignment from valid ptr (%p)! Aborting map.", kLog, ptr);
         // Unmap the memory since mmap seemingly succeeded but assignment failed
         ::munlock(ptr, shmSize_); // Try to unlock, ignore error
         ::munmap(ptr, shmSize_);
         return false;
    }

    // Start the reader thread only after successful initialization
    running_ = true;
    os_log_info(OS_LOG_DEFAULT, "%s map: running_ flag set true.", kLog);

    try {
         reader_ = std::thread(&RingBufferManager::readerLoop, this);
         os_log_info(OS_LOG_DEFAULT, "%s map: std::thread object created.", kLog);

         reader_.detach();
         os_log_info(OS_LOG_DEFAULT, "%s map: reader thread detached.", kLog);

    } catch (const std::exception& e) {
         os_log_error(OS_LOG_DEFAULT, "%s map: Exception during thread creation/detach: %{public}s. Aborting map.", kLog, e.what());
         // Cleanup before returning false
         running_ = false; // Reset flag
         // Destructor of shm_ was likely not called, manually unmap
         if (shm_) {
            ::munlock(shm_, shmSize_);
            ::munmap(shm_, shmSize_);
            shm_ = nullptr;
         }
         return false;
    } catch (...) {
         os_log_error(OS_LOG_DEFAULT, "%s map: Unknown exception during thread creation/detach. Aborting map.", kLog);
         running_ = false;
         if (shm_) {
             ::munlock(shm_, shmSize_);
             ::munmap(shm_, shmSize_);
             shm_ = nullptr;
         }
         return false;
    }

    os_log_info(OS_LOG_DEFAULT, "%s map: Exiting function successfully.", kLog); // Log successful exit
    return true;
}

void RingBufferManager::unmap()
{
    os_log_info(OS_LOG_DEFAULT, "%s unmap: Entered function.", kLog);
    running_ = false; // Signal thread to stop (readerLoop checks this)

    // Note: Since the thread is detached, we can't explicitly join() it here.
    // The thread should ideally check `running_` and exit cleanly.
    // A short sleep might give it time, but isn't guaranteed.
    // std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Optional small delay

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

    RTShmRing::AudioChunk localChunk; // Allocate local copy buffer ONCE outside the loop

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