#pragma once
#include <shared/SharedMemoryStructures.hpp>
#include "Isoch/core/AmdtpTransmitter.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <array>
#include <vector>

class ShmIsochBridge {
public:
    static ShmIsochBridge& instance();
    void start(FWA::Isoch::ITransmitPacketProvider* provider);
    void stop();
    void enqueue(const RTShmRing::AudioChunk_POD& chunk);
    bool isRunning() const;

private:
    ShmIsochBridge() = default;
    ~ShmIsochBridge();

    void worker();

    struct QueueItem {
        std::vector<std::byte> data;
    };

    static constexpr size_t kQCap = 256;
    std::array<QueueItem, kQCap> q_;
    std::atomic<size_t> writeIdx_{0}, readIdx_{0};

    std::atomic<bool> running_{false};
    std::thread thread_;
    FWA::Isoch::ITransmitPacketProvider* provider_ = nullptr;
};
