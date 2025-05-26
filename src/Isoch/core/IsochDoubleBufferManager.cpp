// DEPRECATED
// #include "Isoch/core/IsochDoubleBufferManager.hpp"
// #include <spdlog/spdlog.h>
// #include <algorithm>
// #include <cassert>
// #include <cstring>

// namespace FWA {
// namespace Isoch {

// IsochDoubleBufferManager::IsochDoubleBufferManager(
//     std::shared_ptr<spdlog::logger> logger,
//     uint32_t numSegments,
//     uint32_t cyclesPerSegment,
//     uint32_t cycleBufferSize)
//     : logger_(std::move(logger))
//     , numSegments_(numSegments)
//     , cyclesPerSegment_(cyclesPerSegment)
//     , cycleBufferSize_(cycleBufferSize)
//     , segmentSize_(cyclesPerSegment * cycleBufferSize)
//     , mainBuffer_(nullptr)
//     , totalBufferSize_(0) {
    
//     if (logger_) {
//         logger_->debug("IsochDoubleBufferManager created with {} segments of {} bytes each",
//                      numSegments_, segmentSize_);
//     }
    
//     // Initialize pointers to nullptr
//     segmentCompleteA_ = nullptr;
//     segmentCompleteB_ = nullptr;
//     segmentProcessedA_ = nullptr;
//     segmentProcessedB_ = nullptr;
//     segmentsA_ = nullptr;
//     segmentsB_ = nullptr;
// }

// IsochDoubleBufferManager::~IsochDoubleBufferManager() {
//     // Free allocated memory
//     delete[] segmentCompleteA_;
//     delete[] segmentCompleteB_;
//     delete[] segmentProcessedA_;
//     delete[] segmentProcessedB_;
//     delete[] segmentsA_;
//     delete[] segmentsB_;
//     delete[] mainBuffer_;
    
//     if (logger_) {
//         logger_->debug("IsochDoubleBufferManager destroyed");
//     }
// }

// bool IsochDoubleBufferManager::initialize(uint8_t* baseBuffer, size_t totalSize) {
//     // Check parameters
//     if (!baseBuffer || totalSize == 0) {
//         if (logger_) {
//             logger_->error("IsochDoubleBufferManager::initialize: Invalid buffer parameters");
//         }
//         return false;
//     }
    
//     // Store provided buffer
//     mainBuffer_ = baseBuffer;
//     totalBufferSize_ = totalSize;
    
//     // Calculate required buffer size
//     const size_t dataSize = 2 * numSegments_ * segmentSize_; // Two complete data buffers
    
//     if (totalSize < dataSize) {
//         if (logger_) {
//             logger_->error("IsochDoubleBufferManager::initialize: Insufficient buffer size. Need at least {} bytes for data, got {} bytes",
//                          dataSize, totalSize);
//         }
//         return false;
//     }
    
//     // Allocate control arrays
//     try {
//         segmentCompleteA_ = new std::atomic<bool>[numSegments_];
//         segmentCompleteB_ = new std::atomic<bool>[numSegments_];
//         segmentProcessedA_ = new std::atomic<bool>[numSegments_];
//         segmentProcessedB_ = new std::atomic<bool>[numSegments_];
//         segmentsA_ = new BufferSegment[numSegments_];
//         segmentsB_ = new BufferSegment[numSegments_];
//     } catch (const std::bad_alloc&) {
//         if (logger_) {
//             logger_->error("IsochDoubleBufferManager::initialize: Failed to allocate control arrays");
//         }
        
//         // Clean up any arrays that were successfully allocated
//         delete[] segmentCompleteA_;
//         delete[] segmentCompleteB_;
//         delete[] segmentProcessedA_;
//         delete[] segmentProcessedB_;
//         delete[] segmentsA_;
//         delete[] segmentsB_;
        
//         // Reset pointers
//         segmentCompleteA_ = nullptr;
//         segmentCompleteB_ = nullptr;
//         segmentProcessedA_ = nullptr;
//         segmentProcessedB_ = nullptr;
//         segmentsA_ = nullptr;
//         segmentsB_ = nullptr;
        
//         return false;
//     }
    
//     // Initialize atomic flags
//     for (uint32_t i = 0; i < numSegments_; ++i) {
//         segmentCompleteA_[i].store(false);
//         segmentCompleteB_[i].store(false);
//         segmentProcessedA_[i].store(true);  // Initially processed (ready to write)
//         segmentProcessedB_[i].store(true);  // Initially processed (ready to write)
//     }
    
//     // Set up segment pointers for buffer A
//     uint8_t* currentPtr = mainBuffer_;
//     for (uint32_t i = 0; i < numSegments_; ++i) {
//         segmentsA_[i].data = currentPtr;
//         segmentsA_[i].size = segmentSize_;
//         currentPtr += segmentSize_;
//     }
    
//     // Set up segment pointers for buffer B
//     for (uint32_t i = 0; i < numSegments_; ++i) {
//         segmentsB_[i].data = currentPtr;
//         segmentsB_[i].size = segmentSize_;
//         currentPtr += segmentSize_;
//     }
    
//     // Start with buffer A for writing, buffer B for reading
//     writeBufferIndex_.store(0);  // A
//     readBufferIndex_.store(1);   // B
//     bufferAReady_.store(false);
//     bufferBReady_.store(false);
    
