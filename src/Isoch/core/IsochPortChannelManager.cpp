#include "Isoch/core/IsochPortChannelManager.hpp"
#include <spdlog/spdlog.h>
#include <unistd.h>
#include <os/log.h>
#include "Isoch/utils/AmdtpHelpers.hpp"

namespace FWA {
namespace Isoch {

// Static constant definition
constexpr uint32_t IsochPortChannelManager::kAnyAvailableIsochChannel;

IsochPortChannelManager::IsochPortChannelManager(
    std::shared_ptr<spdlog::logger> logger,
    IOFireWireLibNubRef interface,
    CFRunLoopRef runLoop,
    bool isTalker)
    : logger_(std::move(logger))
    , interface_(interface)
    , runLoop_(runLoop ? runLoop : CFRunLoopGetCurrent())
    , isTalker_(isTalker) {
    
    if (logger_) {
        logger_->debug("IsochPortChannelManager created, isTalker={}", isTalker_);
    }
    
    // Retain the interface reference (needed for proper RAII)
    if (interface_) {
        (*interface_)->AddRef(interface_);
    }
}

IsochPortChannelManager::~IsochPortChannelManager() {
    reset();
    
    if (logger_) {
        logger_->debug("IsochPortChannelManager destroyed");
    }
}

void IsochPortChannelManager::reset() {
    // Acquire lock for thread safety
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    // Clean up resources
    cleanupResources();
    
    // Clean up dispatchers if they were added
    if (dispatchersAdded_ && interface_) {
        cleanupDispatchers();
    }
    
    // Release interface last
    if (interface_) {
        (*interface_)->Release(interface_);
        interface_ = nullptr;
    }
    
    // Reset state
    initialized_ = false;
    finalized_ = false;
    running_ = false;
    
    if (logger_) {
        logger_->debug("IsochPortChannelManager reset completed");
    }
}

void IsochPortChannelManager::cleanupResources() noexcept {
    // Release isoch channel
    if (isochChannel_) {
        if (running_) {
            (*isochChannel_)->Stop(isochChannel_);
            (*isochChannel_)->ReleaseChannel(isochChannel_);
            running_ = false;
        }
        // Turn off notifications before releasing the channel
        if ((*isochChannel_)->TurnOffNotification) {
            (*isochChannel_)->TurnOffNotification(isochChannel_);
        }
        (*isochChannel_)->Release(isochChannel_);
        isochChannel_ = nullptr;
    }
    
    // Release local port
    if (localPort_) {
        (*localPort_)->Release(localPort_);
        localPort_ = nullptr;
    }
    
    // Release remote port
    if (remotePort_) {
        (*remotePort_)->Release(remotePort_);
        remotePort_ = nullptr;
    }
    
    // Release NuDCL pool
    if (nuDCLPool_) {
        (*nuDCLPool_)->Release(nuDCLPool_);
        nuDCLPool_ = nullptr;
    }
    
    // Reset active channel
    activeChannel_ = kAnyAvailableIsochChannel;
}

void IsochPortChannelManager::cleanupDispatchers() noexcept {
    if (!interface_ || !dispatchersAdded_) {
        return;
    }  
    
    // Remove dispatchers from RunLoop
    (*interface_)->RemoveIsochCallbackDispatcherFromRunLoop(interface_);
    (*interface_)->RemoveCallbackDispatcherFromRunLoop(interface_);
    
    dispatchersAdded_ = false;
    
    if (logger_) {
        logger_->debug("IsochPortChannelManager::cleanupDispatchers: Dispatchers removed from RunLoop");
    }
}

std::expected<void, IOKitError> IsochPortChannelManager::initialize() {
    // Acquire lock for thread safety
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (initialized_) {
        return std::unexpected(IOKitError::Busy);
    }
    
    if (!interface_) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::initialize: interface is null");
        }
        return std::unexpected(IOKitError::BadArgument);
    }
    
    if (logger_) {
        logger_->info("IsochPortChannelManager::initialize: isTalker={}, runLoop={:p}",
                      isTalker_, (void*)runLoop_);
    }
    
