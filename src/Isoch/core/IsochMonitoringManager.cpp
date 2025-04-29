#include "Isoch/core/IsochMonitoringManager.hpp"
#include <spdlog/spdlog.h>

namespace FWA {
namespace Isoch {

IsochMonitoringManager::IsochMonitoringManager(
    std::shared_ptr<spdlog::logger> logger,
    CFRunLoopRef runLoop)
    : logger_(std::move(logger))
    , runLoop_(runLoop ? runLoop : CFRunLoopGetCurrent()) {
    
    if (logger_) {
        logger_->debug("IsochMonitoringManager created with RunLoop={:p}", (void*)runLoop_);
    }
}

IsochMonitoringManager::~IsochMonitoringManager() {
    stopMonitoring();
    
    if (logger_) {
        logger_->debug("IsochMonitoringManager destroyed");
    }
}

void IsochMonitoringManager::internalStopAndReleaseTimer() {
    if (timer_) {
        // if (logger_) {
        //     logger_->debug("IsochMonitoringManager::internalStopAndReleaseTimer: Stopping monitoring");
        // }
        CFRunLoopTimerInvalidate(timer_);
        CFRelease(timer_);
        timer_ = nullptr;
    }
}

std::expected<void, IOKitError> IsochMonitoringManager::startMonitoring(uint32_t timeoutMs) {
    // Acquire lock for thread safety
    std::lock_guard<std::mutex> lock(timerMutex_);
    
    // Stop and release existing timer *before* creating new one
    internalStopAndReleaseTimer(); // Call the non-locking helper
    
    // Store timeout value
    timeoutMs_ = timeoutMs;
    
    // if (logger_) {
    //     logger_->debug("IsochMonitoringManager::startMonitoring: Starting with timeout={}ms", timeoutMs_);
    // }
    
    // Create timer context
    CFRunLoopTimerContext context = {
        0, this, nullptr, nullptr, nullptr
    };
    
    // Calculate the absolute time for the timer
    CFAbsoluteTime fireTime = CFAbsoluteTimeGetCurrent() + (timeoutMs_ / 1000.0);
    
    // Create the timer
    timer_ = CFRunLoopTimerCreate(
        kCFAllocatorDefault,
        fireTime,
        0, // interval (0 = one-shot timer)
        0, 0, // flags, order
        timerCallback,
        &context);
    
    if (!timer_) {
        if (logger_) {
            logger_->error("IsochMonitoringManager::startMonitoring: Failed to create timer");
        }
        return std::unexpected(IOKitError::NoMemory);
    }
    
    // Add the timer to the run loop
    CFRunLoopAddTimer(runLoop_, timer_, kCFRunLoopDefaultMode);
    
    // if (logger_) {
    //     logger_->debug("IsochMonitoringManager::startMonitoring: Monitoring started");
    // }
    
    return {};
}

void IsochMonitoringManager::stopMonitoring() {
    // Acquire lock for thread safety
    std::lock_guard<std::mutex> lock(timerMutex_);
    internalStopAndReleaseTimer(); // Call the non-locking helper
}

void IsochMonitoringManager::resetTimer() {
    if (timeoutMs_ == 0) {
        return; // No monitoring
    }
    
    // No lock needed here, startMonitoring handles locking
    startMonitoring(timeoutMs_);
}

void IsochMonitoringManager::handleTimeout() {
    if (logger_) {
        logger_->warn("IsochMonitoringManager::handleTimeout: No data received before timeout");
    }
    
    // Call the no-data callback with the last cycle and refcon
    if (noDataCallback_) {
        noDataCallback_(lastCycle_, noDataCallbackRefCon_);
    }
    
    // Reset the timer to continue monitoring
    resetTimer();
}

void IsochMonitoringManager::timerCallback(CFRunLoopTimerRef timer, void* info) {
    // Get the IsochMonitoringManager instance from the info pointer
    auto manager = static_cast<IsochMonitoringManager*>(info);
    if (manager) {
        manager->handleTimeout();
    }
}

} // namespace Isoch
} // namespace FWA