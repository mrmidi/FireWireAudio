#include "Isoch/core/IsochTransmitDCLManager.hpp"
#include "Isoch/core/CIPHeader.hpp" // Include for CIPHeader definition
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
    const ITransmitBufferManager& bufferManager)
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

    logger_->info("IsochTransmitDCLManager: Creating DCL program with configuration: {}", config.configSummary());

    if (!config.isValid()) {
        logger_->error("IsochTransmitDCLManager: Received an invalid configuration. Aborting DCL program creation.");
        return std::unexpected(IOKitError::BadArgument);
    }

    // Store config and pool reference
    config_ = config;
    nuDCLPool_ = nuDCLPool;

    uint32_t callbackGroupInterval = config_.callbackGroupInterval; // Use from stored config_

    // Basic Input Validation already present, now using config_
    if (config_.numGroups == 0 || config_.packetsPerGroup == 0) {
        logger_->error("createDCLProgram: numGroups or packetsPerGroup is zero in stored config.");
        return std::unexpected(IOKitError::BadArgument);
    }
    
    if (callbackGroupInterval > config_.numGroups) {
        logger_->error("createDCLProgram: callbackGroupInterval ({}) > numGroups ({}) in stored config.", 
                      callbackGroupInterval, config_.numGroups);
        return std::unexpected(IOKitError::BadArgument);
    }

    uint32_t totalPackets = config_.numGroups * config_.packetsPerGroup;
    logger_->debug("Total packets to create: {}", totalPackets);

    // Prepare Internal Structures
    try {
        dclProgramRefs_.resize(totalPackets);
        // CRITICAL FIX: Reserve callbackInfos_ to prevent pointer invalidation
        callbackInfos_.clear();
        callbackInfos_.reserve(config_.numGroups);
        for (uint32_t g = 0; g < config_.numGroups; ++g) {
            callbackInfos_.emplace_back(DCLCallbackInfo{this, g});
        }
    } catch (const std::bad_alloc& e) {
        logger_->error("Failed to allocate internal DCL storage: {}", e.what());
        reset();
        return std::unexpected(IOKitError::NoMemory);
    }

    NuDCLSendPacketRef previousDCL = nullptr;

    // === DCL Allocation Loop ===
    for (uint32_t g = 0; g < config_.numGroups; ++g) {
        // Callback info already prepared during vector initialization

        for (uint32_t p = 0; p < config_.packetsPerGroup; ++p) {
            uint32_t globalPacketIdx = g * config_.packetsPerGroup + p;

            // === 1. Get Buffer Pointers ===
            auto headerVMPtrExp = bufferManager.getPacketIsochHeaderValueMaskPtr(g, p);
            auto cipHdrPtrExp = bufferManager.getPacketCIPHeaderPtr(g, p);
            auto timestampPtrExp = bufferManager.getGroupTimestampPtr(g);

            if (!headerVMPtrExp || !cipHdrPtrExp || !timestampPtrExp) {
                logger_->error("Failed to get buffer pointers for G={}, P={}", g, p);
                reset();
                return std::unexpected(IOKitError::NoMemory);
            }

            auto* headerVM = headerVMPtrExp.value();
            uint8_t* cipHdrPtr = cipHdrPtrExp.value();
            uint32_t* timestampPtr = timestampPtrExp.value();

            // === CRITICAL FIX: Proper audio buffer pointer calculation ===
            // Use round-robin approach to prevent buffer overlaps
            size_t audioPayloadSize = bufferManager.getAudioPayloadSizePerPacket();
            size_t clientBufferSize = bufferManager.getClientAudioBufferSize();
            size_t bufferOffset = (globalPacketIdx * audioPayloadSize) % clientBufferSize;
            
            // Ensure we don't exceed buffer bounds
            if (bufferOffset + audioPayloadSize > clientBufferSize) {
                bufferOffset = 0;  // Wrap to beginning
                logger_->warn("Audio buffer wrap at packet {} (G={}, P={})", globalPacketIdx, g, p);
            }
            
            uint8_t* audioDataPtr = bufferManager.getClientAudioBufferPtr() + bufferOffset;

            // === 2. Initialize Isochronous Header ===
            *headerVM = makeIsoHeader(/*tag=*/1, /*sy=*/0);

            // === 3. Initialize CIP Header (NO-DATA state) ===
            FWA::Isoch::CIPHeader* cipHdr = reinterpret_cast<FWA::Isoch::CIPHeader*>(cipHdrPtr);
            bzero(cipHdr, kTransmitCIPHeaderSize);
            cipHdr->fmt_eoh1 = FWA::Isoch::CIP::kFmtEohValue;
            cipHdr->fdf = (config.sampleRate == 44100.0) ? 
                          FWA::Isoch::CIP::kFDF_44k1 : 
                          FWA::Isoch::CIP::kFDF_48k;
            cipHdr->syt = FWA::Isoch::CIP::makeBigEndianSyt(FWA::Isoch::CIP::kSytNoData);

            // === CRITICAL FIX: Start with NO-DATA packet configuration ===
            IOVirtualRange ranges[2];  // Prepare for max 2 ranges
            uint32_t numRanges = 1;    // Start with only CIP header

            // Range 0: CIP Header (ALWAYS present)
            ranges[0].address = reinterpret_cast<IOVirtualAddress>(cipHdrPtr);
            ranges[0].length = kTransmitCIPHeaderSize;

            // Range 1: Audio Payload (ONLY for data packets - initially NONE)
            // We'll set this up but won't include it in initial numRanges
            ranges[1].address = reinterpret_cast<IOVirtualAddress>(audioDataPtr);
            ranges[1].length = audioPayloadSize;

            // === 4. Allocate Send Packet DCL ===
            NuDCLSendPacketRef currentDCL = (*nuDCLPool_)->AllocateSendPacket(
                nuDCLPool_,
                nullptr,      // updateBag - not using
                numRanges,    // FIXED: Start with 1 range (CIP only)
                (::IOVirtualRange*)ranges
            );

            if (!currentDCL) {
                logger_->error("Failed to allocate NuDCLSendPacketRef for G={}, P={}", g, p);
                reset();
                return std::unexpected(IOKitError::NoMemory);
            }
            dclProgramRefs_[globalPacketIdx] = currentDCL;

            // === 5. Configure DCL ===
            UInt32 dclFlags = kNuDCLDynamic | kNuDCLUpdateBeforeCallback;
            
            // 1) Install flags first so the kernel will keep your callback bit
            (*nuDCLPool_)->SetDCLFlags(currentDCL, dclFlags);

            // 2) Then set header, timestamp, etc…
            (*nuDCLPool_)->SetDCLUserHeaderPtr(currentDCL, &headerVM->value, &headerVM->mask);
            (*nuDCLPool_)->SetDCLTimeStampPtr(currentDCL, timestampPtr);

            // 3) Only now install your callback/refCon
            bool isLastPacketInGroup = (p == config_.packetsPerGroup - 1);
            bool isCallbackGroup = ((g + 1) % callbackGroupInterval == 0);
            bool wantCallback = isLastPacketInGroup && isCallbackGroup;

            if (wantCallback) {
                (*nuDCLPool_)->SetDCLCallback(currentDCL, DCLComplete_Helper);
                (*nuDCLPool_)->SetDCLRefcon(currentDCL, &callbackInfos_[g]);
                logger_->debug("DCL Callback set for group {} (packet {}), global DCL index {}. Effective interval: {}ms.", 
                               g, p, globalPacketIdx, config_.callbackIntervalMs());
                logger_->info("Set callback for group {} (every {}ms)", g, 
                             callbackGroupInterval * config.packetsPerGroup * 125 / 1000);
            } else {
                (*nuDCLPool_)->SetDCLRefcon(currentDCL, this);
            }

            // —and **never** call SetDCLFlags(currentDCL, …) on this packet again—

            // === 6. Link Previous DCL ===
            if (previousDCL) {
                (*nuDCLPool_)->SetDCLBranch(previousDCL, currentDCL);
            }

            // === 7. Store First/Update Previous ===
            if (globalPacketIdx == 0) {
                firstDCLRef_ = currentDCL;
                logger_->debug("Stored first DCL: {:p}", (void*)firstDCLRef_);
            }
            previousDCL = currentDCL;
        }
    }

    // Store last DCL
    lastDCLRef_ = previousDCL;
    if (lastDCLRef_) {
        logger_->debug("Stored last DCL: {:p}", (void*)lastDCLRef_);
    } else {
        logger_->error("Last DCL is NULL after loop!");
    }

    // Get Program Handle
    DCLCommand* programHandle = (*nuDCLPool_)->GetProgram(nuDCLPool_);
    if (!programHandle) {
        logger_->error("GetProgram returned null after creating DCLs");
        reset();
        return std::unexpected(IOKitError::Error);
    }

    // --- REFCON VALIDATION DEBUG PASS ---
    // for each group/packet, verify that SetDCLRefcon was called with the right pointer
    logger_->info("Validating DCL refCon pointers after creation...");
    for (uint32_t g = 0; g < config_.numGroups; ++g) {
        for (uint32_t p = 0; p < config_.packetsPerGroup; ++p) {
            uint32_t idx = g * config_.packetsPerGroup + p;
            NuDCLSendPacketRef dclRef = dclProgramRefs_[idx];
            if (!dclRef) {
                logger_->error("DCL[{}] is null!", idx);
                continue;
            }
            // ask the pool what callback (if any) is installed
            NuDCLCallback cb = (*nuDCLPool_)->GetDCLCallback(dclRef);
            // ask the pool what refCon was stored
            void* actualRefCon = (*nuDCLPool_)->GetDCLRefcon(dclRef);

            // compute what we *should* have set:
            void* expectedRefCon = cb
                ? static_cast<void*>(&callbackInfos_[g])
                : static_cast<void*>(this);

            if (actualRefCon != expectedRefCon) {
                logger_->error(
                    "DCL[{}] refCon mismatch: expected {:p}, got {:p}",
                    idx, expectedRefCon, actualRefCon);
            } else {
                logger_->info(
                    "DCL[{}] refCon OK: {:p}",
                    idx, actualRefCon);
            }
        }
    }

    dclProgramCreated_ = true;
    
    logger_->info("DCL program created successfully with {} DCLs. {} callbacks per full cycle (every {}ms).",
                  config_.totalDCLCommands(),
                  config_.totalCallbacksPerCycle(),
                  config_.callbackIntervalMs());
    
    return programHandle;
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
    const IOVirtualRange ranges[], uint32_t numRanges)
{
     // Remove the lock - DCL access is safe at the driver level
     if (!dclProgramCreated_) return std::unexpected(IOKitError::NotReady);
     if (!nuDCLPool_) return std::unexpected(IOKitError::NotReady); // Should not happen

     NuDCLSendPacketRef dclRef = getDCLRef(groupIndex, packetIndexInGroup);
     if (!dclRef) {
          logger_->error("updateDCLPacket: Could not get DCL Ref for G={}, P={}", groupIndex, packetIndexInGroup);
         return std::unexpected(IOKitError::BadArgument);
     }

     // === CRITICAL VALIDATION: Prevent CIP headers being transmitted as audio data ===
     
     // Input validation
     if (!ranges) {
         logger_->error("updateDCLPacket: ranges is NULL for G={}, P={}", groupIndex, packetIndexInGroup);
         return std::unexpected(IOKitError::BadArgument);
     }
     
     if (numRanges == 0 || numRanges > 2) {
         logger_->error("updateDCLPacket: Invalid numRanges={} for G={}, P={} (expected 1 or 2)", 
                       numRanges, groupIndex, packetIndexInGroup);
         return std::unexpected(IOKitError::BadArgument);
     }

     // Range 0: Must always be CIP header (8 bytes)
     if (ranges[0].length != kTransmitCIPHeaderSize) {
         logger_->error("updateDCLPacket: Range[0] length={} != CIP header size {} for G={}, P={}", 
                       ranges[0].length, kTransmitCIPHeaderSize, groupIndex, packetIndexInGroup);
         return std::unexpected(IOKitError::BadArgument);
     }

     // Validate CIP header address is properly aligned
     if (ranges[0].address == 0) {
         logger_->error("updateDCLPacket: Range[0] CIP header address is NULL for G={}, P={}", 
                       groupIndex, packetIndexInGroup);
         return std::unexpected(IOKitError::BadArgument);
     }

     // If numRanges == 2, validate audio payload range
     if (numRanges == 2) {
         // Range 1: Audio payload validation
         if (ranges[1].length == 0) {
             logger_->error("updateDCLPacket: Range[1] audio payload length is 0 for G={}, P={}", 
                           groupIndex, packetIndexInGroup);
             return std::unexpected(IOKitError::BadArgument);
         }

         if (ranges[1].address == 0) {
             logger_->error("updateDCLPacket: Range[1] audio payload address is NULL for G={}, P={}", 
                           groupIndex, packetIndexInGroup);
             return std::unexpected(IOKitError::BadArgument);
         }

         // CRITICAL: Ensure CIP header and audio data don't overlap
         uintptr_t cipStart = ranges[0].address;
         uintptr_t cipEnd = cipStart + ranges[0].length;
         uintptr_t audioStart = ranges[1].address;
         uintptr_t audioEnd = audioStart + ranges[1].length;

         if ((cipStart >= audioStart && cipStart < audioEnd) ||
             (audioStart >= cipStart && audioStart < cipEnd)) {
             logger_->error("updateDCLPacket: CIP header [0x{:08X}-0x{:08X}) overlaps with audio data [0x{:08X}-0x{:08X}) for G={}, P={}", 
                           cipStart, cipEnd, audioStart, audioEnd, groupIndex, packetIndexInGroup);
             return std::unexpected(IOKitError::BadArgument);
         }

         // Validate audio payload size is reasonable (multiple of sample size)
         const uint32_t BYTES_PER_SAMPLE = 4;  // 24-bit sample in 32-bit container
         const uint32_t SAMPLES_PER_PACKET = 8;  // SYT_INTERVAL
         const uint32_t EXPECTED_PAYLOAD_BASE = BYTES_PER_SAMPLE * SAMPLES_PER_PACKET;
         
         if (ranges[1].length % EXPECTED_PAYLOAD_BASE != 0) {
             logger_->warn("updateDCLPacket: Audio payload length {} not multiple of expected base {} for G={}, P={}", 
                          ranges[1].length, EXPECTED_PAYLOAD_BASE, groupIndex, packetIndexInGroup);
         }
     }

     // Additional safety check: Verify we're not accidentally setting CIP header size as audio data
     for (uint32_t i = 0; i < numRanges; ++i) {
         if (i > 0 && ranges[i].length == kTransmitCIPHeaderSize) {
             logger_->error("updateDCLPacket: Range[{}] has CIP header size {} but should be audio data for G={}, P={}", 
                           i, kTransmitCIPHeaderSize, groupIndex, packetIndexInGroup);
             return std::unexpected(IOKitError::BadArgument);
         }
     }

     // Log successful validation for debugging (remove in production)
     if (logger_->should_log(spdlog::level::trace)) {
         logger_->trace("updateDCLPacket: G={}, P={}, numRanges={}, CIP[0x{:08X}:{}], Audio[0x{:08X}:{}]",
                       groupIndex, packetIndexInGroup, numRanges,
                       ranges[0].address, ranges[0].length,
                       numRanges > 1 ? ranges[1].address : 0,
                       numRanges > 1 ? ranges[1].length : 0);
     }

     // Perform the actual DCL range update
     IOReturn result = (*nuDCLPool_)->SetDCLRanges(dclRef, numRanges, (::IOVirtualRange*)ranges);
     if (result != kIOReturnSuccess) {
         logger_->error("SetDCLRanges failed for G={}, P={}: 0x{:08X}", groupIndex, packetIndexInGroup, result);
         return std::unexpected(IOKitError(result));
     }

     return {};
}

std::expected<void, IOKitError> IsochTransmitDCLManager::notifyGroupUpdate(
    IOFireWireLibLocalIsochPortRef localPort, 
    const std::vector<NuDCLRef>& groupDCLs) {
    
    if (!localPort || groupDCLs.empty()) {
        return std::unexpected(IOKitError::BadArgument);
    }
    
    std::vector<void*> dclPtrArray;
    dclPtrArray.reserve(groupDCLs.size());
    
    for (size_t i = 0; i < groupDCLs.size(); ++i) {
        if (!groupDCLs[i]) {
            logger_->error("notifyGroupUpdate: NULL DCL reference at group index {}.", i);
            return std::unexpected(IOKitError::BadArgument);
        }
        dclPtrArray.push_back(static_cast<void*>(groupDCLs[i]));
    }
    
    IOReturn result = (*localPort)->Notify(
        localPort,
        kFWNuDCLModifyNotification,
        dclPtrArray.data(),
        static_cast<UInt32>(dclPtrArray.size())
    );
    
    if (result != kIOReturnSuccess) {
        logger_->error("notifyGroupUpdate: Hardware notification failed with error: 0x{:08X}", result);
        return std::unexpected(IOKitError(result));
    }
    
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
