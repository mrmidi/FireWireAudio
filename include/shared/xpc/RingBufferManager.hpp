#pragma once
#include <shared/SharedMemoryStructures.hpp>
#include <atomic>
#include <thread>

class RingBufferManager
{
public:
    static RingBufferManager& instance();

    // Accepts isCreator: true if this process created the SHM, false if attaching
    bool map(int shmFd, bool isCreator);
    void unmap();                 // daemon shutdown
    bool isMapped() const { return shm_ != nullptr; }

private:
    RingBufferManager() = default;
    ~RingBufferManager();

    void readerLoop();            // background thread

    // --- SHM pointers ---
    RTShmRing::SharedRingBuffer_POD *shm_ = nullptr;
    size_t                       shmSize_ = 0;

    // --- thread control ---
    std::atomic<bool>            running_{false};
    std::thread                  reader_;
};