    // Add dispatchers to RunLoop
    auto result = setupDispatchers();
    if (!result) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::initialize: Failed to setup dispatchers: {}",
                          static_cast<int>(result.error()));
        }
        return result;
    }
    
    // Setup NuDCL pool
    result = setupNuDCLPool();
    if (!result) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::initialize: Failed to setup NuDCL pool: {}",
                           static_cast<int>(result.error()));
        }
        cleanupDispatchers();
        return result;
    }

    os_log(OS_LOG_DEFAULT, "IsochPortChannelManager::initialize: NuDCL pool created");
    
    // Create remote port
    result = createRemotePort();
    if (!result) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::initialize: Failed to create remote port: {}",
                           static_cast<int>(result.error()));
        }
        
        // Clean up resources created so far
        if (nuDCLPool_) {
            (*nuDCLPool_)->Release(nuDCLPool_);
            nuDCLPool_ = nullptr;
        }
        
        cleanupDispatchers();
        return result;
    }

    os_log(OS_LOG_DEFAULT, "IsochPortChannelManager::initialize: Remote port created");
    
    // Mark initialization as successful
    initialized_ = true;
    
    if (logger_) {
        logger_->info("IsochPortChannelManager::initialize: completed successfully");
    }

    os_log(OS_LOG_DEFAULT, "IsochPortChannelManager::initialize: Initialization complete");
    
    return {};
}

std::expected<void, IOKitError> IsochPortChannelManager::setupDispatchers() {
    if (!interface_) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::setupDispatchers: interface is null");
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    // Add required dispatchers to RunLoop
    IOReturn ret = (*interface_)->AddCallbackDispatcherToRunLoop(interface_, runLoop_);
    if (ret != kIOReturnSuccess) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::setupDispatchers: Failed to add callback dispatcher to RunLoop: 0x{:08X}", ret);
        }
        return std::unexpected(IOKitError(ret));
    }
    
        ret = (*interface_)->AddIsochCallbackDispatcherToRunLoop(interface_, runLoop_);
    if (ret != kIOReturnSuccess) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::setupDispatchers: Failed to add isoch callback dispatcher to RunLoop: 0x{:08X}", ret);
        }
        (*interface_)->RemoveCallbackDispatcherFromRunLoop(interface_);
        return std::unexpected(IOKitError(ret));
    }

        /* NEW ---- enable notifications ---- */
    if (!(*interface_)->TurnOnNotification(interface_)) {
        (*interface_)->RemoveIsochCallbackDispatcherFromRunLoop(interface_);
        (*interface_)->RemoveCallbackDispatcherFromRunLoop(interface_);
        logger_->error("IsochPortChannelManager::setupDispatchers: Failed to enable notifications");
        return std::unexpected(IOKitError::NoMemory);
    } else {
        if (logger_) {
            logger_->info("IsochPortChannelManager::setupDispatchers: Notifications enabled");
        }
    }

    
    dispatchersAdded_ = true;
    
    if (logger_) {
        logger_->info("IsochPortChannelManager::setupDispatchers: dispatchers added to RunLoop");
    }
    
    return {};
}

std::expected<void, IOKitError> IsochPortChannelManager::setupNuDCLPool() {
    if (!interface_) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::setupNuDCLPool: interface is null");
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    // Create the NuDCL pool
    nuDCLPool_ = (*interface_)->CreateNuDCLPool(
                                                interface_,
                                                0,
                                                CFUUIDGetUUIDBytes(kIOFireWireNuDCLPoolInterfaceID));
    
    if (!nuDCLPool_) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::setupNuDCLPool: failed to create NuDCL pool");
        }
        return std::unexpected(IOKitError::NoMemory);
    }
    
    if (logger_) {
        logger_->debug("IsochPortChannelManager::setupNuDCLPool: NuDCL pool created");
    }
    
    return {};
}

