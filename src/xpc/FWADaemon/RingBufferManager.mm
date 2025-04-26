#import "RingBufferManager.hpp"
#include "ShmIsochBridge.hpp"
#include <sys/mman.h>
#include <unistd.h>
#include <os/log.h>

static const char *kLog = "[FWADaemon]";

RingBufferManager& RingBufferManager::instance()
{
    static RingBufferManager g;
    return g;
}

bool RingBufferManager::map(int shmFd)
{
    if (shm_) return true;                       // already mapped

    shmSize_ = sizeof(RTShmRing::SharedRingBuffer);
    void *ptr = ::mmap(nullptr, shmSize_,
                       PROT_READ|PROT_WRITE, MAP_SHARED,
                       shmFd, 0);
    if (ptr == MAP_FAILED)
    {
        os_log_error(OS_LOG_DEFAULT, "%s mmap failed: %{errno}d", kLog, errno);
        return false;
    }
    if (::mlock(ptr, shmSize_) != 0)
        os_log(OS_LOG_DEFAULT, "%s mlock warning: %{errno}d", kLog, errno);

    shm_ = static_cast<RTShmRing::SharedRingBuffer*>(ptr);

    running_ = true;
    reader_  = std::thread(&RingBufferManager::readerLoop, this);
    reader_.detach();                            // fire-and-forget
    os_log(OS_LOG_DEFAULT, "%s ring buffer mapped & reader thread started", kLog);
    return true;
}

void RingBufferManager::unmap()
{
    running_ = false;
    if (shm_)
    {
        ::munlock(shm_, shmSize_);
        ::munmap(shm_, shmSize_);
        shm_ = nullptr;
    }
}

RingBufferManager::~RingBufferManager() { unmap(); }

void RingBufferManager::readerLoop()
{
    RTShmRing::AudioChunk local;
    while (running_)
    {
        if (RTShmRing::pop(shm_->control, shm_->ring, local))
        {
            ShmIsochBridge::instance().enqueue(local);     // <<< NEW line
            os_log(OS_LOG_DEFAULT,
                   "%s popped %u frames @ hostTime %llu",
                   kLog, local.frameCount,
                   static_cast<unsigned long long>(local.timeStamp.mHostTime));
        }
        else
        {
            // nothing ready – sleep 500 µs to avoid burning a core
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }
}
