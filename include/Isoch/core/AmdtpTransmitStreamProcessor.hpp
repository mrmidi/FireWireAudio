#ifndef AMDTPTRANSMITSTREAMPROCESSOR_H
#define AMDTPTRANSMITSTREAMPROCESSOR_H

#include <cstdint>
#include <atomic>
#include <chrono>
#include <memory>
#include <spdlog/spdlog.h> // Use main header
#include "Isoch/utils/RingBuffer.hpp"  // RAUL RingBuffer header

// Configuration options
#define DEBUG_LOGGING 1          // Keep debug logging toggle

namespace AVS {

class AmdtpTransmitStreamProcessor {
public:
    AmdtpTransmitStreamProcessor(std::shared_ptr<spdlog::logger> logger);
    ~AmdtpTransmitStreamProcessor();

    // Push raw audio data (received via XPC) into the ring buffer.
    void pushAudioData(const void* buff, unsigned int buffBytesSize);

    void startSampleRateLogger();

    // ADDED: Method to access internal buffer state (needed for Phase 2 pump)
    [[nodiscard]] uint32_t getAvailableReadBytes() const {
        return audioBuffer_.read_space();
    }

    // ADDED: Method to read from internal buffer (needed for Phase 2 pump)
    uint32_t readData(uint32_t size, void* dst) {
         // This read is potentially called from a different thread (pump thread)
         // than pushAudioData (XPC queue). RingBuffer IS single-producer/single-consumer safe.
         uint32_t bytesRead = audioBuffer_.read(size, dst);
         if (bytesRead > 0) {
             samplesInBuffer_.fetch_sub(bytesRead / sizeof(int32_t), std::memory_order_release); // Assuming int32_t samples
         }
         return bytesRead;
    }

private:
    // Configuration constants (simplified)
    static constexpr size_t BYTES_PER_AUDIO_SAMPLE = 4; // Keep if assuming 32-bit PCM from XPC
    static constexpr size_t RING_BUFFER_SIZE = 4096; // 16 KB - Adjusted size

    // RAUL ring buffer instance.
    raul::RingBuffer audioBuffer_;

    // Simplified atomic counters for basic monitoring
    std::atomic<size_t> samplesInBuffer_{0}; // Keep for basic monitoring/debug
    std::atomic<uint64_t> totalPushedSamples_{0}; // Keep for basic monitoring/debug
    std::atomic<size_t> overflowWriteAttempts_{0}; // Renamed for clarity

    // Logger.
    std::shared_ptr<spdlog::logger> logger_;
};

} // namespace AVS

#endif // AMDTPTRANSMITSTREAMPROCESSOR_H
