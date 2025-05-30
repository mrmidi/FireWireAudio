#import "ShmIsochBridge.hpp"
#include <os/log.h>

static const char *kLog = "[Bridge]";

ShmIsochBridge& ShmIsochBridge::instance()
{
    static ShmIsochBridge g;
    return g;
}

// New start: store only the provider interface
void ShmIsochBridge::start(FWA::Isoch::ITransmitPacketProvider* provider)
{
    if (running_ || !provider) return;
    provider_ = provider;
    running_  = true;
    thread_   = std::thread(&ShmIsochBridge::worker, this);
}

void ShmIsochBridge::stop()
{
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

ShmIsochBridge::~ShmIsochBridge() { stop(); }

void ShmIsochBridge::enqueue(const RTShmRing::AudioChunk_POD& chunk)
{
    const size_t wr = writeIdx_.load(std::memory_order_relaxed);
    const size_t rd = readIdx_.load(std::memory_order_acquire);
    if (wr - rd >= kQCap)
    {
        os_log_error(OS_LOG_DEFAULT, "%s queue overflow", kLog);
        return;
    }
    size_t slot     = wr & (kQCap - 1);
    q_[slot].data.assign(chunk.audio, chunk.audio + chunk.dataBytes);
    writeIdx_.store(wr + 1, std::memory_order_release);
}

void ShmIsochBridge::worker()
{
    auto* prov = provider_;
    if (!prov) { os_log_error(OS_LOG_DEFAULT, "%s no packet provider", kLog); return; }

    while (running_)
    {
        const size_t rd = readIdx_.load(std::memory_order_relaxed);
        const size_t wr = writeIdx_.load(std::memory_order_acquire);

        if (rd == wr) {                          // queue empty
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        size_t slot = rd & (kQCap - 1);
        const auto& item = q_[slot];

        if (!prov->pushAudioData(item.data.data(), item.data.size()))
            os_log_error(OS_LOG_DEFAULT, "%s Isoch FIFO overflow", kLog);

        readIdx_.store(rd + 1, std::memory_order_release);
    }
}
