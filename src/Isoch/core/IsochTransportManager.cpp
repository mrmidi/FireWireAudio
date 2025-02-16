#include "Isoch/core/IsochTransportManager.hpp"
#include <unistd.h>
#include <spdlog/spdlog.h>

namespace FWA {
namespace Isoch {

IsochTransportManager::IsochTransportManager(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {
    if (logger_) {
        logger_->debug("IsochTransportManager created");
    }
}

IsochTransportManager::~IsochTransportManager() {
    if (state_ != State::Stopped) {
        if (logger_) {
            logger_->warn("IsochTransportManager destroyed while not stopped");
        }
    }
}

std::expected<void, IOKitError> IsochTransportManager::start(IOFireWireLibIsochChannelRef channel) {
    // Acquire lock for thread safety
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (state_ != State::Stopped) {
        if (logger_) {
            logger_->error("IsochTransportManager::start: Invalid state: {}",
                         static_cast<int>(state_.load()));
        }
        return std::unexpected(IOKitError::Busy);
    }
    
    // Change state to starting
    state_ = State::Starting;
    finalizeCallbackCalled_ = false;
    
    // Prepare for start
    auto result = prepareStart();
    if (!result) {
        state_ = State::Stopped;
        return result;
    }
    
    // Allocate channel
    IOReturn ret = (*channel)->AllocateChannel(channel);
    if (ret != kIOReturnSuccess) {
        state_ = State::Stopped;
        if (logger_) {
            logger_->error("IsochTransportManager::start: Failed to allocate channel: 0x{:08X}", ret);
        }
        return std::unexpected(IOKitError(ret));
    }
    
    if (logger_) {
        logger_->info("IsochTransportManager::start: Channel allocated successfully");
    }
    
    // Start the channel
    ret = (*channel)->Start(channel);
    if (ret != kIOReturnSuccess) {
        // Clean up allocated channel
        (*channel)->ReleaseChannel(channel);
        state_ = State::Stopped;
        if (logger_) {
            logger_->error("IsochTransportManager::start: Failed to start channel: 0x{:08X}", ret);
        }
        return std::unexpected(IOKitError(ret));
    }
    
    // Change state to running
    state_ = State::Running;
    
    if (logger_) {
        logger_->info("IsochTransportManager::start: Transport started successfully");
    }
    
    return {};
}

std::expected<void, IOKitError> IsochTransportManager::stop(IOFireWireLibIsochChannelRef channel) {
    // Acquire lock for thread safety
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (state_ != State::Running) {
        if (logger_) {
            logger_->error("IsochTransportManager::stop: Invalid state: {}",
                         static_cast<int>(state_.load()));
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    // Change state to stopping
    state_ = State::Stopping;
    
    // Stop the channel
    IOReturn ret = (*channel)->Stop(channel);
    if (ret != kIOReturnSuccess) {
        if (logger_) {
            logger_->error("IsochTransportManager::stop: Failed to stop channel: 0x{:08X}", ret);
        }
        // Don't return error, continue with cleanup
    }
    
    // Release the channel
    (*channel)->ReleaseChannel(channel);
    
    // Finish stop
    auto result = finishStop();
    if (!result) {
        return result;
    }
    
    // Wait for finalize callback if it hasn't been called yet
    int waitCount = 0;
    const int maxWaitCycles = 100; // Maximum wait time = 100ms
    
    while (!finalizeCallbackCalled_ && waitCount < maxWaitCycles) {
        usleep(1000); // 1ms sleep
        waitCount++;
    }
    
    if (!finalizeCallbackCalled_ && waitCount >= maxWaitCycles) {
        if (logger_) {
            logger_->warn("IsochTransportManager::stop: Finalize callback not called after {}ms", waitCount);
        }
    }
    
    // Change state to stopped regardless of finalize callback
    state_ = State::Stopped;
    
    if (logger_) {
        logger_->info("IsochTransportManager::stop: Transport stopped successfully");
    }
    
    return {};
}

void IsochTransportManager::handleFinalize() {
    if (logger_) {
        logger_->debug("IsochTransportManager::handleFinalize called");
    }
    
    // Set flag to indicate finalize callback was called
    finalizeCallbackCalled_ = true;
    
    // Call user-provided finalize callback with refcon
    if (finalizeCallback_) {
        finalizeCallback_(finalizeRefCon_);
    }
}

std::expected<void, IOKitError> IsochTransportManager::prepareStart() {
    // This can be extended with implementation-specific operations
    // before starting the transport
    
    if (logger_) {
        logger_->debug("IsochTransportManager::prepareStart: Preparing transport start");
    }
    
    return {};
}

std::expected<void, IOKitError> IsochTransportManager::finishStop() {
    // This can be extended with implementation-specific operations
    // after stopping the transport
    
    if (logger_) {
        logger_->debug("IsochTransportManager::finishStop: Finishing transport stop");
    }
    
    return {};
}

} // namespace Isoch
} // namespace FWA