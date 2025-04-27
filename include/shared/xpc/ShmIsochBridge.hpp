#pragma once
#include <shared/SharedMemoryStructures.hpp>
#include "Isoch/core/AmdtpTransmitter.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <array>
#include <vector>

class ShmIsochBridge
{
public:
    static ShmIsochBridge& instance();
    // Pass the packet‐provider; we will call pushAudioData on it directly
    void start(FWA::Isoch::ITransmitPacketProvider* provider);
    void stop();
    void enqueue(const RTShmRing::AudioChunk& chunk);          // called from RingBufferManager

private:
    ShmIsochBridge() = default;
    ~ShmIsochBridge();

    void worker();                                             // single producer → single consumer

    struct QueueItem {
        std::vector<std::byte> data;
    };

    // lock-free SPSC ring (power-of-two size)
    static constexpr size_t kQCap = 256;
    std::array<QueueItem, kQCap> q_;
    std::atomic<size_t> writeIdx_{0}, readIdx_{0};

    std::atomic<bool> running_{false};
    std::thread       thread_;

    // Instead, hold the packet-provider interface directly
    FWA::Isoch::ITransmitPacketProvider* provider_ = nullptr;
};
