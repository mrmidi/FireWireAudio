#include "Isoch/core/IsochDCLManager.hpp"
#include "Isoch/core/IsochBufferManager.hpp"
#include <spdlog/spdlog.h>
#include <IOKit/IOKitLib.h> // For kIOReturnSuccess etc.

namespace FWA {
namespace Isoch {

// Constants from Buffer Manager
constexpr size_t kIsochHeaderSize = IsochBufferManager::kIsochHeaderSize;
constexpr size_t kCIPHeaderSize = IsochBufferManager::kCIPHeaderSize;

IsochDCLManager::IsochDCLManager(
    std::shared_ptr<spdlog::logger> logger,
    IOFireWireLibNuDCLPoolRef nuDCLPool,
    const IsochBufferManager& bufferManager, // Take const ref
    const Config& config)
    : logger_(std::move(logger))
    , nuDCLPool_(nuDCLPool)
    , bufferManager_(bufferManager) // Store const ref
    , config_(config)
    , totalPackets_(config.numGroups * config.packetsPerGroup)
{
    if (logger_) logger_->debug("IsochDCLManager created (Kernel Style)");
}

IsochDCLManager::~IsochDCLManager() {
    reset();
    if (logger_) logger_->debug("IsochDCLManager destroyed");
}

void IsochDCLManager::reset() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    // NuDCLPool commands are freed when the pool is released (externally)
    groupInfos_.clear(); // Clear metadata vector
    firstDCLRef_ = nullptr;
    lastDCLRef_ = nullptr;
    dclProgramCreated_ = false;
    if (logger_) logger_->debug("IsochDCLManager reset completed");
}

std::expected<DCLCommand*, IOKitError> IsochDCLManager::createDCLProgram() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (dclProgramCreated_) return std::unexpected(IOKitError::Busy);
    if (!nuDCLPool_) return std::unexpected(IOKitError::NotReady);

    const uint32_t numGroups = config_.numGroups;
    const uint32_t packetsPerGroup = config_.packetsPerGroup;
    const uint32_t packetDataSize = bufferManager_.getPacketDataSize();
    const uint32_t callbackInterval = config_.callbackGroupInterval > 0 ? config_.callbackGroupInterval : 1; // Ensure > 0

    if (logger_) {
        logger_->info("IsochDCLManager::createDCLProgram (Kernel Style):");
        logger_->info("  NumGroups={}, PacketsPerGroup={}, PacketDataSize={}, CallbackInterval={}",
                      numGroups, packetsPerGroup, packetDataSize, callbackInterval);
    }

    if (numGroups == 0 || packetsPerGroup == 0) {
         return std::unexpected(IOKitError::BadArgument);
    }

    groupInfos_.resize(numGroups); // Allocate space for group metadata
    NuDCLRef previousDCL = nullptr;

