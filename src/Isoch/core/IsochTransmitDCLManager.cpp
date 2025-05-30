#include "Isoch/core/IsochTransmitDCLManager.hpp"
#include "Isoch/interfaces/ITransmitBufferManager.hpp" // Full definition needed now
#include <vector>
#include <stdexcept> // For potential exceptions if needed
#include <cstring> // For bzero
#include <IOKit/firewire/IOFireWireFamilyCommon.h>
#include <IOKit/firewire/IOFireWireLibIsoch.h>
#include <IOKit/firewire/IOFireWireLib.h>   

namespace FWA {
namespace Isoch {

IsochTransmitDCLManager::IsochTransmitDCLManager(std::shared_ptr<spdlog::logger> logger)
 : logger_(std::move(logger)) {
    if (logger_) logger_->debug("IsochTransmitDCLManager created");
}

IsochTransmitDCLManager::~IsochTransmitDCLManager() {
    reset();
    if (logger_) logger_->debug("IsochTransmitDCLManager destroyed");
}

void IsochTransmitDCLManager::reset() {
     std::lock_guard<std::mutex> lock(mutex_);
     // DCLs are owned by the pool, just clear refs
     dclProgramRefs_.clear();
     callbackInfos_.clear();
      for (CFMutableSetRef bag : updateBags_) {
         if (bag) CFRelease(bag);
     }
     updateBags_.clear();
     firstDCLRef_ = nullptr;
     lastDCLRef_ = nullptr;
     overrunDCL_ = nullptr;
     nuDCLPool_ = nullptr; // Clear non-owning ref
     dclProgramCreated_ = false;
      if (logger_) logger_->debug("IsochTransmitDCLManager reset");
}

// Basic implementations - returning errors or default values
std::expected<DCLCommand*, IOKitError> IsochTransmitDCLManager::createDCLProgram(
    const TransmitterConfig& config,
    IOFireWireLibNuDCLPoolRef nuDCLPool,
    const ITransmitBufferManager& bufferManager) // Use const ref to Buffer Manager
{
     std::lock_guard<std::mutex> lock(mutex_);
     if (dclProgramCreated_) {
         logger_->warn("createDCLProgram called when already created.");
         return std::unexpected(IOKitError::Busy);
     }
     if (!nuDCLPool) {
          logger_->error("createDCLProgram: NuDCL pool is null.");
         return std::unexpected(IOKitError::BadArgument);
     }

     logger_->info("IsochTransmitDCLManager::createDCLProgram starting...");

     // Store config and pool reference
     config_ = config;
     nuDCLPool_ = nuDCLPool; // Store non-owning ref

     // --- Basic Input Validation ---
     if (config_.numGroups == 0 || config_.packetsPerGroup == 0) {
         logger_->error("createDCLProgram: numGroups or packetsPerGroup is zero.");
         return std::unexpected(IOKitError::BadArgument);
     }
     uint32_t totalPackets = config_.numGroups * config_.packetsPerGroup;
     logger_->debug("  Total packets to create: {}", totalPackets);

     // --- Prepare Internal Structures ---
     try {
        dclProgramRefs_.resize(totalPackets);
        callbackInfos_.resize(config_.numGroups);
        // updateBags_.resize(config_.numGroups); // Not using update bags for now
     } catch (const std::bad_alloc& e) {
         logger_->error("Failed to allocate internal DCL storage: {}", e.what());
         reset();
         return std::unexpected(IOKitError::NoMemory);
     }

     NuDCLSendPacketRef previousDCL = nullptr;

     // --- DCL Allocation Loop ---
     for (uint32_t g = 0; g < config_.numGroups; ++g) {
         // Prepare callback info for this group (even if callback isn't set on all DCLs)
         callbackInfos_[g].manager = this;
         callbackInfos_[g].groupIndex = g;
         // updateBags_[g] = CFSetCreateMutable(nullptr, 0, nullptr); // If using update bags

         for (uint32_t p = 0; p < config_.packetsPerGroup; ++p) {
             uint32_t globalPacketIdx = g * config_.packetsPerGroup + p;

             // --- 1. Get Buffer Pointers for this Packet ---
             auto isochHdrPtrExp = bufferManager.getPacketIsochHeaderPtr(g, p);
             auto cipHdrPtrExp = bufferManager.getPacketCIPHeaderPtr(g, p);
             // For Send DCL, the *source* of audio data is the client buffer area
             uint8_t* audioDataPtr = bufferManager.getClientAudioBufferPtr()
                                   + (globalPacketIdx * bufferManager.getAudioPayloadSizePerPacket()) % bufferManager.getClientAudioBufferSize(); // Cycle through client buffer
             auto timestampPtrExp = bufferManager.getGroupTimestampPtr(g);

             if (!isochHdrPtrExp || !cipHdrPtrExp || !timestampPtrExp || !audioDataPtr) {
                 logger_->error("Failed to get buffer pointers for G={}, P={}", g, p);
                 reset(); // Cleanup partially created DCLs
                 return std::unexpected(IOKitError::NoMemory); // Or more specific error
             }
             uint8_t* isochHdrTemplatePtr = isochHdrPtrExp.value();
             uint8_t* cipHdrPtr = cipHdrPtrExp.value();
             uint32_t* timestampPtr = timestampPtrExp.value();
             size_t audioPayloadSize = bufferManager.getAudioPayloadSizePerPacket();

             // --- 2. Prepare Initial Memory Content (Optional but good practice) ---
             // Isoch Header Template (Minimal: Tag=1, TCode=A. Channel set later?)
             IsochHeaderData* isochHdr = reinterpret_cast<IsochHeaderData*>(isochHdrTemplatePtr);
             isochHdr->data_length = 0; // Hardware fills this
             isochHdr->tag_channel = (1 << 6) | (config_.initialChannel & 0x3F); // Tag=1
             isochHdr->tcode_sy = (0xA << 4) | 0; // TCode=A, Sy=0

             // CIP Header (Initial safe state: NO_DATA)
             CIPHeader* cipHdr = reinterpret_cast<CIPHeader*>(cipHdrPtr);
             bzero(cipHdr, kTransmitCIPHeaderSize);
             cipHdr->fmt_eoh1 = (0x10 << 2) | 0x01; // FMT=0x10 (AM824), EOH=1
             cipHdr->fdf = 0xFF; // NO_DATA
             cipHdr->syt = OSSwapHostToBigInt16(0xFFFF); // NO_INFO

             // Audio Payload (Zero it out initially)
             // bzero(audioDataPtr, audioPayloadSize); // Provider should handle initial silence

             // --- 3. Prepare IOVirtualRange Array ---
             IOVirtualRange ranges[2];
             uint32_t numRanges = 2; // Assume CIP + Payload initially

             // Range 0: CIP Header
             ranges[0].address = reinterpret_cast<IOVirtualAddress>(cipHdrPtr);
             ranges[0].length = kTransmitCIPHeaderSize;

             // Range 1: Audio Payload
             ranges[1].address = reinterpret_cast<IOVirtualAddress>(audioDataPtr);
             ranges[1].length = audioPayloadSize;

             // --- 4. Allocate Send Packet DCL ---
             NuDCLSendPacketRef currentDCL = (*nuDCLPool_)->AllocateSendPacket(
                 nuDCLPool_,
                 nullptr, // updateBag - not using for now
                 numRanges,
                 (::IOVirtualRange*)ranges // Cast to system type
             );

             if (!currentDCL) {
                 logger_->error("Failed to allocate NuDCLSendPacketRef for G={}, P={}", g, p);
                 reset();
                 return std::unexpected(IOKitError::NoMemory);
             }
             dclProgramRefs_[globalPacketIdx] = currentDCL; // Store the reference

             // --- 5. Configure the Allocated DCL ---
             UInt32 dclFlags = kNuDCLDynamic |         // DCL might change (ranges, branch)
                                kNuDCLUpdateBeforeCallback; // Update status/TS before callback

             // Set Isoch Header Template Pointer
             (*nuDCLPool_)->SetDCLUserHeaderPtr(currentDCL, reinterpret_cast<UInt32*>(isochHdrTemplatePtr), nullptr); // Mask is null

             // Set Timestamp Pointer (where hardware WRITES completion time)
             (*nuDCLPool_)->SetDCLTimeStampPtr(currentDCL, timestampPtr);
             // NOTE: Setting a non-NULL timestamp pointer might automatically set kFWNuDCLFlag_TimeStamp

             // Set Callback (Conditional)
             bool isLastPacketInGroup = (p == config_.packetsPerGroup - 1);
             bool isCallbackGroup = ((g + 1) % config_.callbackGroupInterval == 0);

             if (isLastPacketInGroup && isCallbackGroup) {
                 (*nuDCLPool_)->SetDCLCallback(currentDCL, DCLComplete_Helper);
                 (*nuDCLPool_)->SetDCLRefcon(currentDCL, &callbackInfos_[g]); // Pass group info struct addr
                 logger_->trace("  Set callback for G={}, P={} (DCL {:p})", g, p, (void*)currentDCL);
             } else {
                 (*nuDCLPool_)->SetDCLRefcon(currentDCL, this); // Default refcon
             }

             // Set Final Flags
             (*nuDCLPool_)->SetDCLFlags(currentDCL, dclFlags);

             // --- 6. Link Previous DCL to Current ---
             if (previousDCL) {
                 (*nuDCLPool_)->SetDCLBranch(previousDCL, currentDCL);
             }

             // --- 7. Store First/Update Previous ---
             if (globalPacketIdx == 0) {
                 firstDCLRef_ = currentDCL;
                 logger_->debug("  Stored first DCL: {:p}", (void*)firstDCLRef_);
             }
             previousDCL = currentDCL;

         } // End packet loop (p)
     } // End group loop (g)

     // --- 8. Store Last DCL ---
     lastDCLRef_ = previousDCL; // Should be the last one allocated
     if(lastDCLRef_) logger_->debug("  Stored last DCL: {:p}", (void*)lastDCLRef_); else logger_->error("  Last DCL is NULL after loop!");


     // --- 9. Get Program Handle ---
     // The handle is needed for CreateLocalIsochPort
     DCLCommand* programHandle = (*nuDCLPool_)->GetProgram(nuDCLPool_);
     if (!programHandle) {
        logger_->error("GetProgram returned null after creating DCLs");
        reset();
        return std::unexpected(IOKitError::Error);
    }

     dclProgramCreated_ = true;
     logger_->info("IsochTransmitDCLManager::createDCLProgram finished successfully.");
     return programHandle; // Return the system-compatible handle
}

std::expected<void, IOKitError> IsochTransmitDCLManager::fixupDCLJumpTargets(
    IOFireWireLibLocalIsochPortRef localPort)
{
    std::lock_guard<std::mutex> lock(mutex_); // Ensure thread safety

    // 1. Check Preconditions
    if (!dclProgramCreated_) {
        logger_->error("fixupDCLJumpTargets: DCL program not created yet");
        return std::unexpected(IOKitError::NotReady);
    }
    if (!nuDCLPool_) {
        logger_->error("fixupDCLJumpTargets: nuDCLPool_ is null");
        return std::unexpected(IOKitError::NotReady);
    }
    if (!firstDCLRef_) {
        logger_->error("fixupDCLJumpTargets: firstDCLRef_ is null");
        return std::unexpected(IOKitError::NotReady);
    }
    if (!lastDCLRef_) {
        logger_->error("fixupDCLJumpTargets: lastDCLRef_ is null");
        return std::unexpected(IOKitError::NotReady);
    }
    if (!localPort) {
        logger_->error("fixupDCLJumpTargets: localPort is null");
        return std::unexpected(IOKitError::BadArgument);
    }

    // 2. Link Last DCL to First DCL
    logger_->debug("Linking last DCL ({:p}) to first DCL ({:p})", (void*)lastDCLRef_, (void*)firstDCLRef_);
    IOReturn result = (*nuDCLPool_)->SetDCLBranch(lastDCLRef_, firstDCLRef_);
    if (result != kIOReturnSuccess) {
        logger_->error("fixupDCLJumpTargets: SetDCLBranch failed with error: 0x{:08X}", result);
        return std::unexpected(IOKitError(result));
    }

    // 3. Notify the Port using the helper function
    logger_->debug("Notifying port about jump update for last DCL ({:p})", (void*)lastDCLRef_);
    NuDCLRef* dclRefPtr = &lastDCLRef_; // Get address of the variable
    result = notifyJumpUpdate(localPort, dclRefPtr); // Call the helper
    if (result != kIOReturnSuccess) {
        logger_->error("fixupDCLJumpTargets: notifyJumpUpdate failed with error: 0x{:08X}", result);
        return std::unexpected(IOKitError(result));
    } else {
        logger_->debug("Successfully notified port about last DCL jump update.");
    }

    logger_->info("IsochTransmitDCLManager::fixupDCLJumpTargets completed successfully.");
    return {};
}

void IsochTransmitDCLManager::setDCLCompleteCallback(TransmitDCLCompleteCallback callback, void* refCon) {
    dclCompleteCallback_ = callback;
    dclCompleteRefCon_ = refCon;
}

void IsochTransmitDCLManager::setDCLOverrunCallback(TransmitDCLOverrunCallback callback, void* refCon) {
    dclOverrunCallback_ = callback;
    dclOverrunRefCon_ = refCon;
}

std::expected<void, IOKitError> IsochTransmitDCLManager::updateDCLPacket(
    uint32_t groupIndex, uint32_t packetIndexInGroup,
    const IOVirtualRange ranges[], uint32_t numRanges,
    const IsochHeaderData* isochHeaderTemplate)
{
     // Remove the lock - DCL access is safe at the driver level
     if (!dclProgramCreated_) return std::unexpected(IOKitError::NotReady);
     if (!nuDCLPool_) return std::unexpected(IOKitError::NotReady); // Should not happen

     NuDCLSendPacketRef dclRef = getDCLRef(groupIndex, packetIndexInGroup);
     if (!dclRef) {
          logger_->error("updateDCLPacket: Could not get DCL Ref for G={}, P={}", groupIndex, packetIndexInGroup);
         return std::unexpected(IOKitError::BadArgument);
     }

     // Update the ranges (data source pointers and lengths)
     // This is the most common update needed.
     IOReturn result = (*nuDCLPool_)->SetDCLRanges(dclRef, numRanges, (::IOVirtualRange*)ranges);
     if (result != kIOReturnSuccess) {
         logger_->error("SetDCLRanges failed for G={}, P={}: 0x{:08X}", groupIndex, packetIndexInGroup, result);
         return std::unexpected(IOKitError(result));
     }
     
     // Update Isoch Header Template Content (if template itself changes, rarely needed)
     if (isochHeaderTemplate) {
          // Note: This requires that the caller has already obtained the correct
          // template pointer location for this DCL's isoch header.
          // The proper way to handle this would be:
          // 1. Get the current header pointer via GetDCLUserHeaderPtr from NuDCLPool
          // 2. Modify its contents
          // Here we just log that this should happen via buffer manager
          logger_->trace("isochHeaderTemplate updates should be handled via buffer manager directly");
     }

     return {};
}

std::expected<void, IOKitError> IsochTransmitDCLManager::notifySegmentUpdate(
     IOFireWireLibLocalIsochPortRef localPort, uint32_t groupIndexToNotify)
{
     // std::lock_guard<std::mutex> lock(mutex_); // May not need lock if dclProgramRefs_ isn't modified
     if (!dclProgramCreated_) return std::unexpected(IOKitError::NotReady);
     if (!localPort) return std::unexpected(IOKitError::BadArgument);
     if (groupIndexToNotify >= config_.numGroups) return std::unexpected(IOKitError::BadArgument);

     // Prepare the list of DCLs in the segment to notify
     uint32_t startIndex = groupIndexToNotify * config_.packetsPerGroup;
     uint32_t count = config_.packetsPerGroup;
     std::vector<NuDCLRef> dclsInSegment;
     dclsInSegment.reserve(count);
     for (uint32_t i = 0; i < count; ++i) {
          if (startIndex + i < dclProgramRefs_.size()) {
              dclsInSegment.push_back(dclProgramRefs_[startIndex + i]);
          } else {
               logger_->error("notifySegmentUpdate: Calculated index out of bounds!");
               return std::unexpected(IOKitError::BadArgument); // Should not happen
          }
     }

     if (dclsInSegment.empty()) {
          logger_->warn("notifySegmentUpdate: No DCLs found for group {}", groupIndexToNotify);
          return {}; // Not an error, but nothing to do
     }

     // Call the helper to notify
     IOReturn result = notifyDCLUpdates(localPort, dclsInSegment.data(), dclsInSegment.size());
     if (result != kIOReturnSuccess) {
          logger_->error("notifyDCLUpdates failed for group {}: 0x{:08X}", groupIndexToNotify, result);
          return std::unexpected(IOKitError(result));
     }
     // logger_->trace("Notified DCL updates for group {}", groupIndexToNotify);
     return {};
}

// updateJumpTargets method removed - we now use a static circular DCL program
// The fixupDCLJumpTargets method handles the circular loop setup during initialization


DCLCommand* IsochTransmitDCLManager::getProgramHandle() const {
    if (!dclProgramCreated_ || !nuDCLPool_) return nullptr;
    return (*nuDCLPool_)->GetProgram(nuDCLPool_);
}

// --- Static Callbacks & Instance Handlers ---
void IsochTransmitDCLManager::DCLComplete_Helper(void* refcon, NuDCLRef dcl) {
     auto* info = static_cast<DCLCallbackInfo*>(refcon);
     if (info && info->manager && info->manager->dclCompleteCallback_) {
          info->manager->dclCompleteCallback_(info->groupIndex, info->manager->dclCompleteRefCon_);
     }
}

void IsochTransmitDCLManager::DCLOverrun_Helper(void* refcon, NuDCLRef dcl) {
    auto* manager = static_cast<IsochTransmitDCLManager*>(refcon);
     if (manager && manager->dclOverrunCallback_) {
         manager->dclOverrunCallback_(manager->dclOverrunRefCon_);
     }
}

void IsochTransmitDCLManager::handleDCLComplete(uint32_t groupIndex, NuDCLRef dcl) {
     // This instance method could do internal state updates if needed before calling client
     if (dclCompleteCallback_) {
        dclCompleteCallback_(groupIndex, dclCompleteRefCon_);
    }
}
void IsochTransmitDCLManager::handleDCLOverrun(NuDCLRef dcl) {
     // This instance method could do internal state updates if needed before calling client
     if (dclOverrunCallback_) {
        dclOverrunCallback_(dclOverrunRefCon_);
    }
}

// --- Private Helpers ---
// Remove const qualifier to match header declaration
NuDCLSendPacketRef IsochTransmitDCLManager::getDCLRef(uint32_t g, uint32_t p) {
     // Remove the lock - read access is safe once program is created
     if (!dclProgramCreated_ || dclProgramRefs_.empty()) return nullptr;
     uint32_t index = g * config_.packetsPerGroup + p;
     if (index >= dclProgramRefs_.size()) return nullptr;
     return dclProgramRefs_[index];
}

IOReturn IsochTransmitDCLManager::notifyDCLUpdates(IOFireWireLibLocalIsochPortRef localPort, NuDCLRef dcls[], uint32_t count) {
     if (!localPort || !dcls || count == 0) return kIOReturnBadArgument;
     
     // Need to pass array of DCLRefs cast to void*
     std::vector<void*> dclPtrList(count);
     for(uint32_t i = 0; i < count; ++i) {
         // --- FIX: Cast the DCLRef *itself* to void*, not its address ---
         dclPtrList[i] = static_cast<void*>(dcls[i]);    // Correct
         
         // Add a check for NULL DCLs just in case
         if (dclPtrList[i] == nullptr) {
             logger_->error("notifyDCLUpdates: Encountered NULL DCLRef at index {}", i);
             return kIOReturnBadArgument; // Safer to return error
         }
     }

     // Call Notify with the array of DCLRefs cast to void*
     return (*localPort)->Notify(
         localPort,
         kFWNuDCLModifyNotification,  // For content/range updates
         dclPtrList.data(),           // Array of pointers to DCLRefs
         count);
}
IOReturn IsochTransmitDCLManager::notifyJumpUpdate(IOFireWireLibLocalIsochPortRef localPort, NuDCLRef* dclRefPtr) {
     if (!localPort || !dclRefPtr || !*dclRefPtr) return kIOReturnBadArgument;
     return (*localPort)->Notify(localPort, kFWNuDCLModifyJumpNotification, (void**)dclRefPtr, 1);
}


} // namespace Isoch
} // namespace FWA