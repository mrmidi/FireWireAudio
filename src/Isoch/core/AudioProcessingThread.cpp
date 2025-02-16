#include "Isoch/core/AudioProcessingThread.hpp"
#include <spdlog/spdlog.h>

namespace FWA {
namespace Isoch {

AudioProcessingThread::AudioProcessingThread(
    std::shared_ptr<IsochDoubleBufferManager> bufferManager,
    std::shared_ptr<spdlog::logger> logger)
    : bufferManager_(std::move(bufferManager))
    , logger_(std::move(logger))
    , running_(false)
    , shouldExit_(false)
    , dataAvailable_(false)
    , audioCallback_(nullptr)
    , audioCallbackRefCon_(nullptr) {
    
    if (!bufferManager_) {
        if (logger_) {
            logger_->error("AudioProcessingThread: Buffer manager is null");
        }
        return;
    }
    
    // Initialize timestamp vector
    if (bufferManager_->getNumSegments() > 0) {
        timestamps_.resize(bufferManager_->getNumSegments(), 0);
    }
    
    if (logger_) {
        logger_->debug("AudioProcessingThread created with {} segments",
                     bufferManager_->getNumSegments());
    }
}

AudioProcessingThread::~AudioProcessingThread() {
    // Make sure thread is stopped
    stop();
    
    if (logger_) {
        logger_->debug("AudioProcessingThread destroyed");
    }
}

bool AudioProcessingThread::start() {
    // Don't start if already running
    if (running_.load()) {
        if (logger_) {
            logger_->warn("AudioProcessingThread::start: Already running");
        }
        return false;
    }
    
    // Reset exit flag
    shouldExit_.store(false);
    
    // Start the thread
    try {
        processingThread_ = std::thread(&AudioProcessingThread::processingLoop, this);
        running_.store(true);
        
        if (logger_) {
            logger_->info("AudioProcessingThread started");
        }
        
        return true;
    } catch (const std::exception& e) {
        if (logger_) {
            logger_->error("AudioProcessingThread::start: Failed to start thread: {}", e.what());
        }
        return false;
    }
}

void AudioProcessingThread::stop() {
    // Skip if not running
    if (!running_.load()) {
        return;
    }
    
    // Signal thread to exit and notify condition variable
    shouldExit_.store(true);
    
    {
        std::lock_guard<std::mutex> lock(condMutex_);
        dataAvailable_ = true;
    }
    
    dataCond_.notify_one();
    
    // Wait for thread to exit
    if (processingThread_.joinable()) {
        processingThread_.join();
    }
    
    // Reset running flag
    running_.store(false);
    
    if (logger_) {
        logger_->info("AudioProcessingThread stopped");
    }
}

void AudioProcessingThread::setAudioCallback(ExtendedPacketCallback callback, void* refCon) {
    audioCallback_ = callback;
    audioCallbackRefCon_ = refCon;
    
    if (logger_) {
        logger_->debug("AudioProcessingThread: Set audio callback to {:p} with refCon {:p}",
                     reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(callback)),
                     refCon);
    }
}

void AudioProcessingThread::notifyNewData() {
    // Signal that new data is available
    {
        std::lock_guard<std::mutex> lock(condMutex_);
        dataAvailable_ = true;
    }
    
    // Notify the processing thread
    dataCond_.notify_one();
    
    if (logger_) {
        logger_->debug("AudioProcessingThread: Notified of new data");
    }
}

void AudioProcessingThread::setSegmentTimestamp(uint32_t segment, uint32_t timestamp) {
    // Check segment bounds
    if (segment >= timestamps_.size()) {
        if (logger_) {
            logger_->warn("AudioProcessingThread::setSegmentTimestamp: Invalid segment index {}", segment);
        }
        return;
    }
    
    // Update timestamp
    {
        std::lock_guard<std::mutex> lock(timestampMutex_);
        timestamps_[segment] = timestamp;
    }
}

void AudioProcessingThread::processingLoop() {
    if (logger_) {
        logger_->debug("AudioProcessingThread: Processing loop started");
    }
    
    // Main loop
    while (!shouldExit_.load()) {
        // Wait for new data signal
        {
            std::unique_lock<std::mutex> lock(condMutex_);
            dataCond_.wait(lock, [this] {
                return dataAvailable_ || shouldExit_.load();
            });
            
            // Reset flag
            dataAvailable_ = false;
        }
        
        // Check if we should exit
        if (shouldExit_.load()) {
            break;
        }
        
        if (!bufferManager_) {
            if (logger_) {
                logger_->error("AudioProcessingThread: Buffer manager is null");
            }
            continue;
        }
        
        // Try to swap buffers if write buffer is full and read buffer is empty
        if (bufferManager_->isWriteBufferFull() && bufferManager_->isReadBufferEmpty()) {
            if (bufferManager_->trySwapBuffers()) {
                if (logger_) {
                    logger_->debug("AudioProcessingThread: Successfully swapped buffers");
                }
                
                // Process all segments in the read buffer
                uint32_t numSegments = bufferManager_->getNumSegments();
                for (uint32_t i = 0; i < numSegments; ++i) {
                    processSegment(i);
                }
            } else {
                if (logger_) {
                    logger_->warn("AudioProcessingThread: Failed to swap buffers");
                }
            }
        }
    }
    
    if (logger_) {
        logger_->debug("AudioProcessingThread: Processing loop exited");
    }
}

void AudioProcessingThread::processSegment(uint32_t segment) {
    // Get segment data
    uint8_t* data = bufferManager_->getReadSegmentPtr(segment);
    
    if (!data) {
        if (logger_) {
            logger_->warn("AudioProcessingThread::processSegment: Failed to get read segment {} data", segment);
        }
        return;
    }
    
    // Get segment size
    size_t size = bufferManager_->getSegmentSize();
    
    // Get timestamp for this segment
    uint32_t timestamp;
    {
        std::lock_guard<std::mutex> lock(timestampMutex_);
        timestamp = segment < timestamps_.size() ? timestamps_[segment] : 0;
    }
    
    // Send data to client via callback
    if (audioCallback_) {
        try {
            audioCallback_(segment, data, size, timestamp, audioCallbackRefCon_);
        } catch (const std::exception& e) {
            if (logger_) {
                logger_->error("AudioProcessingThread::processSegment: Exception in callback: {}", e.what());
            }
        }
    }
    
    // Mark segment as processed
    bufferManager_->markSegmentProcessed(segment);
    
    if (logger_) {
        logger_->debug("AudioProcessingThread: Processed segment {} with timestamp {}", segment, timestamp);
    }
}

} // namespace Isoch
} // namespace FWA