std::expected<void, IOKitError> IsochPortChannelManager::createRemotePort() {
    if (!interface_) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::createRemotePort: interface is null");
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    // Create remote isoch port
    // Note: Remote port is listener if local is talker, and vice versa
    remotePort_ = (*interface_)->CreateRemoteIsochPort(
                                                       interface_,
                                                       !isTalker_,  // opposite of local role
                                                       CFUUIDGetUUIDBytes(kIOFireWireRemoteIsochPortInterfaceID));
    
    if (!remotePort_) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::createRemotePort: failed to create remote port");
        }
        return std::unexpected(IOKitError::NoMemory);
    }
    
    // Set this instance as the refCon for the port
    (*remotePort_)->SetRefCon((IOFireWireLibIsochPortRef)remotePort_, this);
    
    // Set up callback handlers
    (*remotePort_)->SetGetSupportedHandler(remotePort_, RemotePort_GetSupported_Helper);
    (*remotePort_)->SetAllocatePortHandler(remotePort_, RemotePort_AllocatePort_Helper);
    (*remotePort_)->SetReleasePortHandler(remotePort_, RemotePort_ReleasePort_Helper);
    (*remotePort_)->SetStartHandler(remotePort_, RemotePort_Start_Helper);
    (*remotePort_)->SetStopHandler(remotePort_, RemotePort_Stop_Helper);
    
    if (logger_) {
        logger_->debug("IsochPortChannelManager::createRemotePort: remote port created");
    }
    
    return {};
}

std::expected<void, IOKitError> IsochPortChannelManager::createLocalPort(
    DCLCommand* program,
    const IOVirtualRange& bufferRange) {

    if (!interface_) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::createLocalPort: Interface is null");
        }
        return std::unexpected(IOKitError::NotReady);
    }

    if (!nuDCLPool_) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::createLocalPort: NuDCL pool is null");
        }
        return std::unexpected(IOKitError::NotReady);
    }

    if (!program) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::createLocalPort: DCL program is null");
        }
        return std::unexpected(IOKitError::BadArgument);
    }

    if (!bufferRange.address || !bufferRange.length) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::createLocalPort: Invalid buffer range");
        }
        return std::unexpected(IOKitError::BadArgument);
    }

    // Buffer must be 32-bit aligned - check without using reinterpret_cast
    if ((bufferRange.address & 0x3ULL) != 0) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::createLocalPort: Buffer not 32-bit aligned");
        }
        return std::unexpected(IOKitError::BadArgument);
    }

    if (logger_) {
        logger_->info("IsochPortChannelManager::createLocalPort: Creating port with buffer at {:p}, length={}",
                     (void*)(uintptr_t)bufferRange.address, bufferRange.length);
    }

    // Create a non-const copy of the buffer range to pass to CreateLocalIsochPort
    IOVirtualRange bufferRangeCopy = bufferRange;

    // Create the local port using correct DCLCommand* parameter
    localPort_ = (*interface_)->CreateLocalIsochPort(
        interface_,
        isTalker_,              // True for talker, false for listener
        program,                // DCL program handle (DCLCommand*) from GetProgram
        0,                      // startEvent
        0,                      // startState
        0,                      // startMask
        nullptr,                // dclProgramRanges - we don't use this for our simple config
        0,                      // dclProgramRangeCount
        reinterpret_cast<::IOVirtualRange*>(&bufferRangeCopy),  // Cast to the system IOVirtualRange type
        1,                      // bufferRangeCount
        CFUUIDGetUUIDBytes(kIOFireWireLocalIsochPortInterfaceID)
    );

    if (!localPort_) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::createLocalPort: CreateLocalIsochPort failed");
        }
        return std::unexpected(IOKitError::NoMemory);
    }

    // Set refcon pointer for callbacks
    (*localPort_)->SetRefCon((IOFireWireLibIsochPortRef)localPort_, this);

    // Set finalize callback
    IOReturn result = (*localPort_)->SetFinalizeCallback(localPort_, PortFinalize_Helper);
    if (result != kIOReturnSuccess) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::createLocalPort: Failed to set finalize callback: 0x{:08X}", result);
        }
        // Clean up local port
        (*localPort_)->Release(localPort_);
        localPort_ = nullptr;
        return std::unexpected(IOKitError(result));
    }

    if (logger_) {
        logger_->debug("IsochPortChannelManager::createLocalPort: Local port created successfully");
    }
    return {};
}

