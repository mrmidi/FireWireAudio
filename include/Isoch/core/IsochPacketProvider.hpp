#pragma once

#include "Isoch/interfaces/ITransmitPacketProvider.hpp"
#include "Isoch/utils/RingBuffer.hpp" // Include RingBuffer - WE OWN IT NOW
#include <atomic>
#include <chrono>
#include <spdlog/spdlog.h> // Use main spdlog header

namespace FWA {
namespace Isoch {

class IsochPacketProvider : public ITransmitPacketProvider {
public:
    // --- UPDATED Constructor: Takes buffer size ---
    explicit IsochPacketProvider(std::shared_ptr<spdlog::logger> logger,
                                 size_t ringBufferSize = 131072); // Default size
    ~IsochPacketProvider() override;

    // Prevent Copy
    IsochPacketProvider(const IsochPacketProvider&) = delete;
    IsochPacketProvider& operator=(const IsochPacketProvider&) = delete;

    // --- ADDED: Method for XPC Bridge to call ---
    bool pushAudioData(const void* buffer, size_t bufferSizeInBytes) override;
    // ------------------------------------------

    PreparedPacketData fillPacketData(
        uint8_t* targetBuffer,
        size_t targetBufferSize,
        const TransmitPacketInfo& info
    ) override;

    bool isReadyForStreaming() const override;
    void reset() override;

    // --- ADDED: Helper for XPC Bridge to get stats if needed ---
    [[nodiscard]] uint32_t getAvailableReadBytes() const {
        return audioBuffer_.read_space();
    }
    [[nodiscard]] uint32_t getAvailableWriteBytes() const {
        return audioBuffer_.write_space();
    }
    // -----------------------------------------------------------

private:
    void handleUnderrun(const TransmitPacketInfo& info);

    std::shared_ptr<spdlog::logger> logger_;
    // --- ADDED back: Own the RingBuffer ---
    raul::RingBuffer audioBuffer_;
    // -------------------------------------

    std::atomic<bool> isInitialized_{false};
    std::atomic<size_t> underrunCount_{0};

    // Configuration/Constants
    static constexpr size_t INITIAL_FILL_TARGET_PERCENT = 50;
    // Constants for AM824 conversion (Ensure these are defined/included correctly)
    static constexpr uint32_t AM824_LABEL = 0x40;
    static constexpr uint32_t LABEL_SHIFT = 24;

    // Stats counters (now internal to this class)
    std::chrono::steady_clock::time_point lastStatsTime_;
    std::atomic<uint64_t> totalPushedBytes_{0};
    std::atomic<uint64_t> totalPulledBytes_{0};
    std::atomic<size_t> overflowWriteAttempts_{0}; // For pushAudioData failures

};

} // namespace Isoch
} // namespace FWA