#pragma once

#include "Isoch/core/IsochDoubleBufferManager.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace spdlog {
    class logger;
}

namespace FWA {
namespace Isoch {

// Define a distinct packet callback type to avoid collision with the one in ReceiverTypes.hpp
using ExtendedPacketCallback = void (*)(uint32_t segmentIndex, const uint8_t* data, size_t size, uint32_t timestamp, void* refCon);

/**
 * @class AudioProcessingThread
 * @brief Handles audio processing in a separate thread from FireWire callbacks.
 * 
 * This class runs a separate thread that processes audio data from the double buffer,
 * preventing deadlocks in the audio processing chain by decoupling it from the
 * FireWire interrupt context.
 */
class AudioProcessingThread {
public:
    /**
     * @brief Constructor
     * @param bufferManager Shared double buffer manager instance
     * @param logger Logger instance for debugging
     */
    AudioProcessingThread(std::shared_ptr<IsochDoubleBufferManager> bufferManager,
                         std::shared_ptr<spdlog::logger> logger);
    
    /**
     * @brief Destructor
     */
    ~AudioProcessingThread();
    
    /**
     * @brief Start the processing thread
     * @return True if thread started successfully, false otherwise
     */
    bool start();
    
    /**
     * @brief Stop the processing thread
     */
    void stop();
    
    /**
     * @brief Set the audio callback function
     * @param callback Function pointer to call with processed audio data
     * @param refCon User-provided reference constant passed to callback
     */
    void setAudioCallback(ExtendedPacketCallback callback, void* refCon);
    
    /**
     * @brief Check if processing thread is active
     * @return True if thread is running, false otherwise
     */
    bool isRunning() const { return running_.load(); }
    
    /**
     * @brief Signal that new data is available for processing
     * 
     * This method should be called from the IsochManager after completing a segment
     * to notify the processing thread that there's new data to process.
     */
    void notifyNewData();
    
    /**
     * @brief Set the timestamp for a segment
     * @param segment Segment index
     * @param timestamp FireWire timestamp value
     */
    void setSegmentTimestamp(uint32_t segment, uint32_t timestamp);
    
private:
    // Main processing thread function
    void processingLoop();
    
    // Process a single segment of audio data
    void processSegment(uint32_t segment);
    
    std::shared_ptr<IsochDoubleBufferManager> bufferManager_;
    std::shared_ptr<spdlog::logger> logger_;
    std::thread processingThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> shouldExit_{false};
    
    // Condition variable for signaling new data
    std::mutex condMutex_;
    std::condition_variable dataCond_;
    bool dataAvailable_{false};
    
    // Client callback
    ExtendedPacketCallback audioCallback_{nullptr};
    void* audioCallbackRefCon_{nullptr};
    
    // Timestamps for each segment
    std::vector<uint32_t> timestamps_;
    std::mutex timestampMutex_;
};

} // namespace Isoch
} // namespace FWA