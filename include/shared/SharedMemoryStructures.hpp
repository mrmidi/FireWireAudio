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

namespace RTShmRing {

// ---------- Data chunk ----------
struct alignas(kDestructiveCL) AudioChunk
{
    AudioTimeStamp        timeStamp   {};               // first frame
    uint32_t              frameCount  {0};              // real frames
    uint32_t              dataBytes   {0};              // real bytes in audio[]
    std::atomic<uint64_t> sequence    {0};              // writer commits with seq = globalIdx+1
    std::byte             audio[kAudioDataBytes] {};    // PCM payload

    void storeSequence(uint64_t seq) noexcept
    {
        std::atomic_thread_fence(std::memory_order_release);
        sequence.store(seq, std::memory_order_release);
    }
    [[nodiscard]] bool loadIfFresh(uint64_t expected) const noexcept
    {
        return sequence.load(std::memory_order_acquire) == expected;
    }
};

// ---------- Control block ----------
struct alignas(kDestructiveCL) ControlBlock
{
    std::atomic<uint64_t> writeIndex {0};                               // cache line #1
    char                  pad0[kDestructiveCL - sizeof(writeIndex)];

    std::atomic<uint64_t> readIndex  {0};                               // cache line #2
    char                  pad1[kDestructiveCL - sizeof(readIndex)];

    const uint32_t        capacity   {kRingCapacityPow2};

    // non-RT statistics (update from low-prio thread)
    std::atomic<uint32_t> overrunCount  {0};
    std::atomic<uint32_t> underrunCount {0};
};

// ---------- Shared region ----------
struct alignas(kDestructiveCL) SharedRingBuffer
{
    ControlBlock control;
    AudioChunk   ring[kRingCapacityPow2];
};

// ---------- Inline push / pop ----------
inline bool push(ControlBlock&          cb,
                 AudioChunk*            ring,
                 const AudioBufferList* src,
                 const AudioTimeStamp&  ts,
                 uint32_t               frames,
                 uint32_t               bytesPerFrame) noexcept
{
    if (!src || !ring || frames == 0) return false;
    if (frames > kMaxFramesPerChunk)  return false;

    const uint64_t rd = cb.readIndex .load(std::memory_order_acquire);
    const uint64_t wr = cb.writeIndex.load(std::memory_order_relaxed);
    if (wr - rd >= cb.capacity)       return false;           // overrun

    const uint64_t slot = wr & (cb.capacity - 1);
    AudioChunk& c       = ring[slot];

    const uint32_t totalBytes = frames * bytesPerFrame;
    if (totalBytes > kAudioDataBytes) return false;

    c.timeStamp  = ts;
    c.frameCount = frames;
    c.dataBytes  = totalBytes;

    std::byte* dst = c.audio;
    for (UInt32 i = 0; i < src->mNumberBuffers; ++i) {
        const AudioBuffer& b = src->mBuffers[i];
        std::memcpy(dst, b.mData, b.mDataByteSize);
        dst += b.mDataByteSize;
    }

    c.storeSequence(wr + 1);
    cb.writeIndex.store(wr + 1, std::memory_order_release);
    return true;
}

inline bool pop(ControlBlock& cb,
                AudioChunk*   ring,
                AudioChunk&   out) noexcept
{
    const uint64_t wr = cb.writeIndex.load(std::memory_order_acquire);
    const uint64_t rd = cb.readIndex .load(std::memory_order_relaxed);
    if (rd == wr) return false;                              // underrun

    const uint64_t slot = rd & (cb.capacity - 1);
    const AudioChunk& c = ring[slot];

    if (!c.loadIfFresh(rd + 1)) return false;                // writer not done

    std::memcpy(&out, &c, sizeof(AudioChunk));
    cb.readIndex.store(rd + 1, std::memory_order_release);
    return true;
}

} // namespace RTShmRing