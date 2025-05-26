// ShmIsochBridge.cpp
#include "xpc/FWAXPC/ShmIsochBridge.hpp"
#include <spdlog/spdlog.h>
#include <os/log.h>
#include <pthread/qos.h>
#include <chrono>

static const char* kLog = "[Bridge]";

ShmIsochBridge& ShmIsochBridge::instance()
{
    static ShmIsochBridge g;
    return g;
}

void ShmIsochBridge::start(FWA::Isoch::ITransmitPacketProvider* provider)
{
    os_log_debug(OS_LOG_DEFAULT, "%s starting bridge", kLog);
    if (_running.load(std::memory_order_relaxed) || !provider) return;
    _provider = provider;
    _running.store(true, std::memory_order_release);
    _thread = std::thread(&ShmIsochBridge::worker, this);
}

void ShmIsochBridge::stop()
{
    os_log_debug(OS_LOG_DEFAULT, "%s stopping bridge", kLog);
    _running.store(false, std::memory_order_release);
    if (_thread.joinable()) _thread.join();
}

bool ShmIsochBridge::isRunning() const noexcept
{
    return _running.load(std::memory_order_relaxed);
}

ShmIsochBridge::~ShmIsochBridge() { stop(); }

void ShmIsochBridge::enqueue(const RTShmRing::AudioChunk_POD& chunk)
{
    const size_t wr = _writeIdx.load(std::memory_order_relaxed);
    const size_t rd = _readIdx.load(std::memory_order_acquire);
    if (wr - rd >= kQCap) {
        os_log_error(OS_LOG_DEFAULT, "%s queue overflow", kLog);
        return;
    }
    size_t slot = wr & (kQCap - 1);
    _q[slot].ptr   = chunk.audio;
    _q[slot].bytes = chunk.dataBytes;
    _writeIdx.store(wr + 1, std::memory_order_release);
}

void ShmIsochBridge::worker()
{
    // Boost this thread to interactive QoS
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);

    auto* prov = _provider;
    if (!prov) {
        os_log_error(OS_LOG_DEFAULT, "%s no packet provider", kLog);
        return;
    }

    while (_running.load(std::memory_order_acquire))
    {
        const size_t rd = _readIdx.load(std::memory_order_relaxed);
        const size_t wr = _writeIdx.load(std::memory_order_acquire);

        if (rd == wr) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            continue;
        }

        size_t slot = rd & (kQCap - 1);
        auto  item = _q[slot];

        const std::byte* ptr = item.ptr;
        const std::byte* end = ptr + item.bytes;
        while (ptr < end && _running.load(std::memory_order_relaxed))
        {
            size_t remain = static_cast<size_t>(end - ptr);
            size_t chunk  = (remain >= 64 ? 64 : remain);
            while (_running.load(std::memory_order_relaxed)
                   && !prov->pushAudioData(ptr, chunk))
            {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
            ptr += chunk;
        }

        _readIdx.store(rd + 1, std::memory_order_release);
    }
}