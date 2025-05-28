#include "Isoch/core/IsochPacketProvider.hpp"
#include <CoreServices/CoreServices.h> // For endian swap
#include <cstring> // For memcpy/memmove/bzero
#include <algorithm> // For std::min
#include <spdlog/spdlog.h>
// Include header with AM824 constants if needed
#include "Isoch/core/TransmitterTypes.hpp" // Assuming constants are here
#include "shared/SharedMemoryStructures.hpp"
#include "xpc/FWAXPC/RingBufferManager.hpp"
#include <os/log.h>

namespace FWA {
namespace Isoch {

// --- UPDATED Constructor ---
IsochPacketProvider::IsochPacketProvider(std::shared_ptr<spdlog::logger> logger, size_t ringBufferSize)
    : logger_(std::move(logger)),
      audioBuffer_(ringBufferSize, logger_) // Initialize OWN buffer
{
    if(logger_) logger_->debug("IsochPacketProvider created with RingBuffer size {}", ringBufferSize);
    reset();
}

IsochPacketProvider::~IsochPacketProvider() {
     if(logger_) logger_->debug("IsochPacketProvider destroyed");
}

void IsochPacketProvider::reset() {
    audioBuffer_.reset(); // Reset OWN buffer
    isInitialized_ = false;
    underrunCount_ = 0;
    totalPushedBytes_ = 0;
    totalPulledBytes_ = 0;
    overflowWriteAttempts_ = 0;
     if(logger_) logger_->info("IsochPacketProvider reset");
}

// --- ADDED Implementation for pushAudioData ---
bool IsochPacketProvider::pushAudioData(const void* buffer, size_t bufferSizeInBytes) {
    if (!buffer || bufferSizeInBytes == 0) return false;

    constexpr size_t sampleSize = sizeof(int32_t); // Assuming 32-bit host PCM
    if (bufferSizeInBytes % sampleSize != 0) {
        if(logger_) logger_->warn("pushAudioData: Received data size {} not multiple of sample size {}. Ignoring.", bufferSizeInBytes, sampleSize);
        return false;
    }

    // Write to OWN ring buffer
    size_t written = audioBuffer_.write(bufferSizeInBytes, buffer);
    totalPushedBytes_ += written;

    if (written < bufferSizeInBytes) {
         overflowWriteAttempts_++;
         // Log periodically
         static auto lastWarnTime = std::chrono::steady_clock::now();
         auto now = std::chrono::steady_clock::now();
         if (now - lastWarnTime > std::chrono::seconds(1)) {
              if(logger_) logger_->warn("[pushAudioData] Ring buffer full, couldn't write {} bytes. Available space: {}. Attempts: {}",
                          bufferSizeInBytes, audioBuffer_.write_space(), overflowWriteAttempts_.load());
             lastWarnTime = now;
         }
         return false; // Indicate not all data was accepted
    }

    // Check if initial fill target is met
    if (!isInitialized_) {
        size_t fillTargetBytes = (audioBuffer_.capacity() * INITIAL_FILL_TARGET_PERCENT) / 100;
        if (audioBuffer_.read_space() >= fillTargetBytes) {
            isInitialized_ = true;
             if(logger_) logger_->info("IsochPacketProvider: Ring buffer initial fill target reached ({} bytes). Ready for streaming.", audioBuffer_.read_space());
        }
    }
    return true;
}
// --- END pushAudioData ---


// --- fillPacketData implementation (mostly unchanged, reads from own buffer) ---
PreparedPacketData IsochPacketProvider::fillPacketData(
    uint8_t* targetBuffer,
    size_t targetBufferSize,
    const TransmitPacketInfo& info)
{
    // os_log(OS_LOG_DEFAULT, "IsochPacketProvider: fillPacketData called for Segment: %d, Packet: %d, AbsPkt: %d, TargetSize: %zu",
    //        info.segmentIndex, info.packetIndexInGroup, info.absolutePacketIndex, targetBufferSize);
//    if(logger_) logger_->debug("fillPacketData called: Seg={}, Pkt={}, AbsPkt={}, TargetSize={}",
//                              info.segmentIndex, info.packetIndexInGroup, info.absolutePacketIndex, targetBufferSize);

    PreparedPacketData result;
    result.dataPtr = targetBuffer;
    result.dataLength = 0;
    result.generatedSilence = true;

    if (!targetBuffer || targetBufferSize == 0 || (targetBufferSize % sizeof(int32_t) != 0)) {
         if(logger_) logger_->error("fillPacketData: Invalid target buffer, size ({}), or size not multiple of 4.", targetBufferSize);
         return result;
    }

    // --- Check available space in OWN buffer ---
    size_t availableBeforeRead = audioBuffer_.read_space();
    if(logger_) logger_->trace("  Available read space before pull: {} bytes", availableBeforeRead);

    // --- Read data from OWN buffer ---
    size_t bytesRead = audioBuffer_.read(targetBufferSize, targetBuffer);

    if (bytesRead == targetBufferSize) {
        // --- SUCCESSFUL READ - FORMAT IN PLACE ---
//        if(logger_) logger_->debug("  Successfully pulled {} bytes. Formatting to AM824...", bytesRead);

        int32_t* samplesPtr = reinterpret_cast<int32_t*>(targetBuffer);
        size_t numSamples = targetBufferSize / sizeof(int32_t);

        // Perform the AM824 conversion and Big Endian swap IN PLACE
        for (size_t i = 0; i < numSamples; ++i) {
            int32_t sample = samplesPtr[i];
            sample &= 0x00FFFFFF;
            uint32_t am824Sample = (AM824_LABEL << LABEL_SHIFT) | sample; // Assumes constants defined
            samplesPtr[i] = OSSwapHostToBigInt32(am824Sample);
        }
         if(logger_) logger_->trace("  AM824 formatting complete.");

        result.generatedSilence = false;
        result.dataLength = bytesRead;
        totalPulledBytes_ += bytesRead; // Track pulled bytes

    } else {
        // --- UNDERRUN ---
//        if(logger_) logger_->warn("  UNDERRUN: Requested {}, pulled only {}. Available was {}.", targetBufferSize, bytesRead, availableBeforeRead);
        handleUnderrun(info);
        bzero(targetBuffer, targetBufferSize);
        result.generatedSilence = true;
        result.dataLength = targetBufferSize;
    }

    return result;
}

// --- isReadyForStreaming and handleUnderrun remain the same ---
bool IsochPacketProvider::isReadyForStreaming() const {
    // Check OWN buffer
    size_t fillTargetBytes = (audioBuffer_.capacity() * INITIAL_FILL_TARGET_PERCENT) / 100;
    return audioBuffer_.read_space() >= fillTargetBytes;
    // return isInitialized_.load(); // Or use the flag if preferred
}

void IsochPacketProvider::handleUnderrun(const TransmitPacketInfo& info) {
    underrunCount_++;
    if (underrunCount_ % 100 == 1) {
         if(logger_) logger_->warn("IsochPacketProvider: Buffer underrun detected at Seg={}, Pkt={}, AbsPkt={}. Total Count={}",
                                  info.segmentIndex, info.packetIndexInGroup, info.absolutePacketIndex, underrunCount_.load());
    }
}

} // namespace Isoch
} // namespace FWA
