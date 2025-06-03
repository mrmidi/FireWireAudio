// SharedMemoryStructures.hpp (refactored)
#pragma once
#include <CoreAudio/AudioServerPlugIn.h> 
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

constexpr std::size_t kDestructiveCL     = 64;
constexpr std::size_t kMaxFramesPerChunk = 1024; 
constexpr std::size_t kMaxChannels       = 2;
constexpr std::size_t kMaxBytesPerSample = 4;
constexpr std::size_t kMaxBytesPerFrame  = kMaxChannels * kMaxBytesPerSample;
constexpr std::size_t kRingCapacityPow2  = 512; // TEST
static_assert((kRingCapacityPow2 & (kRingCapacityPow2 - 1)) == 0);
constexpr std::size_t kAudioDataBytes = kMaxFramesPerChunk * kMaxBytesPerFrame;
constexpr uint32_t    kShmVersion     = 3;

namespace RTShmRing {

// --- POD Structures ---

struct alignas(kDestructiveCL) AudioChunk_POD {
    AudioTimeStamp timeStamp{};
    uint32_t       frameCount{0};
    uint32_t       dataBytes{0};
    uint64_t       sequence{0};
    std::byte      audio[kAudioDataBytes]{};
};
// static_assert(sizeof(AudioChunk_POD) <= 4096);
static_assert(sizeof(AudioChunk_POD) % kDestructiveCL == 0);




// struct alignas(kDestructiveCL) ControlBlock_POD {
//     uint32_t abiVersion;      // = 2
//     uint32_t capacity;        // ring length
//     uint32_t sampleRateHz;    // e.g. 44100
//     uint32_t channelCount;    // e.g. 2
//     uint32_t bytesPerFrame;   // = channelCount * bytesPerSample
//     uint64_t writeIndex;
//     char     pad0[kDestructiveCL
//                    - sizeof(uint32_t)*5
//                    - sizeof(uint64_t)];
//     uint64_t readIndex;
//     char     pad1[kDestructiveCL - sizeof(uint64_t)];
//     uint32_t overrunCount;
//     uint32_t underrunCount;
//     uint32_t streamActive;    // 0 = idle, 1 = running
//     uint32_t reserved;        // keep 64-byte alignment
// };

struct alignas(kDestructiveCL) ControlBlock_POD {
    uint32_t abiVersion;      // 0
    uint32_t capacity;        // 4
    uint32_t sampleRateHz;    // 8
    uint32_t channelCount;    // 12
    uint32_t bytesPerFrame;   // 16
    uint32_t _padWriteAlign;  // 20 - NEW: explicit padding
    uint64_t writeIndex;      // 24 - now 8-byte aligned
    char     pad0[kDestructiveCL - 6*sizeof(uint32_t) - sizeof(uint64_t)];
    uint64_t readIndex;       // Already aligned due to cache line boundary
    char     pad1[kDestructiveCL - sizeof(uint64_t)];
    uint32_t overrunCount;
    uint32_t underrunCount;
    uint32_t streamActive;
    uint32_t reserved;
};

// Add compile-time verification
static_assert(offsetof(ControlBlock_POD, writeIndex) % 8 == 0, "writeIndex must be 8-byte aligned");
static_assert(offsetof(ControlBlock_POD, readIndex) % 8 == 0, "readIndex must be 8-byte aligned");


static_assert(sizeof(ControlBlock_POD) % kDestructiveCL == 0);

struct alignas(kDestructiveCL) SharedRingBuffer_POD
{
    ControlBlock_POD control;
    AudioChunk_POD   ring[kRingCapacityPow2];
};

// --- Format Validation Helpers ---
inline bool ValidateFormat(const ControlBlock_POD& cb) noexcept {
    if (cb.abiVersion != kShmVersion) return false;
    if (cb.sampleRateHz == 0 || cb.channelCount == 0) return false;
    if (cb.channelCount > kMaxChannels) return false;
    if (cb.bytesPerFrame != cb.channelCount * kMaxBytesPerSample) return false;
    
    // NEW: Validate capacity is power-of-two and reasonable
    if (cb.capacity == 0) return false;
    if ((cb.capacity & (cb.capacity - 1)) != 0) return false;  // Must be power of 2
    if (cb.capacity > 65536) return false;  // Reasonable upper limit
    
    return true;
}

// --- Atomic Proxies ---
inline std::atomic<uint64_t>& WriteIndexProxy(ControlBlock_POD& cb) noexcept {
    return *reinterpret_cast<std::atomic<uint64_t>*>(&cb.writeIndex);
}
inline std::atomic<uint64_t>& ReadIndexProxy(ControlBlock_POD& cb) noexcept {
    return *reinterpret_cast<std::atomic<uint64_t>*>(&cb.readIndex);
}
inline std::atomic<uint64_t>& SequenceProxy(AudioChunk_POD& c) noexcept {
    return *reinterpret_cast<std::atomic<uint64_t>*>(&c.sequence);
}
inline std::atomic<uint32_t>& OverrunCountProxy(ControlBlock_POD& cb) noexcept {
    return *reinterpret_cast<std::atomic<uint32_t>*>(&cb.overrunCount);
}
inline std::atomic<uint32_t>& UnderrunCountProxy(ControlBlock_POD& cb) noexcept {
    return *reinterpret_cast<std::atomic<uint32_t>*>(&cb.underrunCount);
}

// --- push → unchanged except format check ---
inline bool push(ControlBlock_POD&       cb,
                 AudioChunk_POD*         ring,
                 const AudioBufferList*  src,
                 const AudioTimeStamp&   ts,
                 uint32_t                frames,
                 uint32_t                bpf) noexcept
{
    if (!ValidateFormat(cb)) return false;
    if (!src || !ring || frames==0 || frames>kMaxFramesPerChunk) return false;
    auto rd = ReadIndexProxy(cb).load(std::memory_order_acquire);
    auto wr = WriteIndexProxy(cb).load(std::memory_order_relaxed);
    if (wr - rd >= cb.capacity) return false;

    auto slot = wr & (cb.capacity-1);
    auto& c   = ring[slot];
    auto totalBytes = frames * bpf;
    if (totalBytes > kAudioDataBytes) return false;

    c.timeStamp  = ts;
    c.frameCount = frames;
    c.dataBytes  = totalBytes;

    auto dst = c.audio;
    for (UInt32 i=0; i<src->mNumberBuffers; ++i) {
        auto& b = src->mBuffers[i];
        if (!b.mData || b.mDataByteSize==0)
            std::memset(dst,0,b.mDataByteSize);
        else
            std::memcpy(dst,b.mData,b.mDataByteSize);
        dst += b.mDataByteSize;
    }

    std::atomic_thread_fence(std::memory_order_release);
    SequenceProxy(c).store(wr+1, std::memory_order_release);
    WriteIndexProxy(cb).store(wr+1, std::memory_order_release);
    return true;
}

// --- zero-copy pop → new API for packet provider ---
// FIXED pop() function - remove const parameters to avoid const_cast
inline bool pop(ControlBlock_POD&       cb,           // CHANGED: remove const
                AudioChunk_POD*         ring,         // CHANGED: remove const  
                AudioTimeStamp&         tsOut,
                uint32_t&               bytesOut,
                const std::byte*&       audioPtrOut) noexcept
{
    if (!ValidateFormat(cb)) return false;

    static thread_local bool inUnderrun = false;

    
    // CRITICAL FIX: Use WriteIndexProxy for wr, not ReadIndexProxy!
    const uint64_t wr = WriteIndexProxy(cb).load(std::memory_order_acquire);
    const uint64_t rd = ReadIndexProxy(cb).load(std::memory_order_relaxed);
    if (rd == wr) {
        // only bump once per contiguous underrun run
        if (!inUnderrun) {
            UnderrunCountProxy(cb).fetch_add(1, std::memory_order_relaxed);
            inUnderrun = true;
        }
        return false;
    }
    inUnderrun = false;            // we have data again

    const uint64_t slot = rd & (cb.capacity - 1);
    AudioChunk_POD& c = ring[slot];
    
    if (SequenceProxy(c).load(std::memory_order_acquire) != rd + 1)
        return false;

    tsOut       = c.timeStamp;
    bytesOut    = c.dataBytes;
    audioPtrOut = c.audio;

    ReadIndexProxy(cb).store(rd + 1, std::memory_order_release);
    return true;
}

inline std::atomic<uint32_t>& StreamActiveProxy(ControlBlock_POD& cb) noexcept {
    return *reinterpret_cast<std::atomic<uint32_t>*>(&cb.streamActive);
}

} // namespace RTShmRing