    // --- Loop through groups and packets ---
    for (uint32_t groupIdx = 0; groupIdx < numGroups; ++groupIdx) {
        // Set up metadata for this group
        groupInfos_[groupIdx].manager = this;
        groupInfos_[groupIdx].groupIndex = groupIdx;

        for (uint32_t packetIdx = 0; packetIdx < packetsPerGroup; ++packetIdx) {
            uint32_t globalPacketIdx = groupIdx * packetsPerGroup + packetIdx;

            // Get pointers for this packet's sections from BufferManager
            auto isochHdrPtrExp = bufferManager_.getPacketIsochHeaderPtr(groupIdx, packetIdx);
            auto cipHdrPtrExp = bufferManager_.getPacketCIPHeaderPtr(groupIdx, packetIdx);
            auto dataPtrExp = bufferManager_.getPacketDataPtr(groupIdx, packetIdx);
            auto tsPtrExp = bufferManager_.getPacketTimestampPtr(groupIdx, packetIdx);

            if (!isochHdrPtrExp || !cipHdrPtrExp || !dataPtrExp || !tsPtrExp) {
                 if (logger_) logger_->error("Failed to get buffer pointers for packet G:{} P:{}", groupIdx, packetIdx);
                 reset();
                 return std::unexpected(IOKitError::NoMemory); // Or appropriate error
            }

            // Prepare the 3 ranges for AllocateReceivePacket
            IOVirtualRange ranges[3];
            // Range 0: Isoch Header
            ranges[0].address = reinterpret_cast<IOVirtualAddress>(isochHdrPtrExp.value());
            ranges[0].length = kIsochHeaderSize;
            // Range 1: CIP Header
            ranges[1].address = reinterpret_cast<IOVirtualAddress>(cipHdrPtrExp.value());
            ranges[1].length = kCIPHeaderSize;
            // Range 2: Packet Data
            ranges[2].address = reinterpret_cast<IOVirtualAddress>(dataPtrExp.value());
            ranges[2].length = packetDataSize;

            // Log detailed buffer information
            if (logger_ && logger_->should_log(spdlog::level::trace)) {
                logger_->trace("createDCLProgram G:{} P:{} - Packet buffer ranges:", groupIdx, packetIdx);
                logger_->trace("  IsochHdr: Addr={:p}, Len={}", (void*)ranges[0].address, ranges[0].length);
                logger_->trace("  CIP Hdr:  Addr={:p}, Len={}", (void*)ranges[1].address, ranges[1].length);
                logger_->trace("  Data:     Addr={:p}, Len={}", (void*)ranges[2].address, ranges[2].length);
                logger_->trace("  TimestampPtr: {:p}", (void*)tsPtrExp.value());
                
                // Verify buffer alignment and proximity
                logger_->trace("  Buffer proximity checks:");
                logger_->trace("    IsochHdr→CIPHdr distance: {}", 
                             (uint8_t*)ranges[1].address - (uint8_t*)ranges[0].address);
                logger_->trace("    CIPHdr→Data distance: {}", 
                             (uint8_t*)ranges[2].address - (uint8_t*)ranges[1].address);
                
                // Check for expected layout (should be sequential within each packet's data area)
                bool sequential = ((uint8_t*)ranges[1].address == (uint8_t*)ranges[0].address + ranges[0].length) &&
                                 ((uint8_t*)ranges[2].address == (uint8_t*)ranges[1].address + ranges[1].length);
                if (!sequential) {
                    logger_->warn("  Buffer layout is NOT sequential within packet - unexpected arrangement!");
                }
            }
            
            // Allocate the DCL for this packet
            NuDCLRef currentDCL = (*nuDCLPool_)->AllocateReceivePacket(
                nuDCLPool_,
                nullptr, // Update bag not used in this simpler model
                4,       // Header size
                3,       // Number of ranges
                (::IOVirtualRange*)ranges); // Cast to system type

            if (!currentDCL) {
                if (logger_) logger_->error("Failed to allocate DCL for packet G:{} P:{}", groupIdx, packetIdx);
                reset(); // Clean up allocated DCLs
                return std::unexpected(IOKitError::NoMemory);
            }

            // Set timestamp pointer for *this* DCL
            (*nuDCLPool_)->SetDCLTimeStampPtr(currentDCL, tsPtrExp.value());

            // Set common flags and refcon (refcon could point to packet info if needed)
            (*nuDCLPool_)->SetDCLFlags(currentDCL, kNuDCLDynamic | kNuDCLUpdateBeforeCallback);
            (*nuDCLPool_)->SetDCLRefcon(currentDCL, this); // Could refine refcon later

            // Link previous DCL to this one
            if (previousDCL) {
                (*nuDCLPool_)->SetDCLBranch(previousDCL, currentDCL);
            }

            // Store the very first DCL
            if (groupIdx == 0 && packetIdx == 0) {
                firstDCLRef_ = currentDCL;
            }

            // Check if this is the last packet of a callback group
            if (packetIdx == packetsPerGroup - 1 && ((groupIdx + 1) % callbackInterval == 0)) {
                if (logger_) logger_->debug("Setting callback for Group: {}, Last Packet DCL: {:p}", groupIdx, (void*)currentDCL);
                (*nuDCLPool_)->SetDCLCallback(currentDCL, DCLComplete_Helper);
                // Set refcon specifically for the callback DCL to point to its group info
                (*nuDCLPool_)->SetDCLRefcon(currentDCL, &groupInfos_[groupIdx]);
            }

            previousDCL = currentDCL; // Update previous DCL for next iteration
        } // End packet loop
    } // End group loop

    // Store the very last DCL created
    lastDCLRef_ = previousDCL;

    // Don't fixup jumps here, do it separately after local port is known
    dclProgramCreated_ = true;

    // Get the program handle recognized by the system
    DCLCommand* programHandle = (*nuDCLPool_)->GetProgram(nuDCLPool_);
     if (!programHandle) {
        if (logger_) logger_->error("IsochDCLManager::createDCLProgram: GetProgram returned null after creating DCLs");
        reset();
        return std::unexpected(IOKitError::Error);
    }

    if (logger_) logger_->info("IsochDCLManager::createDCLProgram successful. FirstDCL={:p}, LastDCL={:p}", (void*)firstDCLRef_, (void*)lastDCLRef_);
    return programHandle; // Return handle from GetProgram
}

