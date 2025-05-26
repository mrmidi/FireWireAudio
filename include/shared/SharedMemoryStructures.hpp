// Single-producer / single-consumer ring buffer for Core Audio plug-in <-> daemon
// Build with Clang 16 or newer, -std=c++20

#pragma once
#include <CoreAudio/AudioServerPlugIn.h>   // AudioTimeStamp, AudioBufferList
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

// ---------- Cache-line constant (compile-time!) ----------
constexpr std::size_t kDestructiveCL = 64;   // 64 bytes is correct on every Apple CPU since 2008

// ---------- Tunables ----------
constexpr std::size_t kMaxFramesPerChunk = 4096;
constexpr std::size_t kMaxChannels       = 32;
constexpr std::size_t kMaxBytesPerSample = 4;                     // 32-bit float / int
constexpr std::size_t kMaxBytesPerFrame  = kMaxChannels * kMaxBytesPerSample;
constexpr std::size_t kRingCapacityPow2  = 128;                   // must be power-of-two
static_assert((kRingCapacityPow2 & (kRingCapacityPow2 - 1)) == 0,
              "kRingCapacityPow2 must be a power of two");

constexpr std::size_t kAudioDataBytes = kMaxFramesPerChunk * kMaxBytesPerFrame;
constexpr uint32_t kShmVersion = 1;

namespace RTShmRing {

// --- POD Structures for Shared Memory ---
struct alignas(kDestructiveCL) AudioChunk_POD
{
    AudioTimeStamp timeStamp   {};
    uint32_t       frameCount  {0};
    uint32_t       dataBytes   {0};
    uint64_t       sequence    {0};
    std::byte      audio[kAudioDataBytes] {};
};

struct alignas(kDestructiveCL) ControlBlock_POD
{
    uint32_t abiVersion {0};
    uint32_t capacity   {0};
    uint64_t writeIndex {0};
    char     pad0[kDestructiveCL - sizeof(uint32_t)*2 - sizeof(uint64_t)];
    uint64_t readIndex  {0};
    char     pad1[kDestructiveCL - sizeof(uint64_t)];
    uint32_t overrunCount  {0};
    uint32_t underrunCount {0};
};

struct alignas(kDestructiveCL) SharedRingBuffer_POD
{
    ControlBlock_POD control;
    AudioChunk_POD   ring[kRingCapacityPow2];
};

// --- Helper Functions for Atomic Access (Proxies) ---
inline std::atomic<uint64_t>& WriteIndexProxy(ControlBlock_POD& cb) noexcept {
    return *reinterpret_cast<std::atomic<uint64_t>*>(&cb.writeIndex);
}
inline std::atomic<uint64_t>& ReadIndexProxy(ControlBlock_POD& cb) noexcept {
    return *reinterpret_cast<std::atomic<uint64_t>*>(&cb.readIndex);
}
inline std::atomic<uint64_t>& SequenceProxy(AudioChunk_POD& chunk) noexcept {
    return *reinterpret_cast<std::atomic<uint64_t>*>(&chunk.sequence);
}
inline std::atomic<uint32_t>& OverrunCountProxy(ControlBlock_POD& cb) noexcept {
    return *reinterpret_cast<std::atomic<uint32_t>*>(&cb.overrunCount);
}
inline std::atomic<uint32_t>& UnderrunCountProxy(ControlBlock_POD& cb) noexcept {
    return *reinterpret_cast<std::atomic<uint32_t>*>(&cb.underrunCount);
}

// --- Modified push/pop using POD types and proxies ---
inline bool push(ControlBlock_POD&       cb,
                 AudioChunk_POD*         ring,
                 const AudioBufferList*  src,
                 const AudioTimeStamp&   ts,
                 uint32_t                frames,
                 uint32_t                bytesPerFrame) noexcept
{
    if (!src || !ring || frames == 0) return false;
    if (frames > kMaxFramesPerChunk)  return false;
    const uint64_t rd = ReadIndexProxy(cb).load(std::memory_order_acquire);
    const uint64_t wr = WriteIndexProxy(cb).load(std::memory_order_relaxed);
    if (wr - rd >= cb.capacity)       return false;
    const uint64_t slot = wr & (cb.capacity - 1);
    AudioChunk_POD& c   = ring[slot];
    const uint32_t totalBytes = frames * bytesPerFrame;
    if (totalBytes > kAudioDataBytes) return false;
    c.timeStamp  = ts;
    c.frameCount = frames;
    c.dataBytes  = totalBytes;
    std::byte* dst = c.audio;
    for (UInt32 i = 0; i < src->mNumberBuffers; ++i) {
        const AudioBuffer& b = src->mBuffers[i];

        // --- NEW --- guard against null or 0-byte buffers
        if (b.mData == nullptr || b.mDataByteSize == 0) {
            std::memset(dst, 0, b.mDataByteSize);   // fill silence
        } else {
            std::memcpy(dst, b.mData, b.mDataByteSize);
        }
        dst += b.mDataByteSize;
    }
    std::atomic_thread_fence(std::memory_order_release);
    SequenceProxy(c).store(wr + 1, std::memory_order_relaxed);
    WriteIndexProxy(cb).store(wr + 1, std::memory_order_release);
    return true;
}

inline bool pop(ControlBlock_POD& cb,
                AudioChunk_POD*   ring,
                AudioChunk_POD&   out) noexcept
{
    const uint64_t wr = WriteIndexProxy(cb).load(std::memory_order_acquire);
    const uint64_t rd = ReadIndexProxy(cb).load(std::memory_order_relaxed);
    if (rd == wr) return false;
    const uint64_t slot = rd & (cb.capacity - 1);
    AudioChunk_POD& c   = ring[slot];
    const uint64_t expectedSequence = rd + 1;
    if (SequenceProxy(c).load(std::memory_order_acquire) != expectedSequence) {
         return false;
    }
    std::memcpy(&out, &c, sizeof(AudioChunk_POD));
    ReadIndexProxy(cb).store(rd + 1, std::memory_order_release);
    return true;
}

} // namespace RTShmRing