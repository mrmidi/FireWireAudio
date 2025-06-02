// IsochPacketProvider.hpp - Direct SHM Implementation with Safety Margin
#pragma once

#include "Isoch/interfaces/ITransmitPacketProvider.hpp"
#include "shared/SharedMemoryStructures.hpp"
#include <atomic>
#include <chrono>
#include <spdlog/spdlog.h>

namespace FWA {
namespace Isoch {

class IsochPacketProvider : public ITransmitPacketProvider {
private:
    mutable std::mutex bindMutex_;  // Protect bind/unbind operations
public:
    explicit IsochPacketProvider(std::shared_ptr<spdlog::logger> logger);
    ~IsochPacketProvider() override;

    // Prevent Copy
    IsochPacketProvider(const IsochPacketProvider&) = delete;
    IsochPacketProvider& operator=(const IsochPacketProvider&) = delete;

    // --- NEW: Direct SHM Binding Interface ---
    bool bindSharedMemory(RTShmRing::ControlBlock_POD* controlBlock,
                          RTShmRing::AudioChunk_POD* ringArray);
    void unbindSharedMemory();
    bool isBound() const { return shmControlBlock_ != nullptr; }

    // --- ITransmitPacketProvider Interface ---
    bool pushAudioData(const void* buffer, size_t bufferSizeInBytes) override;
    
    PreparedPacketData fillPacketData(
        uint8_t* targetBuffer,
        size_t targetBufferSize,
        const TransmitPacketInfo& info
    ) override;

    bool isReadyForStreaming() const override;
    void reset() override;

    // --- Enhanced Diagnostics ---
    struct DiagnosticStats {
        uint64_t totalBytesConsumed;
        uint64_t shmPopCount;
        uint64_t shmUnderrunCount;
        uint64_t formatValidationErrors;
        uint64_t partialChunkConsumptions;
        double avgFillPacketDurationUs;
        uint32_t currentShmFillPercent;
        // --- NEW: Safety Margin Diagnostics ---
        uint64_t safetyMarginHolds;
        uint32_t currentSafetyMarginChunks;
        uint64_t safetyMarginAdjustments;
    };
    
    DiagnosticStats getDiagnostics() const;
    void resetDiagnostics();

    // --- NEW: Safety Margin Configuration ---
    void setSafetyMarginChunks(uint32_t chunks);
    uint32_t getSafetyMarginChunks() const { return safetyMarginChunks_; }

private:

    // --- SHM Cursor State ---
    RTShmRing::ControlBlock_POD* shmControlBlock_ {nullptr};
    RTShmRing::AudioChunk_POD*   shmRingArray_    {nullptr};
    
    // Current chunk cache to minimize SHM access
    struct ChunkCache {
        AudioTimeStamp timeStamp;
        uint32_t totalBytes;
        const std::byte* audioDataPtr;
        uint32_t consumedBytes;
        bool valid;
        
        void invalidate() { valid = false; consumedBytes = 0; audioDataPtr = nullptr; }
        uint32_t remainingBytes() const { return valid ? (totalBytes - consumedBytes) : 0; }
    } currentChunk_;

    // --- Core Components ---
    std::shared_ptr<spdlog::logger> logger_;
    
    // --- Diagnostics & State ---
    mutable std::atomic<uint64_t> totalBytesConsumed_{0};
    mutable std::atomic<uint64_t> shmPopCount_{0};
    mutable std::atomic<uint64_t> shmUnderrunCount_{0};
    mutable std::atomic<uint64_t> formatValidationErrors_{0};
    mutable std::atomic<uint64_t> partialChunkConsumptions_{0};
    
    // Performance timing
    mutable std::chrono::steady_clock::time_point lastStatsTime_;
    mutable std::atomic<uint64_t> fillPacketCallCount_{0};
    mutable std::atomic<uint64_t> totalFillPacketTimeNs_{0};

    // --- NEW: Safety Margin State ---
    mutable std::atomic<uint32_t> safetyMarginChunks_{4};  // Default 4 chunks safety margin
    mutable std::atomic<uint64_t> safetyMarginHolds_{0};   // Count of times we held back due to safety
    mutable std::atomic<uint64_t> safetyMarginAdjustments_{0}; // Count of safety margin changes
    mutable std::chrono::steady_clock::time_point lastSafetyAdjustTime_;

    // Configuration/Constants
    static constexpr uint32_t AM824_LABEL = 0x40;     // 24-bit audio label
    static constexpr uint32_t LABEL_SHIFT = 24;       // Shift for 24-bit label
    static constexpr size_t INITIAL_FILL_TARGET_PERCENT = 25;

    // --- NEW: Safety Margin Constants ---
    static constexpr uint32_t MIN_SAFETY_MARGIN = 2;      // Absolute minimum safety margin
    static constexpr uint32_t MAX_SAFETY_MARGIN_PERCENT = 25; // Max 25% of capacity as safety margin
    static constexpr uint32_t SAFETY_ADJUST_INTERVAL_MS = 1000; // Adjust safety margin every 1 second
    static constexpr uint32_t HIGH_WATER_MARK_PERCENT = 75;    // 75% full - increase safety
    static constexpr uint32_t LOW_WATER_MARK_PERCENT = 25;     // 25% full - decrease safety

    // --- Helper Methods ---
    bool popNextChunk();
    void handleUnderrun(const TransmitPacketInfo& info);
    void formatToAM824InPlace(uint8_t* buffer, size_t bufferSize) const;
    uint32_t getCurrentShmFillLevel() const;
    bool validateShmFormat() const;

    // --- NEW: Safety Margin Helper Methods ---
    bool hasMinimumFillLevel() const;
    void adjustSafetyMargin();
    void generateProactiveSilence(uint8_t* targetBuffer, size_t targetBufferSize, 
                                  PreparedPacketData& result, const TransmitPacketInfo& info);
};

} // namespace Isoch
} // namespace FWA