std::expected<void, IOKitError> IsochPortChannelManager::createIsochChannel() {
    if (!interface_) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::createIsochChannel: interface is null");
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    if (!localPort_ || !remotePort_) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::createIsochChannel: Ports are not initialized");
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    // Calculate packet size for IRM allocations - this should be determined based on audio needs
    const uint32_t irmPacketSize = 72; // 64 bytes for samples + 8 bytes for CIP header
    
    // Create the isoch channel
    isochChannel_ = (*interface_)->CreateIsochChannel(
                                                      interface_,
                                                      true,  // doIRMAllocations
                                                      irmPacketSize,
                                                      kFWSpeedMaximum,
                                                      CFUUIDGetUUIDBytes(kIOFireWireIsochChannelInterfaceID));
    
    if (!isochChannel_) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::createIsochChannel: Failed to create isoch channel");
        }
        return std::unexpected(IOKitError::Error);
    }
    
    // Set up channel with appropriate talker/listener roles
    IOReturn result;
    if (isTalker_) {
        // We are talker, remote is listener
        if (logger_) {
            logger_->info("IsochPortChannelManager::createIsochChannel: This is a talker, remote is listener");
        }
        
        result = (*isochChannel_)->AddListener(
                                               isochChannel_,
                                               reinterpret_cast<IOFireWireLibIsochPortRef>(remotePort_));
        
        if (result != kIOReturnSuccess) {
            if (logger_) {
                logger_->error("IsochPortChannelManager::createIsochChannel: Failed to add listener: 0x{:08X}", result);
            }
            return std::unexpected(IOKitError(result));
        }
        
        result = (*isochChannel_)->SetTalker(
                                             isochChannel_,
                                             reinterpret_cast<IOFireWireLibIsochPortRef>(localPort_));
        
        if (result != kIOReturnSuccess) {
            if (logger_) {
                logger_->error("IsochPortChannelManager::createIsochChannel: Failed to set talker: 0x{:08X}", result);
            }
            return std::unexpected(IOKitError(result));
        }
    } else {
        // We are listener, remote is talker
        if (logger_) {
            logger_->info("IsochPortChannelManager::createIsochChannel: This is a listener, remote is talker");
        }
        
        result = (*isochChannel_)->AddListener(
                                               isochChannel_,
                                               reinterpret_cast<IOFireWireLibIsochPortRef>(localPort_));
        
        if (result != kIOReturnSuccess) {
            if (logger_) {
                logger_->error("IsochPortChannelManager::createIsochChannel: Failed to add listener: 0x{:08X}", result);
            }
            return std::unexpected(IOKitError(result));
        }
        
        result = (*isochChannel_)->SetTalker(
                                             isochChannel_,
                                             reinterpret_cast<IOFireWireLibIsochPortRef>(remotePort_));
        
        if (result != kIOReturnSuccess) {
            if (logger_) {
                logger_->error("IsochPortChannelManager::createIsochChannel: Failed to set talker: 0x{:08X}", result);
            }
            return std::unexpected(IOKitError(result));
        }
    }
    
    /* NEW – enable packet notifications so SetDCLCallback() can fire   */
    if ((*isochChannel_)->TurnOnNotification &&
        !(*isochChannel_)->TurnOnNotification(isochChannel_))        /* returns bool */
    {
        logger_->error("IsochPortChannelManager: TurnOnNotification failed");
        return std::unexpected(IOKitError::Error);
    }

    // Set this as the refcon for the channel
    (*isochChannel_)->SetRefCon(isochChannel_, this);

    if (logger_) {
        logger_->debug("IsochPortChannelManager::createIsochChannel: Isoch channel created successfully");
    }
    
    
    return {};
}

std::expected<void, IOKitError> IsochPortChannelManager::setupLocalPortAndChannel(
    DCLCommand* program,
    const IOVirtualRange& bufferRange) {

    std::lock_guard<std::mutex> lock(stateMutex_);

    if (!initialized_) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::setupLocalPortAndChannel: Not initialized");
        }
        return std::unexpected(IOKitError::NotReady);
    }

    // Create local port first
    auto result = createLocalPort(program, bufferRange);
    if (!result) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::setupLocalPortAndChannel: Failed to create local port: {}",
                         iokit_error_category().message(static_cast<int>(result.error())));
        }
        return result;
    }

    // Then create the isoch channel
    result = createIsochChannel();
    if (!result) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::setupLocalPortAndChannel: Failed to create isoch channel: {}",
                         iokit_error_category().message(static_cast<int>(result.error())));
        }
        // Cleanup local port if channel creation fails
        if (localPort_) {
            (*localPort_)->Release(localPort_);
            localPort_ = nullptr;
        }
        return result;
    }

    if (logger_) {
        logger_->info("IsochPortChannelManager::setupLocalPortAndChannel: Port and channel setup successfully");
    }
    return {};
}

