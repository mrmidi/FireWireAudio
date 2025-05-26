// RingBufferManager.hpp
#pragma once

#include <shared/SharedMemoryStructures.hpp>
#include <Isoch/interfaces/ITransmitPacketProvider.hpp>
#include <atomic>
#include <os/log.h>
#include <thread>
#include <cstdint>
#include <sys/types.h>

/**
   A pure-C++ shared-memory reader that slices audio into 64-byte packets
   and pushes them directly into the FireWire stack via ITransmitPacketProvider.
   Removes the intermediate ShmIsochBridge.
*/
class RingBufferManager {
public:
    static RingBufferManager& instance() {
        static RingBufferManager inst;
        return inst;
    }

    /**
       Map (or attach to) the shared-memory ring. Starts the reader thread.
       @param shmFd     POSIX shm file descriptor
       @param isCreator true if this process created and zeroed the region
       @returns         true on success
    */
    bool map(int shmFd, bool isCreator);

    /**
       Stop reader thread and unmap shared memory.
    */
    void unmap();

    bool isMapped() const { return shm_ != nullptr; }

    /**
       Inject the packet provider that will receive 64-byte slices.
    */
    void setPacketProvider(FWA::Isoch::ITransmitPacketProvider* prov) noexcept {
        packetProvider_.store(prov, std::memory_order_release); // Store atomically
        if (prov) {
            os_log_info(OS_LOG_DEFAULT, "RingBufferManager: Packet provider set to %p.", static_cast<void*>(prov));
        } else {
            os_log_info(OS_LOG_DEFAULT, "RingBufferManager: Packet provider cleared (set to nullptr).");
        }
    }

private:
    RingBufferManager() = default;
    ~RingBufferManager() { unmap(); }
    RingBufferManager(const RingBufferManager&) = delete;
    RingBufferManager& operator=(const RingBufferManager&) = delete;

    void readerLoop();

    RTShmRing::SharedRingBuffer_POD* shm_     = nullptr;
    size_t                          shmSize_ = 0;
    std::atomic<bool>               running_{false};
    std::thread                    reader_;
    std::atomic<FWA::Isoch::ITransmitPacketProvider*> packetProvider_{nullptr}; // Make atomic
};