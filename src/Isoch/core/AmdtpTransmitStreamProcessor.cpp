#include "Isoch/core/AmdtpTransmitStreamProcessor.hpp"
#include <iostream>
#include <chrono>
#include <cstring>
#include <spdlog/spdlog.h>
#include <thread> // Keep for logger thread

namespace AVS {

AmdtpTransmitStreamProcessor::AmdtpTransmitStreamProcessor(std::shared_ptr<spdlog::logger> logger)
: audioBuffer_(RING_BUFFER_SIZE, logger), // Initialize RingBuffer with logger
  logger_(logger)
{
#if DEBUG_LOGGING
    logger_->info("[AmdtpTransmitStreamProcessor] Initialized Simplified (size: {} bytes)", RING_BUFFER_SIZE);
    startSampleRateLogger(); // Keep logger thread if desired for debug
#endif
}

AmdtpTransmitStreamProcessor::~AmdtpTransmitStreamProcessor() {
    // RAUL::RingBuffer cleans up automatically.
#if DEBUG_LOGGING
    logger_->info("[AmdtpTransmitStreamProcessor] Simplified Destroyed.");
#endif
}

void AmdtpTransmitStreamProcessor::pushAudioData(const void* buff, unsigned int buffBytesSize) {
    if (!buff || buffBytesSize == 0) {
        return;
    }

    // Simplified Direct Ring Buffer Write
    size_t bytesToWrite = buffBytesSize;
    const uint8_t* bufferPtr = static_cast<const uint8_t*>(buff);

    uint32_t written = audioBuffer_.write(bytesToWrite, bufferPtr);

    if (written < bytesToWrite) {
        // Buffer was full or couldn't accept all data at once.
        // The RingBuffer's write() only writes if the *entire* size fits.
        overflowWriteAttempts_.fetch_add(1, std::memory_order_relaxed);
#if DEBUG_LOGGING
         // Log periodically to avoid spamming
         static auto lastWarnTime = std::chrono::steady_clock::now();
         auto now = std::chrono::steady_clock::now();
         if (now - lastWarnTime > std::chrono::seconds(1)) {
             logger_->warn("[pushAudioData] Ring buffer full, couldn't write {} bytes. Available space: {}. Attempts: {}",
                          bytesToWrite, audioBuffer_.write_space(), overflowWriteAttempts_.load());
             lastWarnTime = now;
         }
#endif
        // Data was dropped because it didn't fit
    } else {
        // Successfully wrote the data
        // Assuming 32-bit PCM data as input based on original code context
        size_t samplesWritten = written / sizeof(int32_t);
        totalPushedSamples_.fetch_add(samplesWritten, std::memory_order_relaxed);
        samplesInBuffer_.fetch_add(samplesWritten, std::memory_order_release);
    }
}

void AmdtpTransmitStreamProcessor::startSampleRateLogger() {
#if DEBUG_LOGGING
    std::thread([this]() {
        using namespace std::chrono;
        auto lastTime = steady_clock::now();
        uint64_t lastSampleCount = totalPushedSamples_.load(std::memory_order_relaxed);
        size_t lastOverflowCount = overflowWriteAttempts_.load(std::memory_order_relaxed);
        while (true) { // Consider adding a proper stop mechanism if needed
            std::this_thread::sleep_for(seconds(2)); // Log less frequently
            auto now = steady_clock::now();
            uint64_t currentSampleCount = totalPushedSamples_.load(std::memory_order_relaxed);
            size_t currentOverflowCount = overflowWriteAttempts_.load(std::memory_order_relaxed);

            double elapsedSec = duration_cast<duration<double>>(now - lastTime).count();
            uint64_t samplesInInterval = currentSampleCount - lastSampleCount;
            size_t overflowsInInterval = currentOverflowCount - lastOverflowCount;
            double samplesPerSecond = (elapsedSec > 0) ? (samplesInInterval / elapsedSec) : 0.0;
            size_t currentSamplesBuffered = samplesInBuffer_.load(std::memory_order_relaxed);

            logger_->debug("[ProcessorStats] Pushed ~{:.0f} samples/sec. CurrentBuffered: {}. OverflowWrites: {}",
                         samplesPerSecond, currentSamplesBuffered, overflowsInInterval);

            lastSampleCount = currentSampleCount;
            lastOverflowCount = currentOverflowCount;
            lastTime = now;
        }
    }).detach();
#endif
}

} // namespace FWA