std::expected<void, IOKitError> IsochPortChannelManager::configure(IOFWSpeed speed, uint32_t channel) {
    // Acquire lock for thread safety
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (!initialized_) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::configure: Not initialized");
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    if (running_) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::configure: Cannot configure while running");
        }
        return std::unexpected(IOKitError::Busy);
    }
    
    configuredSpeed_ = speed;
    configuredChannel_ = channel;
    
    if (logger_) {
        logger_->info("IsochPortChannelManager::configure: Set speed={}, channel={}",
                      static_cast<int>(speed), channel);
    }
    
    return {};
}

IOFireWireLibNuDCLPoolRef IsochPortChannelManager::getNuDCLPool() const {
    return nuDCLPool_;
}

IOFireWireLibLocalIsochPortRef IsochPortChannelManager::getLocalPort() const {
    return localPort_;
}

IOFireWireLibIsochChannelRef IsochPortChannelManager::getIsochChannel() const {
    return isochChannel_;
}

std::expected<uint32_t, IOKitError> IsochPortChannelManager::getActiveChannel() const {
    if (!initialized_) {
        if (logger_) {
            logger_->error("IsochPortChannelManager::getActiveChannel: Not initialized");
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    return activeChannel_;
}

std::expected<uint16_t, IOKitError> IsochPortChannelManager::getLocalNodeID() const {
     if (!interface_) {
         if (logger_) logger_->error("IsochPortChannelManager::getLocalNodeID: interface is null");
         return std::unexpected(IOKitError::NotReady);
     }
     
     UInt32 generation = 0;
     UInt16 nodeID = 0;
     
     IOReturn result = (*interface_)->GetBusGeneration(interface_, &generation);
     if (result != kIOReturnSuccess) {
          if(logger_) logger_->error("IsochPortChannelManager::getLocalNodeID: Failed to get bus generation: {:#0x}", result);
         return std::unexpected(IOKitError(result));
     }
     
     result = (*interface_)->GetLocalNodeIDWithGeneration(interface_, generation, &nodeID);
     if (result != kIOReturnSuccess) {
          if(logger_) logger_->error("IsochPortChannelManager::getLocalNodeID: Failed to get local node ID: {:#0x}", result);
         return std::unexpected(IOKitError(result));
     }
     
     if(logger_) logger_->trace("IsochPortChannelManager::getLocalNodeID: Got local node ID: {:#x}", nodeID);
     return nodeID;
}

void IsochPortChannelManager::handlePortFinalize() {
    if (logger_) {
        logger_->debug("IsochPortChannelManager::handlePortFinalize: Port finalize called");
    }
    
    finalized_ = true;
}

// Static callback handlers
IOReturn IsochPortChannelManager::RemotePort_GetSupported_Helper(
                                                      IOFireWireLibIsochPortRef interface,
                                                      IOFWSpeed *outMaxSpeed,
                                                      UInt64 *outChanSupported) {
    
    // Get the IsochPortChannelManager instance from the refcon
    auto manager = static_cast<IsochPortChannelManager*>((*interface)->GetRefCon(interface));
    if (!manager) {
        return kIOReturnBadArgument;
    }
    
    return manager->handleRemotePortGetSupported(outMaxSpeed, outChanSupported);
}

IOReturn IsochPortChannelManager::RemotePort_AllocatePort_Helper(
                                                      IOFireWireLibIsochPortRef interface,
                                                      IOFWSpeed maxSpeed,
                                                      UInt32 channel) {
    
    // Get the IsochPortChannelManager instance from the refcon
    auto manager = static_cast<IsochPortChannelManager*>((*interface)->GetRefCon(interface));
    if (!manager) {
        return kIOReturnBadArgument;
    }
    
    return manager->handleRemotePortAllocatePort(maxSpeed, channel);
}

IOReturn IsochPortChannelManager::RemotePort_ReleasePort_Helper(
                                                     IOFireWireLibIsochPortRef interface) {
    
    // Get the IsochPortChannelManager instance from the refcon
    auto manager = static_cast<IsochPortChannelManager*>((*interface)->GetRefCon(interface));
    if (!manager) {
        return kIOReturnBadArgument;
    }
    
    return manager->handleRemotePortReleasePort();
}

IOReturn IsochPortChannelManager::RemotePort_Start_Helper(
                                               IOFireWireLibIsochPortRef interface) {
    
    // Get the IsochPortChannelManager instance from the refcon
    auto manager = static_cast<IsochPortChannelManager*>((*interface)->GetRefCon(interface));
    if (!manager) {
        return kIOReturnBadArgument;
    }
    
    return manager->handleRemotePortStart();
}

IOReturn IsochPortChannelManager::RemotePort_Stop_Helper(
                                              IOFireWireLibIsochPortRef interface) {
    
    // Get the IsochPortChannelManager instance from the refcon
    auto manager = static_cast<IsochPortChannelManager*>((*interface)->GetRefCon(interface));
    if (!manager) {
        return kIOReturnBadArgument;
    }
    
    return manager->handleRemotePortStop();
}

IOReturn IsochPortChannelManager::PortFinalize_Helper(void* refcon) {
    // Get the IsochPortChannelManager instance from the refcon
    auto manager = static_cast<IsochPortChannelManager*>(refcon);
    if (!manager) {
        return kIOReturnBadArgument;
    }
    
    manager->handlePortFinalize();
    
    return kIOReturnSuccess;
}

// Instance methods called by static helpers
IOReturn IsochPortChannelManager::handleRemotePortGetSupported(IOFWSpeed *outMaxSpeed, UInt64 *outChanSupported) {
    // Use stored speed from configuration
    *outMaxSpeed = configuredSpeed_;
    
    // Handle channel selection based on configuration
    uint32_t channel = configuredChannel_;
    if (channel == kAnyAvailableIsochChannel) {
        // Allow FireWireFamily to determine an available channel
        // Enable all channels except 0 (which is reserved)
        *outChanSupported = ~1ULL;
    } else {
        // Use a specific channel - create a mask with only that bit set
        *outChanSupported = (((UInt64)0x80000000 << 32 | (UInt64)0x00000000) >> channel);
    }
    
    if (logger_) {
        logger_->debug("RemotePort_GetSupported: speed={}, channel={}",
                        static_cast<int>(*outMaxSpeed),
                        (channel == kAnyAvailableIsochChannel) ?
                        "any" : std::to_string(channel));
    }
    
    return kIOReturnSuccess;
}

IOReturn IsochPortChannelManager::handleRemotePortAllocatePort(IOFWSpeed maxSpeed, UInt32 channel) {
    // Store the allocated channel for future reference
    activeChannel_ = channel;
    
    if (logger_) {
        logger_->debug("RemotePort_AllocatePort: speed={}, channel={}",
                        static_cast<int>(maxSpeed), channel);
    }
    
    return kIOReturnSuccess;
}

IOReturn IsochPortChannelManager::handleRemotePortReleasePort() {
    // Reset active channel
    activeChannel_ = kAnyAvailableIsochChannel;
    
    if (logger_) {
        logger_->debug("RemotePort_ReleasePort called");
    }
    
    return kIOReturnSuccess;
}

IOReturn IsochPortChannelManager::handleRemotePortStart() {
    // Set the running state
    running_ = true;
    
    if (logger_) {
        logger_->debug("RemotePort_Start called");
    }
    
    return kIOReturnSuccess;
}

IOReturn IsochPortChannelManager::handleRemotePortStop() {
    // Clear the running state
    running_ = false;
    
    if (logger_) {
        logger_->debug("RemotePort_Stop called");
    }
    
    return kIOReturnSuccess;
}

} // namespace Isoch
} // namespace FWA