std::expected<void, IOKitError> IsochDCLManager::fixupDCLJumpTargets(IOFireWireLibLocalIsochPortRef localPort) {
     std::lock_guard<std::mutex> lock(stateMutex_);
    if (!dclProgramCreated_ || !firstDCLRef_ || !lastDCLRef_) {
         if (logger_) logger_->error("IsochDCLManager::fixupDCLJumpTargets: DCL program not fully created.");
         return std::unexpected(IOKitError::NotReady);
    }
     if (!localPort) {
         if (logger_) logger_->error("IsochDCLManager::fixupDCLJumpTargets: Local port is null.");
         return std::unexpected(IOKitError::BadArgument);
     }

    // Make the program circular: last DCL branches to first DCL
    (*nuDCLPool_)->SetDCLBranch(lastDCLRef_, firstDCLRef_);
    if (logger_) logger_->info("IsochDCLManager::fixupDCLJumpTargets: Set branch LastDCL ({:p}) -> FirstDCL ({:p})", (void*)lastDCLRef_, (void*)firstDCLRef_);

    // Notify the port about the jump update on the *last* DCL
    IOReturn result = notifyJumpUpdate(localPort, &lastDCLRef_); // Pass address of the variable storing the ref
    if (result != kIOReturnSuccess) {
        // Log error but don't fail the whole operation
        logger_->error("IsochDCLManager::fixupDCLJumpTargets: Notify failed for last DCL jump: 0x{:08X}", result);
    } else {
        logger_->debug("IsochDCLManager::fixupDCLJumpTargets: Notify successful for last DCL jump");
    }

    // No explicit overrun DCL in this model, so no separate jump for it.

    currentSegment_ = 0; // Reset conceptual 'segment' counter (might rename this)
    logger_->info("IsochDCLManager::fixupDCLJumpTargets successful.");
    return {};
}

IOReturn IsochDCLManager::notifyJumpUpdate(IOFireWireLibLocalIsochPortRef localPort, NuDCLRef* dclRefPtr) {
    // Helper to notify port about jump changes
     if (!localPort || !dclRefPtr || !*dclRefPtr) {
        return kIOReturnBadArgument;
    }
    // Pass the address of the NuDCLRef variable
    return (*localPort)->Notify(
        localPort,
        kFWNuDCLModifyJumpNotification,
        (void**)dclRefPtr, // Pass address of the variable holding the NuDCLRef
        1);
}


std::expected<DCLCommandPtr, IOKitError> IsochDCLManager::getProgram() const {
    // GetProgram from NuDCLPool should return the DCLCommand* compatible handle
     if (!nuDCLPool_) return std::unexpected(IOKitError::NotReady);
     DCLCommand* program = (*nuDCLPool_)->GetProgram(nuDCLPool_);
     if (!program) return std::unexpected(IOKitError::Error);
     return reinterpret_cast<DCLCommandPtr>(program); // Cast to DCLCommandPtr
}

// --- Callbacks ---
void IsochDCLManager::setDCLCompleteCallback(DCLCompleteCallback callback, void* refCon) {
    dclCompleteCallback_ = callback;
    dclCompleteRefCon_ = refCon;
}

void IsochDCLManager::setDCLOverrunCallback(DCLOverrunCallback callback, void* refCon) {
    dclOverrunCallback_ = callback;
    dclOverrunRefCon_ = refCon;
}

void IsochDCLManager::handleDCLComplete(NuDCLRef dcl, BufferGroupInfo* groupInfo) {
    if (!groupInfo) {
         if(logger_) logger_->error("handleDCLComplete: Received NULL groupInfo!");
         return;
    }

    // if (logger_) logger_->debug("handleDCLComplete: Group {} completed (DCL: {:p})", groupInfo->groupIndex, (void*)dcl);

    // Call the client's callback
    if (dclCompleteCallback_) {
        // The client callback expects a single 'groupIndex' index. We provide the group index.
        dclCompleteCallback_(groupInfo->groupIndex, dclCompleteRefCon_);
    }
}

void IsochDCLManager::handleDCLOverrun(NuDCLRef dcl) {
     if (logger_) logger_->warn("handleDCLOverrun: DCL overrun detected (DCL: {:p})", (void*)dcl);
    if (dclOverrunCallback_) {
        dclOverrunCallback_(dclOverrunRefCon_);
    }
}

// Static Helpers
void IsochDCLManager::DCLComplete_Helper(void* refcon, NuDCLRef dcl) {
    // refcon here is BufferGroupInfo*
    auto groupInfo = static_cast<BufferGroupInfo*>(refcon);
    if (groupInfo && groupInfo->manager) {
        groupInfo->manager->handleDCLComplete(dcl, groupInfo);
    } else if (auto logger = spdlog::default_logger()) { // Fallback logger
         logger->error("DCLComplete_Helper: Invalid refcon or manager pointer!");
    }
}

void IsochDCLManager::DCLOverrun_Helper(void* refcon, NuDCLRef dcl) {
     // refcon here is IsochDCLManager*
     auto manager = static_cast<IsochDCLManager*>(refcon);
     if (manager) {
         manager->handleDCLOverrun(dcl);
     } else if (auto logger = spdlog::default_logger()) {
         logger->error("DCLOverrun_Helper: Invalid manager refcon!");
     }
}

} // namespace Isoch
} // namespace FWA
