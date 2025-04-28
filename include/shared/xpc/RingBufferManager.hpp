#pragma once
#include <shared/SharedMemoryStructures.hpp>
#include <atomic>
#include <thread>

class RingBufferManager
{
public:
    static RingBufferManager& instance();

    bool map(int shmFd);          // driver calls once
    void unmap();                 // daemon shutdown
    // --- ADDED: Check if mapped ---
    bool isMapped() const { return shm_ != nullptr; }

private:
    RingBufferManager() = default;
    ~RingBufferManager();

    void readerLoop();            // background thread

    // --- SHM pointers ---
    RTShmRing::SharedRingBuffer *shm_ = nullptr;
    size_t                       shmSize_ = 0;

    // --- thread control ---
    std::atomic<bool>            running_{false};
    std::thread                  reader_;
};
