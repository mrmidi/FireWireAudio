#pragma once

#include <memory>
#include <expected>
#include <mutex>
#include <atomic>
#include <CoreFoundation/CoreFoundation.h>
#include <spdlog/logger.h>
#include "FWA/Error.h"
#include "Isoch/core/ReceiverTypes.hpp"

namespace FWA {
namespace Isoch {

/**
 * @brief Manager for monitoring data flow in isochronous transport
 * 
 * This class handles monitoring for no-data conditions and timeouts
 * in isochronous transport.
 */
class IsochMonitoringManager {
public:
    /**
     * @brief Construct a new IsochMonitoringManager
     * 
     * @param logger Logger for diagnostic information
     * @param runLoop RunLoop for timer callbacks, or nullptr for current
     */
    IsochMonitoringManager(
        std::shared_ptr<spdlog::logger> logger,
        CFRunLoopRef runLoop = nullptr);
    
    /**
     * @brief Destructor - ensures proper cleanup of timers
     */
    ~IsochMonitoringManager();
    
    // Prevent copying
    IsochMonitoringManager(const IsochMonitoringManager&) = delete;
    IsochMonitoringManager& operator=(const IsochMonitoringManager&) = delete;
    
    /**
     * @brief Set callback for no-data condition
     * 
     * @param callback Function to call when no data is received
     * @param refCon Context pointer to pass to the callback
     */
    void setNoDataCallback(NoDataCallback callback, void* refCon) {
        noDataCallback_ = callback;
        noDataCallbackRefCon_ = refCon;
    }
    
    /**
     * @brief Start monitoring for no-data condition
     * 
     * @param timeoutMs Timeout in milliseconds
     * @return std::expected<void, IOKitError> Success or error code
     */
    std::expected<void, IOKitError> startMonitoring(uint32_t timeoutMs);
    
    /**
     * @brief Stop monitoring
     */
    void stopMonitoring();
    
    /**
     * @brief Reset timer on data reception
     */
    void resetTimer();
    
    /**
     * @brief Set CIP-only mode
     * 
     * In CIP-only mode, packets with only CIP headers (no payload)
     * are considered as "no data".
     * 
     * @param enable True to enable CIP-only mode, false otherwise
     */
    void setCIPOnlyMode(bool enable) {
        cipOnlyMode_ = enable;
    }
    
    /**
     * @brief Update the last received cycle
     * 
     * @param cycle Cycle number
     */
    void updateLastCycle(uint32_t cycle) {
        lastCycle_ = cycle;
    }
    
    /**
     * @brief Set the RunLoop for timer callbacks
     * 
     * @param runLoop The RunLoop to use
     */
    void setRunLoop(CFRunLoopRef runLoop) {
        runLoop_ = runLoop;
    }
    
private:
    /**
     * @brief Helper method to stop and release timer without locking
     * 
     * This method does the actual work of stopping the timer without
     * acquiring the mutex, to prevent potential deadlocks.
     */
    void internalStopAndReleaseTimer();

    /**
     * @brief Handle timer expiration
     */
    void handleTimeout();
    
    /**
     * @brief Static callback for CFRunLoopTimer
     */
    static void timerCallback(CFRunLoopTimerRef timer, void* info);
    
    std::shared_ptr<spdlog::logger> logger_;
    CFRunLoopRef runLoop_{nullptr};
    CFRunLoopTimerRef timer_{nullptr};
    NoDataCallback noDataCallback_{nullptr};
    void* noDataCallbackRefCon_{nullptr};
    uint32_t timeoutMs_{1000}; // Default timeout: 1 second
    std::atomic<uint32_t> lastCycle_{0};
    bool cipOnlyMode_{true};
    std::mutex timerMutex_;
};

} // namespace Isoch
} // namespace FWA