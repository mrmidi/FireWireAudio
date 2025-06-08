// IsochPacketProvider.hpp - Direct SHM Implementation with Safety Margin
#pragma once

#include "Isoch/interfaces/ITransmitPacketProvider.hpp"
#include "shared/SharedMemoryStructures.hpp"
#include <atomic>
#include <chrono>
#include <spdlog/spdlog.h>

#include <vector> //for fillLevelHistogram_
#include <map>    // for fillLevelHistogram_

namespace FWA {
namespace Isoch {

class IsochPacketProvider : public ITransmitPacketProvider {
private:
    // --- Telemetry for SHM Fill Level ---
    mutable std::mutex histogramMutex_; // Protects histogram access if queried from another thread
    static constexpr uint32_t HISTOGRAM_MAX_BINS = 1024; // Max fill level to track individually
    mutable std::vector<std::atomic<uint64_t>> fillLevelHistogram_;
    mutable std::atomic<uint64_t> fillLevelOverflowCount_{0}; // Count for fill levels > HISTOGRAM_MAX_BINS
    mutable std::chrono::steady_clock::time_point lastHistogramLogTime_;
    static constexpr auto HISTOGRAM_LOG_INTERVAL = std::chrono::seconds(10); // Log histogram every 10s
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

    // --- Safety Margin Configuration ---
    void setSafetyMarginChunks(uint32_t chunks);
    uint32_t getSafetyMarginChunks() const { return safetyMarginChunks_; }


    // Get diagnostics and reset them
    
    DiagnosticStats getDiagnostics() const;
    void resetDiagnostics();

    std::map<uint32_t, uint64_t> getFillLevelHistogram() const; // Ensure this declaration exists
    void resetFillLevelHistogram();                             // Ensure this declaration exists



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

    // --- NEW: Priming State for refined buffer management ---
    mutable std::atomic<bool> isPriming_{true};  // Start in priming state
    mutable std::atomic<uint64_t> packetsProcessedInPriming_{0};

    // Configuration/Constants
    static constexpr uint32_t AM824_LABEL = 0x40;     // 24-bit audio label
    static constexpr uint32_t LABEL_SHIFT = 24;       // Shift for 24-bit label
    static constexpr size_t INITIAL_FILL_TARGET_PERCENT = 25;

    // --- NEW: Safety Margin Constants ---
    static constexpr uint32_t MIN_SAFETY_MARGIN = 2;      // Absolute minimum safety margin
    static constexpr uint32_t MAX_SAFETY_MARGIN_PERCENT = 20; // Max 25% of capacity as safety margin
    static constexpr uint32_t SAFETY_ADJUST_INTERVAL_MS = 1000; // Adjust safety margin every 1 second
    static constexpr uint32_t HIGH_WATER_MARK_PERCENT = 40;    // 75% full - increase safety
    static constexpr uint32_t LOW_WATER_MARK_PERCENT = 20;     // 25% full - decrease safety

    // --- Helper Methods ---
    void logFillLevelHistogram() const; // New helper to print the histogram
    bool popNextChunk();
    void handleUnderrun(const TransmitPacketInfo& info);
    void formatToAM824InPlace(uint8_t* buffer, size_t bufferSize) const;
    uint32_t getCurrentShmFillLevel() const;
    bool validateShmFormat() const;

    /**
     * @brief Gets the number of bytes remaining to be consumed in the current cached audio chunk.
     * @return Number of remaining bytes, or 0 if no valid chunk is cached or all bytes consumed.
     */
    inline uint32_t remainingBytesInCurrentChunk() const noexcept {
        if (currentChunk_.valid && currentChunk_.totalBytes > currentChunk_.consumedBytes) {
            return currentChunk_.totalBytes - currentChunk_.consumedBytes;
        }
        return 0;
    }

    /**
     * @brief Gets a pointer to the current read position within the cached audio chunk's data.
     * @return Pointer to the next byte to be consumed, or nullptr if no valid chunk or fully consumed.
     */
    inline const std::byte* currentChunkReadPtr() const noexcept {
        if (currentChunk_.valid && currentChunk_.totalBytes > currentChunk_.consumedBytes) {
            return currentChunk_.audioDataPtr + currentChunk_.consumedBytes;
        }
        return nullptr;
    }

    // SAFETY MARGIN CONSTANTS AND DECLARATIONS
    static constexpr uint32_t kMinChunksToPop = 8; // Example: Require at least 4 chunks available

    // Helper to check if we have enough beyond the minimum threshold
    bool hasSufficientDataForPop() const;

    // --- Safety Margin Helper Methods ---
    bool hasMinimumFillLevel() const;
    void adjustSafetyMargin();
    void generateProactiveSilence(uint8_t* targetBuffer, size_t targetBufferSize, 
                                  PreparedPacketData& result, const TransmitPacketInfo& info);
};

} // namespace Isoch
} // namespace FWA