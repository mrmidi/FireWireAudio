#include "Isoch/core/IsochManager.hpp"
#include <spdlog/spdlog.h>
#include <unistd.h>

namespace FWA {
namespace Isoch {

IsochManager::IsochManager(std::shared_ptr<spdlog::logger> logger)
: logger_(std::move(logger)) {
    if (logger_) {
        logger_->debug("IsochManager created");
    }
}

IsochManager::~IsochManager() {
    reset();
    
    if (logger_) {
        logger_->debug("IsochManager destroyed");
    }
}

void IsochManager::reset() {
    // Acquire lock for thread safety
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    // Release isoch channel
    if (isochChannel_) {
        if (running_) {
            (*isochChannel_)->Stop(isochChannel_);
            (*isochChannel_)->ReleaseChannel(isochChannel_);
            running_ = false;
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
    
    // Release NuDCL pool and free update bags
    if (nuDCLPool_) {
        // Clean up update bags
        for (auto& segment : segments_) {
            if (segment.updateBag) {
                CFRelease(segment.updateBag);
                segment.updateBag = nullptr;
            }
        }
        
        (*nuDCLPool_)->Release(nuDCLPool_);
        nuDCLPool_ = nullptr;
    }
    
    // Clear segment information
    segments_.clear();
    overrunDCL_ = nullptr;
    
    // Clear buffer pointers
    baseBuffer_ = nullptr;
    timestampBuffer_ = nullptr;
    
    // Reset state
    initialized_ = false;
    finalized_ = false;
    running_ = false;
    
    if (logger_) {
        logger_->debug("IsochManager reset completed");
    }
}

std::expected<void, IOKitError> IsochManager::initialize(
                                                         IOFireWireLibNubRef interface,
                                                         bool isTalker,
                                                         CFRunLoopRef runLoop) {
    
    // Acquire lock for thread safety
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (initialized_) {
        return std::unexpected(IOKitError::Busy);
    }
    
    if (!interface) {
        if (logger_) {
            logger_->error("IsochManager::initialize: interface is null");
        }
        return std::unexpected(IOKitError::BadArgument);
    }
    
    // Store parameters
    interface_ = interface;
    isTalker_ = isTalker;
    runLoop_ = runLoop ? runLoop : CFRunLoopGetCurrent();
    
    if (logger_) {
        logger_->info("IsochManager::initialize: isTalker={}, runLoop={:p}",
                      isTalker_, (void*)runLoop_);
    }
    
    // Retain the interface reference (needed for proper RAII)
    (*interface_)->AddRef(interface_);
    
    // Add required dispatchers to RunLoop
    IOReturn ret = (*interface_)->AddCallbackDispatcherToRunLoop(interface_, runLoop_);
    if (ret != kIOReturnSuccess) {
        if (logger_) {
            logger_->error("Failed to add callback dispatcher to RunLoop: 0x{:08X}", ret);
        }
        (*interface_)->Release(interface_);
        interface_ = nullptr;
        return std::unexpected(IOKitError(ret));
    }
    
    ret = (*interface_)->AddIsochCallbackDispatcherToRunLoop(interface_, runLoop_);
    if (ret != kIOReturnSuccess) {
        if (logger_) {
            logger_->error("Failed to add isoch callback dispatcher to RunLoop: 0x{:08X}", ret);
        }
        (*interface_)->RemoveCallbackDispatcherFromRunLoop(interface_);
        (*interface_)->Release(interface_);
        interface_ = nullptr;
        return std::unexpected(IOKitError(ret));
    }
    
    if (logger_) {
        logger_->info("IsochManager::initialize: dispatchers added to RunLoop");
    }
    
    // Setup NuDCL pool (this must be done first)
    auto result = setupNuDCLPool();
    if (!result) {
        if (logger_) {
            logger_->error("Failed to setup NuDCL pool: {}",
                           static_cast<int>(result.error()));
        }
        
        // Clean up
        (*interface_)->RemoveIsochCallbackDispatcherFromRunLoop(interface_);
        (*interface_)->RemoveCallbackDispatcherFromRunLoop(interface_);
        (*interface_)->Release(interface_);
        interface_ = nullptr;
        return result;
    }
    
    // Remote port is required for both talker and listener roles
    result = createRemotePort();
    if (!result) {
        if (logger_) {
            logger_->error("Failed to create remote port: {}",
                           static_cast<int>(result.error()));
        }
        
        reset();
        return result;
    }
    
    // Mark initialization as successful at this point
    // Note: Local port and isoch channel will be created when createDCLProgram is called
    initialized_ = true;
    
    if (logger_) {
        logger_->info("IsochManager::initialize: completed successfully");
    }
    
    return {};
}

std::expected<void, IOKitError> IsochManager::setupNuDCLPool() {
    if (!interface_) {
        if (logger_) {
            logger_->error("IsochManager::setupNuDCLPool: interface is null");
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
            logger_->error("IsochManager::setupNuDCLPool: failed to create NuDCL pool");
        }
        return std::unexpected(IOKitError::NoMemory);
    }
    
    if (logger_) {
        logger_->debug("IsochManager::setupNuDCLPool: NuDCL pool created");
    }
    
    return {};
}

std::expected<void, IOKitError> IsochManager::createRemotePort() {
    if (!interface_) {
        if (logger_) {
            logger_->error("IsochManager::createRemotePort: interface is null");
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
            logger_->error("IsochManager::createRemotePort: failed to create remote port");
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
        logger_->debug("IsochManager::createRemotePort: remote port created");
    }
    
    return {};
}

std::expected<void, IOKitError> IsochManager::createLocalPort(IOVirtualRange& bufferRange) {
    if (!interface_) {
        if (logger_) {
            logger_->error("IsochManager::createLocalPort: interface is null");
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    if (!nuDCLPool_) {
        if (logger_) {
            logger_->error("IsochManager::createLocalPort: NuDCL pool is null");
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    // Get the DCL program
    DCLCommandPtr program = (*nuDCLPool_)->GetProgram(nuDCLPool_);
    if (!program) {
        if (logger_) {
            logger_->error("IsochManager::createLocalPort: GetProgram returned null");
        }
        return std::unexpected(IOKitError::Error);
    }
    
    // Validate buffer range
    if (!bufferRange.address || !bufferRange.length) {
        if (logger_) {
            logger_->error("IsochManager::createLocalPort: Invalid buffer range: address={:p}, length={}",
                           (void*)bufferRange.address, bufferRange.length);
        }
        return std::unexpected(IOKitError::BadArgument);
    }
    
    // Check buffer alignment (4-byte alignment required)
    if (bufferRange.address & 0x3) {
        if (logger_) {
            logger_->error("IsochManager::createLocalPort: Buffer not 4-byte aligned: address={:p}",
                           (void*)bufferRange.address);
        }
        return std::unexpected(IOKitError::NotAligned);
    }
    
    if (logger_) {
        logger_->info("IsochManager::createLocalPort: Creating local port with buffer: address={:p}, length={}",
                      (void*)bufferRange.address, bufferRange.length);
    }
    
    // Create the local port
    localPort_ = (*interface_)->CreateLocalIsochPort(
                                                     interface_,
                                                     isTalker_,
                                                     program,
                                                     0,              // startEvent
                                                     0,              // startState
                                                     0,              // startMask
                                                     nullptr,        // dclProgramRanges
                                                     0,              // dclProgramRangeCount
                                                     &bufferRange,   // bufferRanges - now compatible with system type
                                                     1,              // bufferRangeCount
                                                     CFUUIDGetUUIDBytes(kIOFireWireLocalIsochPortInterfaceID));
    
    if (!localPort_) {
        if (logger_) {
            logger_->error("IsochManager::createLocalPort: Failed to create local port");
        }
        return std::unexpected(IOKitError::NoMemory);
    }
    
    // Set this instance as the refCon for the port
    (*localPort_)->SetRefCon((IOFireWireLibIsochPortRef)localPort_, this);
    
    
    
    
    // Set the finalize callback
    IOReturn result = (*localPort_)->SetFinalizeCallback(localPort_, PortFinalize_Helper);
    if (result != kIOReturnSuccess) {
        if (logger_) {
            logger_->error("IsochManager::createLocalPort: Failed to set finalize callback: {}", result);
        }
        return std::unexpected(IOKitError(result));
    }
    
    if (logger_) {
        logger_->debug("IsochManager::createLocalPort: Local port created successfully");
    }
    
    return {};
}

std::expected<void, IOKitError> IsochManager::createIsochChannel() {
    if (!interface_) {
        if (logger_) {
            logger_->error("IsochManager::createIsochChannel: interface is null");
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    if (!localPort_ || !remotePort_) {
        if (logger_) {
            logger_->error("IsochManager::createIsochChannel: Ports are not initialized");
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
            logger_->error("IsochManager::createIsochChannel: Failed to create isoch channel");
        }
        return std::unexpected(IOKitError::Error);
    }
    
    // Set up channel with appropriate talker/listener roles
    IOReturn result;
    if (isTalker_) {
        // We are talker, remote is listener
        if (logger_) {
            logger_->info("IsochManager::createIsochChannel: This is a talker, remote is listener");
        }
        
        result = (*isochChannel_)->AddListener(
                                               isochChannel_,
                                               reinterpret_cast<IOFireWireLibIsochPortRef>(remotePort_));
        
        if (result != kIOReturnSuccess) {
            if (logger_) {
                logger_->error("IsochManager::createIsochChannel: Failed to add listener: 0x{:08X}", result);
            }
            return std::unexpected(IOKitError(result));
        }
        
        result = (*isochChannel_)->SetTalker(
                                             isochChannel_,
                                             reinterpret_cast<IOFireWireLibIsochPortRef>(localPort_));
        
        if (result != kIOReturnSuccess) {
            if (logger_) {
                logger_->error("IsochManager::createIsochChannel: Failed to set talker: 0x{:08X}", result);
            }
            return std::unexpected(IOKitError(result));
        }
    } else {
        // We are listener, remote is talker
        if (logger_) {
            logger_->info("IsochManager::createIsochChannel: This is a listener, remote is talker");
        }
        
        result = (*isochChannel_)->AddListener(
                                               isochChannel_,
                                               reinterpret_cast<IOFireWireLibIsochPortRef>(localPort_));
        
        if (result != kIOReturnSuccess) {
            if (logger_) {
                logger_->error("IsochManager::createIsochChannel: Failed to add listener: 0x{:08X}", result);
            }
            return std::unexpected(IOKitError(result));
        }
        
        result = (*isochChannel_)->SetTalker(
                                             isochChannel_,
                                             reinterpret_cast<IOFireWireLibIsochPortRef>(remotePort_));
        
        if (result != kIOReturnSuccess) {
            if (logger_) {
                logger_->error("IsochManager::createIsochChannel: Failed to set talker: 0x{:08X}", result);
            }
            return std::unexpected(IOKitError(result));
        }
    }
    
    
    // fix up the DCL program here!
    auto fixupResult = fixupDCLJumpTargets();
    if (!fixupResult) {
        if (logger_) {
            logger_->error("IsochManager::createIsochChannel: Failed to fix up DCL jump targets: {}",
                           static_cast<int>(fixupResult.error()));
        }
        return fixupResult;
    }
    
    logger_->info("IsochManager::createIsochChannel: FIXUP SUCCESSFUL");
    
    
    
    
    // Set this as the refcon for the channel
    (*isochChannel_)->SetRefCon(isochChannel_, this);
    
    if (logger_) {
        logger_->debug("IsochManager::createIsochChannel: Isoch channel created successfully");
    }
    
    return {};
}

std::expected<void, IOKitError> IsochManager::createDCLProgram(
                                                               uint32_t cyclesPerSegment,
                                                               uint32_t numSegments,
                                                               uint32_t cycleBufferSize,
                                                               IOVirtualRange& bufferRange) {
    
    // Acquire lock for thread safety
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (!initialized_) {
        if (logger_) {
            logger_->error("IsochManager::createDCLProgram: Not initialized");
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    if (!nuDCLPool_) {
        if (logger_) {
            logger_->error("IsochManager::createDCLProgram: NuDCL pool is null");
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    // Store configuration values
    cyclesPerSegment_ = cyclesPerSegment;
    numSegments_ = numSegments;
    cycleBufferSize_ = cycleBufferSize;
    baseBuffer_ = reinterpret_cast<uint8_t*>(bufferRange.address);
    
    if (logger_) {
        logger_->info("IsochManager::createDCLProgram: cyclesPerSegment={}, numSegments={}, cycleBufferSize={}",
                      cyclesPerSegment_, numSegments_, cycleBufferSize_);
        logger_->info("IsochManager::createDCLProgram: Using buffer range: address={:p}, length={}",
                      (void*)bufferRange.address, bufferRange.length);
    }
    
    // Calculate total buffer size needed for all cycle buffers
    uint32_t totalCyclesBufferSize = cyclesPerSegment_ * numSegments_ * cycleBufferSize_;
    uint32_t overrunBufferSize = cycleBufferSize_; // One additional buffer for overruns
    uint32_t timestampBufferSize = numSegments_ * sizeof(uint32_t); // One timestamp per segment
    
    size_t totalRequiredSize = totalCyclesBufferSize + overrunBufferSize + timestampBufferSize;
    
    if (totalRequiredSize > bufferRange.length) {
        if (logger_) {
            logger_->error("IsochManager::createDCLProgram: Buffer too small: needed={}, provided={}",
                           totalRequiredSize, bufferRange.length);
        }
        return std::unexpected(IOKitError::NoSpace);
    }
    
    // Prepare segments vector
    segments_.resize(numSegments_);
    
    // Calculate timestamp buffer location
    timestampBuffer_ = reinterpret_cast<uint32_t*>(
                                                   baseBuffer_ + totalCyclesBufferSize + overrunBufferSize);
    
    // Create segment DCLs with direct buffer reference
    auto result = createSegmentDCLs(bufferRange);
    if (!result) {
        if (logger_) {
            logger_->error("IsochManager::createDCLProgram: Failed to create segment DCLs: {}",
                           static_cast<int>(result.error()));
        }
        segments_.clear();
        return result;
    }
    
    // Create overrun DCL
    result = createOverrunDCL(bufferRange);
    if (!result) {
        if (logger_) {
            logger_->error("IsochManager::createDCLProgram: Failed to create overrun DCL: {}",
                           static_cast<int>(result.error()));
        }
        segments_.clear();
        return result;
    }
    
    // Create the local port
    result = createLocalPort(bufferRange);
    if (!result) {
        if (logger_) {
            logger_->error("IsochManager::createDCLProgram: Failed to create local port: {}",
                           static_cast<int>(result.error()));
        }
        segments_.clear();
        overrunDCL_ = nullptr;
        return result;
    }
    
    // Create the isoch channel
    result = createIsochChannel();
    if (!result) {
        if (logger_) {
            logger_->error("IsochManager::createDCLProgram: Failed to create isoch channel: {}",
                           static_cast<int>(result.error()));
        }
        // Local port will be cleaned up in reset()
        segments_.clear();
        overrunDCL_ = nullptr;
        return result;
    }
    
    // Fix up DCL jump targets
    result = fixupDCLJumpTargets();
    if (!result) {
        if (logger_) {
            logger_->error("IsochManager::createDCLProgram: Failed to fix up DCL jump targets: {}",
                           static_cast<int>(result.error()));
        }
        segments_.clear();
        overrunDCL_ = nullptr;
        return result;
    }
    
    // Reset segment position to start
    currentSegment_ = 0;
    
    if (logger_) {
        logger_->info("IsochManager::createDCLProgram: DCL program created successfully");
    }
    
    return {};
}

std::expected<void, IOKitError> IsochManager::createSegmentDCLs(IOVirtualRange& bufferRange) {
    if (!nuDCLPool_) {
        if (logger_) {
            logger_->error("IsochManager::createSegmentDCLs: NuDCL pool is null");
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    if (logger_) {
        logger_->debug("IsochManager::createSegmentDCLs: Creating DCLs for {} segments with {} cycles each",
                       numSegments_, cyclesPerSegment_);
        logger_->debug("IsochManager::createSegmentDCLs: Base buffer={:p}, timestamp buffer={:p}",
                       (void*)baseBuffer_, (void*)timestampBuffer_);
    }
    
    // Create DCLs for each segment
    for (uint32_t segment = 0; segment < numSegments_; segment++) {
        // Create a mutable bag for update lists
        segments_[segment].updateBag = CFSetCreateMutable(nullptr, 0, nullptr);
        if (!segments_[segment].updateBag) {
            if (logger_) {
                logger_->error("IsochManager::createSegmentDCLs: Failed to create update bag for segment {}", segment);
            }
            return std::unexpected(IOKitError::NoMemory);
        }
        
        // For each cycle in the segment
        for (uint32_t cycle = 0; cycle < cyclesPerSegment_; cycle++) {
            // Calculate buffer offset directly
            uint32_t bufferOffset = (segment * cyclesPerSegment_ + cycle) * cycleBufferSize_;
            
            // Create virtual range directly from main buffer
            IOVirtualRange range;
            range.address = bufferRange.address + bufferOffset;
            range.length = cycleBufferSize_;
            
            // Allocate receive packet DCL
            NuDCLRef dcl = (*nuDCLPool_)->AllocateReceivePacket(
                                                                nuDCLPool_,
                                                                segments_[segment].updateBag,
                                                                4,  // Header size in quadlets (4 bytes each)
                                                                1,  // Number of ranges
                                                                &range);
            
            if (!dcl) {
                if (logger_) {
                    logger_->error("IsochManager::createSegmentDCLs: Failed to allocate receive packet DCL");
                }
                return std::unexpected(IOKitError::NoMemory);
            }
            
            // Set DCL flags
            (*nuDCLPool_)->SetDCLFlags(dcl, kNuDCLDynamic | kNuDCLUpdateBeforeCallback);
            
            // Set refcon
            (*nuDCLPool_)->SetDCLRefcon(dcl, this);
            
            // Special handling for first DCL in segment
            if (cycle == 0) {
                segments_[segment].startDCL = dcl;
                
                // Set timestamp pointer directly to the timestamp buffer
                (*nuDCLPool_)->SetDCLTimeStampPtr(dcl, &timestampBuffer_[segment]);
                
                if (logger_) {
                    logger_->debug("IsochManager::createSegmentDCLs: Start DCL for segment {}: {:p}",
                                   segment, (void*)dcl);
                }
            }
            
            // Special handling for last DCL in segment
            if (cycle == cyclesPerSegment_ - 1) {
                segments_[segment].endDCL = dcl;
                
                // Set update list
                (*nuDCLPool_)->SetDCLUpdateList(dcl, segments_[segment].updateBag);
                
                // Set callback
                (*nuDCLPool_)->SetDCLCallback(dcl, DCLComplete_Helper);
                
                if (logger_) {
                    logger_->debug("IsochManager::createSegmentDCLs: End DCL for segment {}: {:p}",
                                   segment, (void*)dcl);
                }
            }
        }
        
        // Mark segment as not active initially
        segments_[segment].isActive = false;
        
        if (logger_) {
            logger_->debug("IsochManager::createSegmentDCLs: Created DCLs for segment {}", segment);
        }
    }
    
    return {};
}

std::expected<void, IOKitError> IsochManager::createOverrunDCL(IOVirtualRange& bufferRange) {
    if (!nuDCLPool_) {
        if (logger_) {
            logger_->error("IsochManager::createOverrunDCL: NuDCL pool is null");
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    // Calculate overrun buffer address
    uint32_t totalCyclesBufferSize = cyclesPerSegment_ * numSegments_ * cycleBufferSize_;
    
    // Create range for overrun buffer
    IOVirtualRange range;
    range.address = bufferRange.address + totalCyclesBufferSize;
    range.length = cycleBufferSize_;
    
    if (logger_) {
        logger_->debug("IsochManager::createOverrunDCL: Overrun buffer at address={:p}, length={}",
                       (void*)range.address, range.length);
    }
    
    // Create overrun DCL
    overrunDCL_ = (*nuDCLPool_)->AllocateReceivePacket(
                                                       nuDCLPool_,
                                                       nullptr,  // No update bag for overrun DCL
                                                       4,        // Header size in quadlets
                                                       1,        // Single range
                                                       &range);
    
    if (!overrunDCL_) {
        if (logger_) {
            logger_->error("IsochManager::createOverrunDCL: Failed to allocate overrun DCL");
        }
        return std::unexpected(IOKitError::NoMemory);
    }
    
    // Set DCL flags
    (*nuDCLPool_)->SetDCLFlags(overrunDCL_, kNuDCLDynamic | kNuDCLUpdateBeforeCallback);
    
    // Set refcon
    (*nuDCLPool_)->SetDCLRefcon(overrunDCL_, this);
    
    // Set callback
    (*nuDCLPool_)->SetDCLCallback(overrunDCL_, DCLOverrun_Helper);
    
       (*nuDCLPool_)->PrintProgram(nuDCLPool_);
    
    if (logger_) {
        logger_->debug("IsochManager::createOverrunDCL: Created overrun DCL: {:p}", (void*)overrunDCL_);
    }
    
    return {};
}

std::expected<void, IOKitError> IsochManager::fixupDCLJumpTargets() {
    if (!nuDCLPool_) {
        if (logger_) {
            logger_->error("IsochManager::fixupDCLJumpTargets: NuDCL pool is null");
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    if (!localPort_) {
        if (logger_) {
            logger_->error("IsochManager::fixupDCLJumpTargets: Local port is null");
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    if (segments_.empty() || !overrunDCL_) {
        if (logger_) {
            logger_->error("IsochManager::fixupDCLJumpTargets: Segments or overrun DCL not created");
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    if (logger_) {
        logger_->debug("IsochManager::fixupDCLJumpTargets: Fixing up DCL jump targets for {} segments", 
                     segments_.size());
    }
    
    // Fix up jump targets - each segment's last DCL jumps to the next segment
    for (size_t i = 0; i < segments_.size(); i++) {
        // Get the end DCL of the current segment
        NuDCLRef endDCL = segments_[i].endDCL;
        if (!endDCL) {
            if (logger_) {
                logger_->error("IsochManager::fixupDCLJumpTargets: End DCL for segment {} is null", i);
            }
            return std::unexpected(IOKitError::BadArgument);
        }
        
        // Set the branch target
        NuDCLRef targetDCL;
        if (i != (segments_.size() - 1)) {
            // Not the last segment, jump to the next segment's start
            targetDCL = segments_[i + 1].startDCL;
            if (logger_) {
                logger_->debug("IsochManager::fixupDCLJumpTargets: Segment {} jumps to segment {}", 
                               i, i + 1);
            }
        } else {
            // Last segment, jump to the overrun DCL
            targetDCL = overrunDCL_;
            if (logger_) {
                logger_->debug("IsochManager::fixupDCLJumpTargets: Last segment {} jumps to overrun DCL", i);
            }
        }
        
        if (!targetDCL) {
            if (logger_) {
                logger_->error("IsochManager::fixupDCLJumpTargets: Target DCL is null for segment {}", i);
            }
            return std::unexpected(IOKitError::BadArgument);
        }
        
        // Set the branch
        (*nuDCLPool_)->SetDCLBranch(endDCL, targetDCL);
        
        // Send notification
        IOReturn result = notifyJumpUpdate(endDCL);
        if (result != kIOReturnSuccess) {
            if (logger_) {
                logger_->error("IsochManager::fixupDCLJumpTargets: Notify failed for segment {}: 0x{:08X}", 
                             i, result);
            }
            // Continue despite error - not fatal
        }
    }
    
    // CRITICAL: Set the overrun DCL to jump back to the first segment
    if (overrunDCL_ && !segments_.empty() && segments_[0].startDCL) {
        (*nuDCLPool_)->SetDCLBranch(overrunDCL_, segments_[0].startDCL);
        
        // Add clear logging for this critical step
        logger_->info("IsochManager: Set overrun DCL {:p} to branch back to segment 0 start DCL {:p}", 
                     (void*)overrunDCL_, (void*)segments_[0].startDCL);
        
        // Send notification for overrun DCL
        IOReturn result = (*localPort_)->Notify(
            localPort_,
            kFWNuDCLModifyJumpNotification,
            (void**) &overrunDCL_,  // Use direct cast like in UniversalReceiver
            1);
            
        if (result != kIOReturnSuccess) {
            logger_->error("IsochManager::fixupDCLJumpTargets: Notify failed for overrun DCL: 0x{:08X}", 
                         result);
            // Continue despite error - not fatal
        } else {
            logger_->info("IsochManager: Successfully notified overrun DCL branch update");
        }
    } else {
        logger_->error("IsochManager: Cannot set overrun DCL branch - missing required DCLs");
    }

    logger_->info("DCL Program Flow: segment0 → segment1 → overrun → segment0");
    logger_->info("Segment Counter Initialization: currentSegment_ = 0");
    
    // Reset the current segment to 0
    currentSegment_ = 0;
    
    if (logger_) {
        logger_->debug("IsochManager::fixupDCLJumpTargets: Fixed up all DCL jump targets successfully");
    }
    
    return {};
}

IOReturn IsochManager::notifyJumpUpdate(NuDCLRef dcl) {
    if (!localPort_ || !dcl) {
        return kIOReturnBadArgument;
    }

    // CRITICAL: Must pass the ADDRESS OF the DCL pointer variable
    // Not just a copy of the pointer itself
    void** dclPtrAddr = (void**)&dcl;  // Get address of the DCL pointer
    
    return (*localPort_)->Notify(
        localPort_,
        kFWNuDCLModifyJumpNotification,
        dclPtrAddr,  // Pass address of the pointer, not a temporary
        1);
}

std::expected<void, IOKitError> IsochManager::configure(IOFWSpeed speed, uint32_t channel) {
    // Acquire lock for thread safety
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (!initialized_) {
        if (logger_) {
            logger_->error("IsochManager::configure: Not initialized");
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    if (running_) {
        if (logger_) {
            logger_->error("IsochManager::configure: Cannot configure while running");
        }
        return std::unexpected(IOKitError::Busy);
    }
    
    configuredSpeed_ = speed;
    configuredChannel_ = channel;
    
    if (logger_) {
        logger_->info("IsochManager::configure: Set speed={}, channel={}",
                      static_cast<int>(speed), channel);
    }
    
    return {};
}

std::expected<DCLCommandPtr, IOKitError> IsochManager::getProgram() const {
    if (!nuDCLPool_) {
        if (logger_) {
            logger_->error("IsochManager::getProgram: NuDCL pool is null");
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    DCLCommandPtr program = (*nuDCLPool_)->GetProgram(nuDCLPool_);
    if (!program) {
        if (logger_) {
            logger_->error("IsochManager::getProgram: GetProgram returned null");
        }
        return std::unexpected(IOKitError::Error);
    }
    
    return program;
}

std::expected<uint32_t*, IOKitError> IsochManager::getTimestampPtr(uint32_t segment) const {
    if (!initialized_ || !timestampBuffer_) {
        if (logger_) {
            logger_->error("IsochManager::getTimestampPtr: Not initialized or no timestamp buffer");
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    if (segment >= numSegments_) {
        if (logger_) {
            logger_->error("IsochManager::getTimestampPtr: Invalid segment {}", segment);
        }
        return std::unexpected(IOKitError::BadArgument);
    }
    
    return &timestampBuffer_[segment];
}

std::expected<uint32_t, IOKitError> IsochManager::getActiveChannel() const {
    if (!initialized_) {
        if (logger_) {
            logger_->error("IsochManager::getActiveChannel: Not initialized");
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    return activeChannel_;
}



std::expected<void, IOKitError> IsochManager::handleSegmentComplete(uint32_t segment) {
    if (segment >= segments_.size()) {
        if (logger_) {
            logger_->error("IsochManager::handleSegmentComplete: Invalid segment number: {}", segment);
        }
        return std::unexpected(IOKitError::BadArgument);
    }
    
    // Mark segment as inactive
    segments_[segment].isActive = false;
    
    if (logger_) {
        logger_->debug("IsochManager::handleSegmentComplete: Segment {} complete", segment);
    }
    
    // Notify callback with proper refcon
    if (dclCompleteCallback_) {
        dclCompleteCallback_(segment, dclCompleteRefCon_);
    }
    
    return {};
}

bool IsochManager::isValidSegment(uint32_t segment) const {
    return segment < segments_.size();
}

NuDCLRef IsochManager::getDCLForSegment(uint32_t segment, uint32_t cycle) const {
    if (!isValidSegment(segment) || cycle >= cyclesPerSegment_) {
        return nullptr;
    }
    
    if (cycle == 0) {
        return segments_[segment].startDCL;
    } else if (cycle == cyclesPerSegment_ - 1) {
        return segments_[segment].endDCL;
    } else {
        // For middle DCLs, we'd need to maintain an array of all DCLs
        // This is a limitation of the current implementation
        return nullptr;
    }
}

// Static callback handlers
IOReturn IsochManager::RemotePort_GetSupported_Helper(
                                                      IOFireWireLibIsochPortRef interface,
                                                      IOFWSpeed *outMaxSpeed,
                                                      UInt64 *outChanSupported) {
    
    // Get the IsochManager instance from the refcon
    auto manager = static_cast<IsochManager*>((*interface)->GetRefCon(interface));
    if (!manager) {
        return kIOReturnBadArgument;
    }
    
    // Use stored speed from configuration
    *outMaxSpeed = manager->configuredSpeed_;
    
    // Handle channel selection based on configuration
    uint32_t channel = manager->configuredChannel_;
    if (channel == kAnyAvailableIsochChannel) {
        // Allow FireWireFamily to determine an available channel
        // Enable all channels except 0 (which is reserved)
        *outChanSupported = ~1ULL;
    } else {
        // Use a specific channel - create a mask with only that bit set
        *outChanSupported = (((UInt64)0x80000000 << 32 | (UInt64)0x00000000) >> channel);
    }
    
    if (manager->logger_) {
        manager->logger_->debug("RemotePort_GetSupported: speed={}, channel={}",
                                static_cast<int>(*outMaxSpeed),
                                (channel == kAnyAvailableIsochChannel) ?
                                "any" : std::to_string(channel));
    }
    
    return kIOReturnSuccess;
}

IOReturn IsochManager::RemotePort_AllocatePort_Helper(
                                                      IOFireWireLibIsochPortRef interface,
                                                      IOFWSpeed maxSpeed,
                                                      UInt32 channel) {
    
    // Get the IsochManager instance from the refcon
    auto manager = static_cast<IsochManager*>((*interface)->GetRefCon(interface));
    if (!manager) {
        return kIOReturnBadArgument;
    }
    
    // Store the allocated channel for future reference
    manager->activeChannel_ = channel;
    
    if (manager->logger_) {
        manager->logger_->debug("RemotePort_AllocatePort: speed={}, channel={}",
                                static_cast<int>(maxSpeed), channel);
    }
    
    return kIOReturnSuccess;
}

IOReturn IsochManager::RemotePort_ReleasePort_Helper(
                                                     IOFireWireLibIsochPortRef interface) {
    
    // Get the IsochManager instance from the refcon
    auto manager = static_cast<IsochManager*>((*interface)->GetRefCon(interface));
    if (!manager) {
        return kIOReturnBadArgument;
    }
    
    // Reset active channel
    manager->activeChannel_ = kAnyAvailableIsochChannel;
    
    if (manager->logger_) {
        manager->logger_->debug("RemotePort_ReleasePort called");
    }
    
    return kIOReturnSuccess;
}

IOReturn IsochManager::RemotePort_Start_Helper(
                                               IOFireWireLibIsochPortRef interface) {
    
    // Get the IsochManager instance from the refcon
    auto manager = static_cast<IsochManager*>((*interface)->GetRefCon(interface));
    if (!manager) {
        return kIOReturnBadArgument;
    }
    
    // Set the running state
    manager->running_ = true;
    
    if (manager->logger_) {
        manager->logger_->debug("RemotePort_Start called");
    }
    
    return kIOReturnSuccess;
}

IOReturn IsochManager::RemotePort_Stop_Helper(
                                              IOFireWireLibIsochPortRef interface) {
    
    // Get the IsochManager instance from the refcon
    auto manager = static_cast<IsochManager*>((*interface)->GetRefCon(interface));
    if (!manager) {
        return kIOReturnBadArgument;
    }
    
    // Clear the running state
    manager->running_ = false;
    
    if (manager->logger_) {
        manager->logger_->debug("RemotePort_Stop called");
    }
    
    return kIOReturnSuccess;
}

IOReturn IsochManager::PortFinalize_Helper(void* refcon) {
    // Get the IsochManager instance from the refcon
    auto manager = static_cast<IsochManager*>(refcon);
    if (!manager) {
        return kIOReturnBadArgument;
    }
    
    // Handle port finalization
    manager->handlePortFinalize();
    
    return kIOReturnSuccess;
}



// Static callback handler
// TODO: make it static?
void IsochManager::DCLComplete_Helper(void* refcon, NuDCLRef dcl) {
    // This just forwards to the instance method, ignoring the dcl parameter
    auto manager = static_cast<IsochManager*>(refcon);
    if (manager) {
        manager->handleDCLComplete(nullptr); // Pass nullptr to indicate we don't care which DCL
    }
}

void IsochManager::DCLOverrun_Helper(void* refcon, NuDCLRef dcl) {
    // Get static logger for emergency debugging
    auto staticLogger = spdlog::default_logger();
    staticLogger->critical("OVERRUN_CALLBACK: ENTERED with refcon={:p}, dcl={:p}", refcon, (void*)dcl);
    
    // This is just for debugging now - no actual overrun handling yet
    auto manager = static_cast<IsochManager*>(refcon);
    if (!manager) {
        staticLogger->critical("OVERRUN_CALLBACK: NULL MANAGER REFCON!");
        return;
    }
    
    staticLogger->critical("OVERRUN_CALLBACK: Found manager at {:p}", (void*)manager);
    staticLogger->critical("OVERRUN_CALLBACK: COMPLETED");
}

// void IsochManager::DCLOverrun_Helper(void* refcon, NuDCLRef dcl) {
//     // Get the IsochManager instance from the refcon
//     auto manager = static_cast<IsochManager*>(refcon);
//     if (manager) {
//         manager->handleDCLOverrun();
//     }
// }

std::expected<uint32_t, IOKitError> IsochManager::getSegmentTimestamp(uint32_t segment) const {
    if (!initialized_ || !timestampBuffer_) {
        if (logger_) {
            logger_->error("IsochManager::getSegmentTimestamp: Not initialized or no timestamp buffer");
        }
        return std::unexpected(IOKitError::NotReady);
    }
    
    if (segment >= numSegments_) {
        if (logger_) {
            logger_->error("IsochManager::getSegmentTimestamp: Invalid segment {}", segment);
        }
        return std::unexpected(IOKitError::BadArgument);
    }
    
    return timestampBuffer_[segment];
}


void IsochManager::handleDCLComplete(NuDCLRef dcl) {
    // Get segment and timestamps without locks
    uint32_t segment = currentSegment_.load(std::memory_order_relaxed);
    uint32_t timestamp = timestampBuffer_ ? timestampBuffer_[segment] : 0;
    
    // CRITICAL: Do minimal local work first without callbacks
    logger_->debug("Processing segment {}", segment);
    
    // Calculate total segments and previous segment
    uint32_t numSegments = segments_.size();
    uint32_t prevSegment = (segment == 0) ? (numSegments - 1) : (segment - 1);
    
    // dclCompleteCallback_ is removed - beginning to simplify callback chain
    // if (dclCompleteCallback_) {
    //     try {
    //         // Call a minimal callback function that just logs
    //         dclCompleteCallback_(segment, dclCompleteRefCon_);
    //     } catch (const std::exception& e) {
    //         logger_->error("Exception in callback: {}", e.what());
    //     }
    // }

    // FIRST: Do minimal buffer processing
    processSegmentData(segment, timestamp);
    
    // SECOND: Update branch targets EXACTLY as original
    if (segments_.size() > 0 && nuDCLPool_ && overrunDCL_) {
        // Current segment's end DCL → overrun DCL
        (*nuDCLPool_)->SetDCLBranch(segments_[segment].endDCL, overrunDCL_);
        (*localPort_)->Notify(
            localPort_,
            kFWNuDCLModifyJumpNotification,
            (void**) &segments_[segment].endDCL,
            1);
        
        // Previous segment's end DCL → current segment's start DCL
        (*nuDCLPool_)->SetDCLBranch(
            segments_[prevSegment].endDCL,
            segments_[segment].startDCL);
        (*localPort_)->Notify(
            localPort_,
            kFWNuDCLModifyJumpNotification,
            (void**) &segments_[prevSegment].endDCL,
            1);
    }
    
    // THIRD: Update segment counter AFTER branch updates
    currentSegment_.store((segment + 1) % numSegments, std::memory_order_relaxed);
    

}

// Simple direct access to buffer data - revised
void IsochManager::processSegmentData(uint32_t segment, uint32_t timestamp) {
    // Skip if buffer is not initialized
    if (!baseBuffer_ || segment >= segments_.size()) {
        logger_->warn("Cannot process segment {}: invalid buffer state", segment);
        return;
    }
    
    // Get direct access to segment buffer
    uint8_t* bufferBase = baseBuffer_ + (segment * cyclesPerSegment_ * cycleBufferSize_);
    
    // Log the buffer pointer but don't do complex processing here
    logger_->debug("Segment {} buffer at {:p}, timestamp: {}", 
                 segment, (void*)bufferBase, timestamp);
    
    // Store that this was the last segment processed
    processedSegments_.store(segment, std::memory_order_release);
    processedTimestamps_.store(timestamp, std::memory_order_release);
    
    // Set hasData flag based on simple check
    // For example, see if first quadlet indicates valid data
    bool hasData = false;
    if (bufferBase) {
        uint32_t* firstQuadlet = reinterpret_cast<uint32_t*>(bufferBase);
        uint32_t payloadLength = (*firstQuadlet & 0xFFFF0000) >> 16;
        hasData = (payloadLength > 0);
        
        // Log if we found data
        if (hasData) {
            logger_->debug("Segment {} has data: payload length={}", segment, payloadLength);
        }
    }
    
    // Track if we've seen data
    hasReceivedData_.store(hasData, std::memory_order_release);
}




void IsochManager::handleDCLOverrun() {
    if (logger_) {
        logger_->warn("IsochManager::handleDCLOverrun: DCL overrun detected");
    }
    
    // Notify callback with proper refcon
    if (dclOverrunCallback_) {
        dclOverrunCallback_(dclOverrunRefCon_);
    }
}

void IsochManager::handlePortFinalize() {
    if (logger_) {
        logger_->debug("IsochManager::handlePortFinalize: Port finalize called");
    }
    
    finalized_ = true;
}

} // namespace Isoch
} // namespace FWA



