#pragma once
#include <atomic>
#include <cstdint>

constexpr size_t MAX_AUDIO_CHUNK_BYTES = 4096; // Example size, adjust as needed
constexpr size_t TRANSMIT_RING_CAPACITY = 128; // Example ring size

struct AudioTransmitChunk {
    uint64_t hostPresentationTimeAbs;
    uint64_t startAbsSampleFrame;
    uint32_t frameCount;
    uint32_t dataSizeBytes;
    std::atomic<uint64_t> sequenceNumber;
    uint8_t data[MAX_AUDIO_CHUNK_BYTES];
};

struct SharedTransmitControl {
    std::atomic<uint64_t> writeIndex;
    std::atomic<uint64_t> readIndex;
    size_t ringBufferCapacity;
    // Add more fields as needed for synchronization, status, etc.
    // Optionally, add overrun/underrun counters, flags, etc.
};
