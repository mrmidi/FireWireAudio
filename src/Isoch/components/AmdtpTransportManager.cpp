#include "Isoch/components/AmdtpTransportManager.hpp"
#include <unistd.h>
#include "Isoch/utils/RunLoopHelper.hpp"

namespace FWA {
namespace Isoch {

AmdtpTransportManagerImpl::AmdtpTransportManagerImpl(std::shared_ptr<spdlog::logger> logger)
: AmdtpTransportManager(std::move(logger)) {
    if (logger_) {
        logger_->debug("[AmdtpTransportManagerImpl] created");
    }
}

// TODO: fixupDCLJumpTargets();
std::expected<void, IOKitError> AmdtpTransportManagerImpl::start(IOFireWireLibIsochChannelRef channel) {
    
    // Log callback execution using RunLoopHelper
    logCallbackThreadInfo("AmdtpTransportManager", "start", this);
    
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (state_ != State::Stopped) {
        logger_->error("Cannot start transport - invalid state: {}",
                       static_cast<int>(state_.load()));
        return std::unexpected(IOKitError::Busy);
    }
    
    state_ = State::Starting;
    finalizeCallbackCalled_ = false;
    
    
    if (auto result = prepareStart(); !result) {
        state_ = State::Stopped;
        return result;
    }
    
    IOReturn result = (*channel)->AllocateChannel(channel);
    if (result != kIOReturnSuccess) {
        state_ = State::Stopped;
        logger_->error("[AmdtpTransportManagerImpl::start] Failed to allocate channel: {}", result);
        return std::unexpected(static_cast<IOKitError>(result));
    }
        
    logger_->info("[AmdtpTransportManagerImpl::start] Channel allocated successfully");
    
   
    
    result = (*channel)->Start(channel);
    if (result != kIOReturnSuccess) {
        (*channel)->ReleaseChannel(channel);
        state_ = State::Stopped;
        logger_->error("Failed to start channel: {}", result);
        return std::unexpected(static_cast<IOKitError>(result));
    }
    
    state_ = State::Running;
    logger_->info("[AmdtpTransportManagerImpl::start] Transport started successfully");
    return {};
}

std::expected<void, IOKitError> AmdtpTransportManagerImpl::stop(
                                                                IOFireWireLibIsochChannelRef channel) {
    
    // Log callback execution using RunLoopHelper
    logCallbackThreadInfo("AmdtpTransportManager", "stop", this);
    
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (state_ != State::Running) {
        logger_->error("Cannot stop transport - invalid state: {}",
                       static_cast<int>(state_.load()));
        return std::unexpected(IOKitError::Busy);
    }
    
    state_ = State::Stopping;
    
    IOReturn result = (*channel)->Stop(channel);
    if (result != kIOReturnSuccess) {
        logger_->error("Failed to stop channel: {}", result);
        return std::unexpected(static_cast<IOKitError>(result));
    }
    
    (*channel)->ReleaseChannel(channel);
    
    if (auto result = finishStop(); !result) {
        return result;
    }
    
    while (!finalizeCallbackCalled_) {
        usleep(1000);
    }
    
    state_ = State::Stopped;
    logger_->info("Transport stopped successfully");
    return {};
}

AmdtpTransportManager::State AmdtpTransportManagerImpl::getState() const noexcept {
    return state_.load(std::memory_order_acquire);
}

void AmdtpTransportManagerImpl::handleFinalize() {
    // Log callback execution using RunLoopHelper
    logCallbackThreadInfo("AmdtpTransportManager", "handleFinalize", this);
    
    finalizeCallbackCalled_ = true;
    
    if (finalizeCallback_) {
        finalizeCallback_();
    }
    
    logger_->debug("Transport finalize callback completed");
}

std::expected<void, IOKitError> AmdtpTransportManagerImpl::prepareStart() {
    // Log callback execution using RunLoopHelper
    logCallbackThreadInfo("AmdtpTransportManager", "prepareStart", this);
    
    // Implementation specific to your needs
    return {};
}

std::expected<void, IOKitError> AmdtpTransportManagerImpl::finishStop() {
    // Log callback execution using RunLoopHelper
    logCallbackThreadInfo("AmdtpTransportManager", "finishStop", this);
    
    // Implementation specific to your needs
    return {};
}

} // namespace Isoch
} // namespace FWA
