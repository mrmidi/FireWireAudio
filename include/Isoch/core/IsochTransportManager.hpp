#pragma once

#include <memory>
#include <expected>
#include <atomic>
#include <mutex>
#include <functional>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/firewire/IOFireWireLibIsoch.h>
#include <spdlog/logger.h>
#include "FWA/Error.h"

namespace FWA {
namespace Isoch {

/**
 * @brief Callback function for transport finalization
 */
using FinalizeCallback = void(*)(void* refCon);

/**
 * @brief Manager for FireWire isochronous transport operations
 * 
 * This class handles the lifecycle of isochronous transport, including
 * starting and stopping transmission, and managing transport state.
 */
class IsochTransportManager {
public:
    /**
     * @brief Transport state enumeration
     */
    enum class State {
        Stopped,    ///< Transport is stopped
        Starting,   ///< Transport is in the process of starting
        Running,    ///< Transport is running
        Stopping    ///< Transport is in the process of stopping
    };
    
    /**
     * @brief Construct a new IsochTransportManager
     * 
     * @param logger Logger for diagnostic information
     */
    explicit IsochTransportManager(std::shared_ptr<spdlog::logger> logger);
    
    /**
     * @brief Destructor
     */
    ~IsochTransportManager();
    
    // Prevent copying
    IsochTransportManager(const IsochTransportManager&) = delete;
    IsochTransportManager& operator=(const IsochTransportManager&) = delete;
    
    /**
     * @brief Start isochronous transport
     * 
     * @param channel FireWire isochronous channel
     * @return std::expected<void, IOKitError> Success or error code
     */
    std::expected<void, IOKitError> start(IOFireWireLibIsochChannelRef channel);
    
    /**
     * @brief Stop isochronous transport
     * 
     * @param channel FireWire isochronous channel
     * @return std::expected<void, IOKitError> Success or error code
     */
    std::expected<void, IOKitError> stop(IOFireWireLibIsochChannelRef channel);
    
    /**
     * @brief Get current transport state
     * 
     * @return State Current state
     */
    State getState() const noexcept { return state_; }
    
    /**
     * @brief Set finalize callback
     * 
     * @param callback Function to call when transport is finalized
     * @param refCon Context pointer to pass to the callback
     */
    void setFinalizeCallback(FinalizeCallback callback, void* refCon) {
        finalizeCallback_ = callback;
        finalizeRefCon_ = refCon;
    }
    
    /**
     * @brief Handle transport finalization
     * 
     * Called when the transport is finalized by FireWire
     */
    void handleFinalize();
    
private:
    /**
     * @brief Prepare for transport start
     * 
     * @return std::expected<void, IOKitError> Success or error code
     */
    std::expected<void, IOKitError> prepareStart();
    
    /**
     * @brief Finish transport stop
     * 
     * @return std::expected<void, IOKitError> Success or error code
     */
    std::expected<void, IOKitError> finishStop();
    
    std::shared_ptr<spdlog::logger> logger_;
    std::atomic<State> state_{State::Stopped};
    std::mutex stateMutex_;
    FinalizeCallback finalizeCallback_{nullptr};
    void* finalizeRefCon_{nullptr};
    std::atomic<bool> finalizeCallbackCalled_{false};
};

} // namespace Isoch
} // namespace FWA