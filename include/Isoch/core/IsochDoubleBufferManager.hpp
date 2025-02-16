#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

namespace spdlog {
    class logger;
}

namespace FWA {
namespace Isoch {

/**
 * @class IsochDoubleBufferManager
 * @brief Manages double-buffering for isochronous data to separate reception from processing.
 * 
 * This class implements a double-buffering mechanism to prevent deadlocks in audio processing.
 * One buffer is used for writing incoming data from FireWire DCL callbacks, while the other
 * is used for reading/processing by the audio processing thread.
 */
class IsochDoubleBufferManager {
public:
    /**
     * @struct BufferSegment
     * @brief Represents a segment of audio data within a buffer.
     */
    struct BufferSegment {
        uint8_t* data;
        size_t size;
    };

    /**
     * @brief Constructor
     * @param logger Logger instance for debugging
     * @param numSegments Number of segments per buffer
     * @param cyclesPerSegment Number of FireWire cycles per segment
     * @param cycleBufferSize Size of each cycle buffer in bytes
     */
    IsochDoubleBufferManager(std::shared_ptr<spdlog::logger> logger,
                             uint32_t numSegments,
                             uint32_t cyclesPerSegment,
                             uint32_t cycleBufferSize);

    /**
     * @brief Destructor
     */
    ~IsochDoubleBufferManager();

    /**
     * @brief Initialize the buffer manager with actual memory
     * @param baseBuffer Pointer to pre-allocated memory for all buffers
     * @param totalSize Total size of the provided memory in bytes
     * @return True if initialization succeeded, false otherwise
     */
    bool initialize(uint8_t* baseBuffer, size_t totalSize);
    
    /**
     * @brief Get pointer to current write segment for specified segment index
     * @param segment Segment index
     * @return Pointer to segment data or nullptr if invalid
     */
    uint8_t* getWriteSegmentPtr(uint32_t segment);
    
    /**
     * @brief Get pointer to current read segment for specified segment index
     * @param segment Segment index
     * @return Pointer to segment data or nullptr if invalid
     */
    uint8_t* getReadSegmentPtr(uint32_t segment);
    
    /**
     * @brief Mark a segment in the write buffer as complete
     * @param segment Segment index
     */
    void markSegmentComplete(uint32_t segment);
    
    /**
     * @brief Mark a segment in the read buffer as processed
     * @param segment Segment index
     */
    void markSegmentProcessed(uint32_t segment);
    
    /**
     * @brief Check if a segment in the write buffer is complete
     * @param segment Segment index
     * @return True if the segment is complete
     */
    bool isSegmentComplete(uint32_t segment) const;
    
    /**
     * @brief Check if a segment in the read buffer is processed
     * @param segment Segment index
     * @return True if the segment is processed
     */
    bool isSegmentProcessed(uint32_t segment) const;
    
    /**
     * @brief Check if current write buffer is completely filled
     * @return True if all segments in the write buffer are complete
     */
    bool isWriteBufferFull() const;
    
    /**
     * @brief Check if current read buffer has all data processed
     * @return True if all segments in the read buffer are processed
     */
    bool isReadBufferEmpty() const;
    
    /**
     * @brief Try to swap the write buffer to become the read buffer
     * @return True if swap succeeded, false otherwise
     */
    bool trySwapBuffers();
    
    /**
     * @brief Get number of segments per buffer
     * @return Number of segments
     */
    uint32_t getNumSegments() const { return numSegments_; }
    
    /**
     * @brief Get segment size in bytes (total size per segment)
     * @return Segment size in bytes
     */
    size_t getSegmentSize() const { return segmentSize_; }
    
    /**
     * @brief Get active write buffer index
     * @return 0 for buffer A, 1 for buffer B
     */
    uint32_t getActiveWriteBufferIndex() const { return writeBufferIndex_.load(); }
    
    /**
     * @brief Get active read buffer index
     * @return 0 for buffer A, 1 for buffer B
     */
    uint32_t getActiveReadBufferIndex() const { return readBufferIndex_.load(); }

private:
    std::shared_ptr<spdlog::logger> logger_;
    
    // Buffer configuration
    uint32_t numSegments_;
    uint32_t cyclesPerSegment_;
    uint32_t cycleBufferSize_;
    size_t segmentSize_;  // Total bytes per segment = cyclesPerSegment * cycleBufferSize
    
    // Direct allocation of arrays for better control
    std::atomic<bool>* segmentCompleteA_;    // Completion flags for buffer A
    std::atomic<bool>* segmentProcessedA_;   // Processing flags for buffer A
    BufferSegment* segmentsA_;               // Segment data for buffer A
    
    std::atomic<bool>* segmentCompleteB_;    // Completion flags for buffer B
    std::atomic<bool>* segmentProcessedB_;   // Processing flags for buffer B
    BufferSegment* segmentsB_;               // Segment data for buffer B
    
    // Memory management
    uint8_t* mainBuffer_;     // All allocated memory (owned)
    size_t totalBufferSize_;  // Total buffer size in bytes
    
    // Current buffer state
    std::atomic<uint32_t> writeBufferIndex_{0};  // 0 = bufferA, 1 = bufferB
    std::atomic<uint32_t> readBufferIndex_{1};   // 1 = bufferB, 0 = bufferA
    
    // Buffer state tracking
    std::atomic<bool> bufferAReady_{false};
    std::atomic<bool> bufferBReady_{false};
    
    // Helpers for accessing the correct buffer based on current indices
    std::atomic<bool>* getWriteCompleteFlags() { return writeBufferIndex_.load() == 0 ? segmentCompleteA_ : segmentCompleteB_; }
    std::atomic<bool>* getReadCompleteFlags() { return readBufferIndex_.load() == 0 ? segmentCompleteA_ : segmentCompleteB_; }
    std::atomic<bool>* getWriteProcessedFlags() { return writeBufferIndex_.load() == 0 ? segmentProcessedA_ : segmentProcessedB_; }
    std::atomic<bool>* getReadProcessedFlags() { return readBufferIndex_.load() == 0 ? segmentProcessedA_ : segmentProcessedB_; }
    BufferSegment* getWriteSegments() { return writeBufferIndex_.load() == 0 ? segmentsA_ : segmentsB_; }
    BufferSegment* getReadSegments() { return readBufferIndex_.load() == 0 ? segmentsA_ : segmentsB_; }
    
    // Const versions of the accessors
    const std::atomic<bool>* getWriteCompleteFlags() const { return writeBufferIndex_.load() == 0 ? segmentCompleteA_ : segmentCompleteB_; }
    const std::atomic<bool>* getReadCompleteFlags() const { return readBufferIndex_.load() == 0 ? segmentCompleteA_ : segmentCompleteB_; }
    const std::atomic<bool>* getWriteProcessedFlags() const { return writeBufferIndex_.load() == 0 ? segmentProcessedA_ : segmentProcessedB_; }
    const std::atomic<bool>* getReadProcessedFlags() const { return readBufferIndex_.load() == 0 ? segmentProcessedA_ : segmentProcessedB_; }
    const BufferSegment* getWriteSegments() const { return writeBufferIndex_.load() == 0 ? segmentsA_ : segmentsB_; }
    const BufferSegment* getReadSegments() const { return readBufferIndex_.load() == 0 ? segmentsA_ : segmentsB_; }
};

} // namespace Isoch
} // namespace FWA