//     if (logger_) {
//         logger_->info("IsochDoubleBufferManager::initialize: Initialized with {} bytes of memory at {:p}",
//                     totalSize, (void*)baseBuffer);
//     }
    
//     return true;
// }

// uint8_t* IsochDoubleBufferManager::getWriteSegmentPtr(uint32_t segment) {
//     if (segment >= numSegments_ || !mainBuffer_) {
//         if (logger_) {
//             logger_->warn("IsochDoubleBufferManager::getWriteSegmentPtr: Invalid segment {} or uninitialized buffer",
//                         segment);
//         }
//         return nullptr;
//     }
    
//     // Check if segment is ready for writing
//     if (!getWriteProcessedFlags()[segment].load()) {
//         if (logger_) {
//             logger_->warn("IsochDoubleBufferManager::getWriteSegmentPtr: Segment {} not processed yet", segment);
//         }
//         return nullptr;
//     }
    
//     // Return pointer to segment data
//     return getWriteSegments()[segment].data;
// }

// uint8_t* IsochDoubleBufferManager::getReadSegmentPtr(uint32_t segment) {
//     if (segment >= numSegments_ || !mainBuffer_) {
//         if (logger_) {
//             logger_->warn("IsochDoubleBufferManager::getReadSegmentPtr: Invalid segment {} or uninitialized buffer",
//                         segment);
//         }
//         return nullptr;
//     }
    
//     // Check if segment is ready for reading (completed)
//     if (!getReadCompleteFlags()[segment].load()) {
//         if (logger_) {
//             logger_->warn("IsochDoubleBufferManager::getReadSegmentPtr: Segment {} not complete yet", segment);
//         }
//         return nullptr;
//     }
    
//     // Return pointer to segment data
//     return getReadSegments()[segment].data;
// }

// void IsochDoubleBufferManager::markSegmentComplete(uint32_t segment) {
//     if (segment >= numSegments_) {
//         if (logger_) {
//             logger_->warn("IsochDoubleBufferManager::markSegmentComplete: Invalid segment {}", segment);
//         }
//         return;
//     }
    
//     // Mark segment as complete and not processed
//     getWriteCompleteFlags()[segment].store(true);
//     getWriteProcessedFlags()[segment].store(false);
    
//     // Check if this completes the buffer
//     if (isWriteBufferFull()) {
//         if (writeBufferIndex_.load() == 0) {
//             bufferAReady_.store(true);
//         } else {
//             bufferBReady_.store(true);
//         }
//     }
    
//     if (logger_) {
//         logger_->debug("IsochDoubleBufferManager: Marked write buffer segment {} as complete", segment);
//     }
// }

// void IsochDoubleBufferManager::markSegmentProcessed(uint32_t segment) {
//     if (segment >= numSegments_) {
//         if (logger_) {
//             logger_->warn("IsochDoubleBufferManager::markSegmentProcessed: Invalid segment {}", segment);
//         }
//         return;
//     }
    
//     // Mark segment as processed and not complete
//     getReadProcessedFlags()[segment].store(true);
//     getReadCompleteFlags()[segment].store(false);
    
//     if (logger_) {
//         logger_->debug("IsochDoubleBufferManager: Marked read buffer segment {} as processed", segment);
//     }
// }

// bool IsochDoubleBufferManager::isSegmentComplete(uint32_t segment) const {
//     if (segment >= numSegments_) {
//         return false;
//     }
//     return getWriteCompleteFlags()[segment].load();
// }

// bool IsochDoubleBufferManager::isSegmentProcessed(uint32_t segment) const {
//     if (segment >= numSegments_) {
//         return false;
//     }
//     return getReadProcessedFlags()[segment].load();
// }

// bool IsochDoubleBufferManager::isWriteBufferFull() const {
//     // Check if all segments in write buffer are complete
//     const std::atomic<bool>* completeFlags = getWriteCompleteFlags();
//     for (uint32_t i = 0; i < numSegments_; ++i) {
//         if (!completeFlags[i].load()) {
//             return false;
//         }
//     }
//     return true;
// }

// bool IsochDoubleBufferManager::isReadBufferEmpty() const {
//     // Check if all segments in read buffer are processed
//     const std::atomic<bool>* processedFlags = getReadProcessedFlags();
//     for (uint32_t i = 0; i < numSegments_; ++i) {
//         if (!processedFlags[i].load()) {
//             return false;
//         }
//     }
//     return true;
// }

// bool IsochDoubleBufferManager::trySwapBuffers() {
//     // Check if buffers are in a state that allows swapping
//     if (!isWriteBufferFull() || !isReadBufferEmpty()) {
//         return false;
//     }
    
//     // Perform the swap
//     uint32_t oldWrite = writeBufferIndex_.load();
//     uint32_t oldRead = readBufferIndex_.load();
    
//     writeBufferIndex_.store(oldRead);
//     readBufferIndex_.store(oldWrite);
    
//     // Update buffer ready flags
//     if (oldWrite == 0) {
//         bufferAReady_.store(false);  // No longer ready for reading
//     } else {
//         bufferBReady_.store(false);  // No longer ready for reading
//     }
    
//     if (logger_) {
//         logger_->debug("IsochDoubleBufferManager: Swapped buffers - write buffer is now {}, read buffer is now {}",
//                      writeBufferIndex_.load(), readBufferIndex_.load());
//     }
    
//     return true;
// }

// } // namespace Isoch
// } // namespace FWA