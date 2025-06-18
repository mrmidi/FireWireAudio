#include "Isoch/core/AmdtpTransmitter.hpp"
#include "Isoch/core/CIPHeader.hpp" // Include for CIPHeader definition
#include "Isoch/core/CIPPreCalculator.hpp"

// Add using declaration for convenient access
using FWA::Isoch::PreCalcGroup;
#include "Isoch/core/IsochTransmitBufferManager.hpp"
#include "Isoch/core/IsochPortChannelManager.hpp"
#include "Isoch/core/IsochTransmitDCLManager.hpp"
#include "Isoch/core/IsochTransportManager.hpp"
#include "Isoch/core/IsochPacketProvider.hpp"
#include <mach/mach_time.h>        // For mach_absolute_time
#include <CoreServices/CoreServices.h> // For endian swap
#include <vector>
#include <chrono>                  // For timing/sleep

#define DEBUG 1
#include <os/log.h>

// Safety checks  
static_assert(sizeof(FWA::Isoch::CIPHeader) == 8, "CIPHeader size must be 8");

// DBC Continuity Check Constants
namespace {
    constexpr uint16_t DBC_WRAP = 256;              // DBC is 8-bit, wraps at 256
    constexpr uint8_t NO_DATA_INCREMENT = 8;       // No-data packets always increment by 8
    constexpr uint8_t SYT_INTERVAL = 8;            // Normal data packet increment
    
    // Inline DBC continuity checker - optimized for fast path
    inline bool checkDbcContinuity(uint8_t currentDbc, bool isNoData,
                                   uint8_t& lastDataPacketDbc, uint8_t& lastPacketDbc,
                                   bool& prevPacketWasNoData, bool& hasValidState,
                                   const std::shared_ptr<spdlog::logger>& logger) {
        
        if (isNoData) {
            // No-data packets: DBC should be (last_data_packet_dbc + 8) mod 256
            if (hasValidState && lastDataPacketDbc != 0xFF) {
                uint8_t expectedDbc = (lastDataPacketDbc + NO_DATA_INCREMENT) % DBC_WRAP;
                if (currentDbc != expectedDbc) {
                    // COMMENTED OUT: DBC discontinuity critical log temporarily disabled
                    // logger->critical("DBC CONTINUITY ERROR: No-data packet DBC=0x{:02X}, expected=0x{:02X} "
                    //                "(last_data=0x{:02X})", currentDbc, expectedDbc, lastDataPacketDbc);
                    return false;
                }
            }
            
            lastPacketDbc = currentDbc;
            prevPacketWasNoData = true;
            
        } else {
            // Data packet
            if (!hasValidState || lastDataPacketDbc == 0xFF) {
                // First data packet - just record it
                lastDataPacketDbc = currentDbc;
                hasValidState = true;
            } else {
                // Determine expected DBC based on previous packet type
                uint8_t expectedDbc;
                if (prevPacketWasNoData) {
                    // After no-data: DBC should stay same as no-data packet
                    expectedDbc = lastPacketDbc;
                } else {
                    // Normal data-to-data: increment by SYT_INTERVAL
                    expectedDbc = (lastDataPacketDbc + SYT_INTERVAL) % DBC_WRAP;
                }
                
                if (currentDbc != expectedDbc) {
                    // COMMENTED OUT: DBC discontinuity critical log temporarily disabled
                    // logger->critical("DBC CONTINUITY ERROR: Data packet DBC=0x{:02X}, expected=0x{:02X} "
                    //                "(prev_no_data={}, last_data=0x{:02X}, last_pkt=0x{:02X})", 
                    //                currentDbc, expectedDbc, prevPacketWasNoData, 
                    //                lastDataPacketDbc, lastPacketDbc);
                    return false;
                }
            }
            
            // Update tracking variables for next iteration
            lastDataPacketDbc = currentDbc;
            lastPacketDbc = currentDbc;
            prevPacketWasNoData = false;
        }
        
        return true;
    }
}

namespace FWA {
namespace Isoch {

// --- Factory Method ---
std::shared_ptr<AmdtpTransmitter> AmdtpTransmitter::create(const TransmitterConfig& config) {
    os_log(OS_LOG_DEFAULT, "Creating AmdtpTransmitter with config: numGroups=%u, packetsPerGroup=%u",
           config.numGroups, config.packetsPerGroup);
    // Using make_shared with a helper struct to handle enable_shared_from_this properly
    struct MakeSharedEnabler : public AmdtpTransmitter {
        MakeSharedEnabler(const TransmitterConfig& cfg) : AmdtpTransmitter(cfg) {}
    };
    return std::make_shared<MakeSharedEnabler>(config);
}


ITransmitPacketProvider* AmdtpTransmitter::getPacketProvider() const {
    return packetProvider_.get(); // Return raw pointer from unique_ptr
}

std::expected<void, IOKitError> AmdtpTransmitter::startTransmit() {
    os_log(OS_LOG_DEFAULT, "AmdtpTransmitter::startTransmit called");
    // Temporary storage for callback info
    MessageCallback callback_to_notify = nullptr;
    void* refcon_to_notify = nullptr;
    IOKitError error_code = IOKitError::Success; // Use a status variable

    { // --- Start Scope for stateMutex_ ---
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (!initialized_) {
            logger_->error("startTransmit: Not initialized.");
            return std::unexpected(IOKitError::NotReady);
        }
        if (running_) {
            logger_->warn("startTransmit: Already running.");
            return {}; // Not an error
        }
        if (!portChannelManager_ || !dclManager_ || !transportManager_ || !packetProvider_ || !bufferManager_) {
            logger_->error("startTransmit: Required components not available.");
            return std::unexpected(IOKitError::NotReady);
        }

        logger_->info("AmdtpTransmitter starting transmit...");

        // --- 1. Reset State ---
        initializeCIPState(); // Reset DBC, SYT state, first callback flag etc.

        // --- 2. CORRECTED "PRIME THE PUMP" LOGIC ---
        logger_->info("Priming DCL program with initial NO-DATA state...");
        uint16_t nodeID = portChannelManager_->getLocalNodeID().value_or(0x3F);
        nodeID_ = nodeID;  // Store for emergency use

        for (uint32_t g = 0; g < config_.numGroups; ++g) {
            for (uint32_t p = 0; p < config_.packetsPerGroup; ++p) {
                // --- 2a. Get pointer to the CIP header memory location ---
                auto cipHdrPtrExp = bufferManager_->getPacketCIPHeaderPtr(g, p);
                if (!cipHdrPtrExp) {
                    logger_->error("startTransmit: Failed to get CIP header pointer for G={}, P={}", g, p);
                    error_code = IOKitError::InternalError;
                    break;
                }
                auto* cipHdrTarget = reinterpret_cast<FWA::Isoch::CIPHeader*>(cipHdrPtrExp.value());

                // --- 2b. Fill the memory with a safe NO-DATA header ---
                cipHdrTarget->sid_byte       = static_cast<uint8_t>(nodeID & 0x3F);
                cipHdrTarget->dbs            = 2; // 2 quadlets per frame for stereo
                cipHdrTarget->fn_qpc_sph_rsv = 0;
                cipHdrTarget->dbc            = 0;
                cipHdrTarget->fmt_eoh1       = FWA::Isoch::CIP::kFmtEohValue;
                cipHdrTarget->fdf            = (config_.sampleRate == 44100.0) ? 
                                               FWA::Isoch::CIP::kFDF_44k1 : 
                                               FWA::Isoch::CIP::kFDF_48k;
                cipHdrTarget->syt            = FWA::Isoch::CIP::kSytNoData;

                // --- 2c. CRITICAL FIX: Update the DCL command itself ---
                // Tell the hardware to ONLY transmit the 8-byte CIP header for this initial state
                IOVirtualRange noDataRanges[1];
                noDataRanges[0].address = reinterpret_cast<IOVirtualAddress>(cipHdrTarget);
                noDataRanges[0].length = sizeof(FWA::Isoch::CIPHeader);

                auto updateResult = dclManager_->updateDCLPacket(g, p, noDataRanges, 1);
                if (!updateResult) {
                    logger_->error("startTransmit: Failed to update DCL packet for priming G={}, P={}", g, p);
                    error_code = updateResult.error();
                    break;
                }
            }
            if (error_code != IOKitError::Success) break;
        }
        
        if (error_code == IOKitError::Success) {
            logger_->info("DCL program memory and commands configured for NO-DATA state.");
        }

        // Check for error from the loop
        if (error_code != IOKitError::Success) {    
            return std::unexpected(error_code);
        }

        logger_->debug("Initial memory preparation complete.");

        // --- 3. Fixup DCL Jumps ---
        // Link the last DCL back to the first one and notify the port.
        auto localPort = portChannelManager_->getLocalPort();
        if (!localPort) {
            logger_->error("startTransmit: Cannot get local port for DCL fixup.");
            error_code = IOKitError::NotReady; // Store error
            // Go to end of locked scope
        } else {
            auto dclFixupResult = dclManager_->fixupDCLJumpTargets(localPort);
            if (!dclFixupResult) {
                logger_->error("startTransmit: Failed to fix up DCL jump targets: {}",
                              iokit_error_category().message(static_cast<int>(dclFixupResult.error())));
                error_code = dclFixupResult.error(); // Store error
                // Go to end of locked scope
            }
        }

        // --- PLACE DCL PROGRAM DUMP HERE (Attempt 1) ---
        // At this point, the DCLs should be created, linked, and their initial content prepared.
        // The IsochPortChannelManager should also have its nuDCLPool_ if dclManager_ got it.
        if (portChannelManager_) { // Ensure portChannelManager_ exists
            IOFireWireLibNuDCLPoolRef dclPool = portChannelManager_->getNuDCLPool();
            if (dclPool) {
                CFArrayRef dclArray = (*dclPool)->GetDCLs(dclPool);
                if (dclArray) {
                    CFIndex dclCount = CFArrayGetCount(dclArray);
                    os_log(OS_LOG_DEFAULT,
                           "AmdtpTransmitter::startTransmit: NuDCLPool GetDCLs reports %ld DCLs.",
                           dclCount);

                    // You can optionally iterate and log the DCLRef pointers for more detail
                    // for (CFIndex i = 0; i < dclCount; ++i) {
                    //     NuDCLRef dcl = (NuDCLRef)CFArrayGetValueAtIndex(dclArray, i);
                    //     logger_->trace("  DCL[{}]: {:p}", i, (void*)dcl);
                    // }

                    CFRelease(dclArray); // IMPORTANT: Release the CFArrayRef
                }

                os_log(OS_LOG_DEFAULT, "--- DUMPING DCL PROGRAM (TRANSMIT) BEFORE TRANSPORT START ---");
                (*dclPool)->PrintProgram(dclPool); // Call PrintProgram
                logger_->info("--- END DCL PROGRAM DUMP (TRANSMIT) ---");
            } else {
                logger_->error("AmdtpTransmitter::startTransmit: NuDCLPool is null from portChannelManager_, cannot dump DCL program.");
                os_log(OS_LOG_DEFAULT,
                       "AmdtpTransmitter::startTransmit: NuDCLPool is null from portChannelManager_, cannot dump DCL program.");
            }
        } else {
            os_log(OS_LOG_DEFAULT,
                   "AmdtpTransmitter::startTransmit: portChannelManager_ is null, cannot get NuDCLPool to dump DCL program.");
        }

        // --- 4. Initial DCL Content Priming (Apple UniversalTransmitter style) ---
        // Prime all DCLs with NO-DATA content and notify in batches
        if (error_code == IOKitError::Success) {
            error_code = primeInitialDCLContent(localPort);
        }

        // --- 5. Initialize and Start CIP Pre-calculator BEFORE transport (only if no error so far) ---
        if (error_code == IOKitError::Success) {
            cipPreCalc_ = std::make_unique<CIPPreCalculator>();
            uint16_t nodeID = portChannelManager_->getLocalNodeID().value_or(0x3F);
            nodeID_ = nodeID;  // Store for emergency use
            cipPreCalc_->initialize(config_, nodeID);
            
            // Sync initial state to prevent startup discrepancy
            cipPreCalc_->forceSync(this->dbc_count_, this->wasNoData_);
            
            cipPreCalc_->start();
            
            // Wait for initial buffer fill
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            logger_->debug("CIP pre-calculator started with synchronized state");
        }

        // --- 6. Start Transport (only if no error so far) ---
        if (error_code == IOKitError::Success) {
            auto channel = portChannelManager_->getIsochChannel();
            if (!channel) {
                logger_->error("startTransmit: Cannot get isoch channel to start transport.");
                error_code = IOKitError::NotReady; // Store error
            } else {
                auto startResult = transportManager_->start(channel);
                if (!startResult) {
                    logger_->error("startTransmit: Failed to start transport manager: {}",
                                   iokit_error_category().message(static_cast<int>(startResult.error())));
                    error_code = startResult.error(); // Store error
                }
            }
        }

        // --- 7. Update State (only if no error so far) ---
        if (error_code == IOKitError::Success) {
            running_                 = true;
            firstDCLCallbackOccurred_ = false; // Reset for timing measurements
            isFirstTimeExecution_    = true;   // Set flag for startup logic in callback handler
            logger_->info("AmdtpTransmitter transmit started successfully.");

            // -- Read callback info while lock is held --
            callback_to_notify  = messageCallback_;
            refcon_to_notify    = messageCallbackRefCon_;
            // -----------------------------------------
        }
    } // --- End Scope for stateMutex_ --- lock is released here

    // --- Handle return/notification outside the lock ---
    if (error_code != IOKitError::Success) {
        // An error occurred during setup
        return std::unexpected(error_code);
    }

    // Call the callback directly after releasing the lock
    if (callback_to_notify) {
        callback_to_notify(static_cast<uint32_t>(TransmitterMessage::StreamStarted), 0, 0, refcon_to_notify);
    }

    return {}; // Success
}

// --- IMPLEMENT stopTransmit ---
std::expected<void, IOKitError> AmdtpTransmitter::stopTransmit() {
    std::lock_guard<std::mutex> lock(stateMutex_); // Ensure exclusive access
    if (!initialized_) {
        return {}; // Nothing to do
    }
    if (!running_) {
        return {}; // Already stopped
    }

    logger_->info("AmdtpTransmitter stopping transmit...");
    running_ = false; // Signal handlers/callbacks to stop processing ASAP
    
    // Stop CIP pre-calculator first
    if (cipPreCalc_) {
        cipPreCalc_->stop();
        cipPreCalc_.reset();
        logger_->debug("CIP pre-calculator stopped and cleaned up");
    }

    // Check required components for cleanup
    if (!portChannelManager_ || !transportManager_) {
        logger_->error("stopTransmit: PortChannelManager or TransportManager missing. Cannot stop cleanly.");
        // Setting running_ to false might prevent some crashes.
        return std::unexpected(IOKitError::NotReady);
    }

    // Get the channel to stop transport
    auto channel = portChannelManager_->getIsochChannel();
    if (!channel) {
        logger_->error("stopTransmit: Cannot get IsochChannel to stop transport.");
        running_ = true; // Revert state if we can't proceed
        return std::unexpected(IOKitError::NotReady);
    }

    // Stop the transport manager
    auto stopResult = transportManager_->stop(channel);

    logger_->info("AmdtpTransmitter transmit stopped.");
    notifyMessage(TransmitterMessage::StreamStopped); // Notify client

    if (!stopResult) {
        logger_->error("stopTransmit: TransportManager failed to stop cleanly: {}. State set to stopped, but resources might leak.",
                       iokit_error_category().message(static_cast<int>(stopResult.error())));
        // Return the error from stop so caller knows it wasn't clean
        return std::unexpected(stopResult.error());
    }

    return {}; // Success
}

// --- handleDCLOverrun implementation ---
void AmdtpTransmitter::handleDCLOverrun() {
   
    // Running check should happen *before* this is called ideally,
    // but double check here.
    if (!running_.load()) return;
   

    logger_->error("AmdtpTransmitter DCL Overrun detected!");
    notifyMessage(TransmitterMessage::OverrunError);

    // Attempt to stop the transport cleanly
    logger_->warn("Attempting to stop stream due to overrun...");
    auto stopExp = stopTransmit();
    if (!stopExp) {
        logger_->error("Failed to stop stream cleanly during overrun handling: {}",
                       iokit_error_category().message(static_cast<int>(stopExp.error())));
        // At this point, state might be inconsistent.
    }
}


// --- APPLE'S MULTI-GROUP BATCH PROCESSING ---
void AmdtpTransmitter::handleDCLCompleteFastPath(uint32_t completedGroupIndex) {
    // log critical: FIRED
    logger_->critical("AmdtpTransmitter::handleDCLCompleteFastPath called for group {}", completedGroupIndex);
    uint64_t startTime = mach_absolute_time();
    
    if (!running_.load(std::memory_order_relaxed)) return;
    
    auto localPort = portChannelManager_->getLocalPort();
    if (!localPort || !dclManager_ || !bufferManager_ || !packetProvider_ || !cipPreCalc_) {
        logger_->critical("Fast path: Required component missing!");
        return;
    }
    
    // 0. Verify DBC on the group we just sent
    for (uint32_t p = 0; p < config_.packetsPerGroup; ++p) {
        auto e = bufferManager_->getPacketCIPHeaderPtr(completedGroupIndex, p);
        if (!e) continue;
        auto* hdr = reinterpret_cast<CIPHeader*>(e.value());
        bool wasNo = (hdr->syt == CIP::kSytNoData);
        checkDbcContinuity(
            hdr->dbc, wasNo,
            lastDataPacketDbc_, lastPacketDbc_,
            prevPacketWasNoData_, hasValidDbcState_, logger_);
    }
    
    uint32_t appleGroupsPerCallback = config_.callbackGroupInterval;
    
    bool firstRun = isFirstTimeExecution_.exchange(false);
    uint32_t numGroupsToProcessThisCallback = firstRun ? 
        std::min(appleGroupsPerCallback, config_.numGroups / 4) : // Conservative start
        appleGroupsPerCallback;                                   // Apple's full batch
    
    if (firstRun) {
        logger_->info("First DCL callback (group {}). Processing {} groups for initial pipeline fill.", 
                     completedGroupIndex, numGroupsToProcessThisCallback);
    }
    
    // Calculate the index of the first group in the batch that the hardware JUST FINISHED processing.
    // This requires careful handling of wrap-around in the circular buffer.
    uint32_t firstConsumedGroupInBatch;
    if (completedGroupIndex >= (numGroupsToProcessThisCallback - 1)) {
        firstConsumedGroupInBatch = completedGroupIndex - (numGroupsToProcessThisCallback - 1);
    } else {
        // Wrap-around case: e.g., completedGroupIndex=0, numGroupsToProcess=20, totalGroups=100
        // -> firstConsumedGroup = 100 + 0 - (20 - 1) = 81
        firstConsumedGroupInBatch = config_.numGroups + completedGroupIndex - (numGroupsToProcessThisCallback - 1);
    }
    
    // Now, iterate through the batch of groups that were just consumed by the hardware.
    // For each consumed group, we need to prepare the *next* corresponding group in the DCL ring
    // that the hardware will eventually reach. This maintains a lead.
    for (uint32_t i = 0; i < numGroupsToProcessThisCallback; ++i) {
        uint32_t consumedGroupIndex = (firstConsumedGroupInBatch + i) % config_.numGroups;
        
        // Determine the group to fill: this is `callbackGroupInterval` groups ahead
        // of the group that was just consumed. This is Apple's "lead distance" strategy.
        uint32_t groupToFill = (consumedGroupIndex + config_.callbackGroupInterval) % config_.numGroups;
        
        // This function (as refactored in Phase 2) processes the groupToFill
        // and issues a single hardware notification for it.
        processAndQueueGroup(groupToFill);
    }
    
    // Group-based notification now handled in processAndQueueGroup
    std::atomic_thread_fence(std::memory_order_release);
    
    // Performance monitoring
    uint64_t endTime = mach_absolute_time();
    uint64_t elapsedNs = endTime - startTime; // Simplified timing
    
    perfStats_.totalCallbacks.fetch_add(1);
    if (elapsedNs > 10000000) {  // >10ms (very conservative for now)
        perfStats_.slowCallbacks.fetch_add(1);
    }
    
    // Update max
    uint64_t currentMax = perfStats_.maxCallbackNs.load();
    while (elapsedNs > currentMax && 
           !perfStats_.maxCallbackNs.compare_exchange_weak(currentMax, elapsedNs));
    
    // Log statistics periodically (every ~4 seconds = 8192 callbacks at 8kHz)
    static thread_local uint64_t lastStatsLog = 0;
    if ((perfStats_.totalCallbacks.load() & 0x1FFF) == 0 && 
        perfStats_.totalCallbacks.load() != lastStatsLog) {
        lastStatsLog = perfStats_.totalCallbacks.load();
        logPerformanceStatistics();
        // Log ring occupancy for monitoring
        logger_->debug("Ring occupancy: {}/16", cipPreCalc_->groupRing_.occupancy());
    }
}

// --- Helper method to set up packet DCL ranges ---
uint32_t AmdtpTransmitter::setupPacketRanges(uint32_t fillGroup, uint32_t p, bool isNoData, IOVirtualRange ranges[]) {
    auto cipPtr = bufferManager_->getPacketCIPHeaderPtr(fillGroup, p);
    if (!cipPtr) return 0;
    
    // First range is always the CIP header
    ranges[0].address = reinterpret_cast<IOVirtualAddress>(cipPtr.value());
    ranges[0].length = kTransmitCIPHeaderSize;
    uint32_t numRanges = 1;
    
    if (!isNoData) {
        // For data packets, add audio payload range
        uint32_t absIdx = fillGroup * config_.packetsPerGroup + p;
        uint8_t* audioPtr = bufferManager_->getClientAudioBufferPtr() +
            (absIdx * bufferManager_->getAudioPayloadSizePerPacket()) % 
            bufferManager_->getClientAudioBufferSize();
        
        TransmitPacketInfo info = {
            .segmentIndex = fillGroup,
            .packetIndexInGroup = p,
            .absolutePacketIndex = absIdx
        };
        
        auto fillRes = packetProvider_->fillPacketData(
            audioPtr,
            bufferManager_->getAudioPayloadSizePerPacket(),
            info
        );
        
        if (fillRes.dataLength > 0) {
            ranges[1].address = reinterpret_cast<IOVirtualAddress>(audioPtr);
            ranges[1].length = fillRes.dataLength;
            numRanges = 2;
            dataPacketsSent_.fetch_add(1);
        } else {
            noDataPacketsSent_.fetch_add(1);
        }
    } else {
        noDataPacketsSent_.fetch_add(1);
    }
    
    return numRanges;
}

// --- Helper method to process a single group ---
void AmdtpTransmitter::processAndQueueGroup(uint32_t fillGroup) {
    std::vector<NuDCLRef> groupDCLs;
    groupDCLs.reserve(config_.packetsPerGroup);
    
    // Attempt to get the next pre-calculated group from SPSC ring
    PreCalcGroup grp;
    bool havePrecalc = cipPreCalc_->groupRing_.pop(grp);
    if (!havePrecalc) {
        // EMERGENCY: Pre-calculator failed, use emergency generation
        logger_->warn("FastPath: No pre-calc data for group {}, using emergency", fillGroup);
        perfStats_.missedPrecalc.fetch_add(1);
        
        // Use emergency calculation for each packet in the group
        for (uint32_t p = 0; p < config_.packetsPerGroup; ++p) {
            auto cipPtr = bufferManager_->getPacketCIPHeaderPtr(fillGroup, p);
            if (!cipPtr) continue;
            
            CIPHeader* hdr = reinterpret_cast<CIPHeader*>(cipPtr.value());
            
            // Use emergency calculation - this will update internal state
            bool isNoData = cipPreCalc_->emergencyCalculateCIP(hdr, p);
            
            // DBC continuity check (emergency path)
            checkDbcContinuity(hdr->dbc, isNoData, lastDataPacketDbc_, lastPacketDbc_, 
                             prevPacketWasNoData_, hasValidDbcState_, logger_);
            
            // Set up DCL ranges
            IOVirtualRange ranges[2];
            ranges[0].address = reinterpret_cast<IOVirtualAddress>(hdr);
            ranges[0].length = kTransmitCIPHeaderSize;
            uint32_t numRanges = 1;
            
            if (isNoData) {
                // NO-DATA: Only CIP header
                noDataPacketsSent_.fetch_add(1);
            } else {
                // DATA: Fill audio data
                uint32_t absIdx = fillGroup * config_.packetsPerGroup + p;
                uint8_t* audioPtr = bufferManager_->getClientAudioBufferPtr() +
                    (absIdx * bufferManager_->getAudioPayloadSizePerPacket()) % 
                    bufferManager_->getClientAudioBufferSize();
                
                TransmitPacketInfo info = {
                    .segmentIndex = fillGroup,
                    .packetIndexInGroup = p,
                    .absolutePacketIndex = absIdx
                };
                
                auto fillRes = packetProvider_->fillPacketData(
                    audioPtr,
                    bufferManager_->getAudioPayloadSizePerPacket(),
                    info
                );
                
                if (fillRes.dataLength > 0) {
                    ranges[1].address = reinterpret_cast<IOVirtualAddress>(audioPtr);
                    ranges[1].length = fillRes.dataLength;
                    numRanges = 2;
                    dataPacketsSent_.fetch_add(1);
                } else {
                    noDataPacketsSent_.fetch_add(1);
                }
            }
            
            // Update DCL packet ranges
            auto updateResult = dclManager_->updateDCLPacket(fillGroup, p, ranges, numRanges);
            if (updateResult) {
                if (auto dclRef = dclManager_->getDCLRef(fillGroup, p)) {
                    groupDCLs.push_back(dclRef);
                }
            }
        }
        
        // Force sync to ensure DBC alignment between transmitter and pre-calc
        cipPreCalc_->forceSync(this->dbc_count_, this->wasNoData_);
        
        // CRITICAL: Also sync emergency state for consistent DBC calculation
        cipPreCalc_->syncEmergencyState();
        
    } else {
        // FAST PATH: Use pre-calculated headers from SPSC ring
        for (uint32_t p = 0; p < config_.packetsPerGroup; ++p) {
            const auto& pktInfo = grp.packets[p];
            
            // Get buffer pointers
            auto cipPtr = bufferManager_->getPacketCIPHeaderPtr(fillGroup, p);
            if (!cipPtr) continue;
            
            // Copy pre-calculated header (VERY FAST)
            std::memcpy(cipPtr.value(), &pktInfo.header, sizeof(CIPHeader));
            CIPHeader* hdr = reinterpret_cast<CIPHeader*>(cipPtr.value());
            
            // DBC continuity check (fast path)
            checkDbcContinuity(hdr->dbc, pktInfo.isNoData, lastDataPacketDbc_, lastPacketDbc_, 
                             prevPacketWasNoData_, hasValidDbcState_, logger_);
            
            // Set up DCL ranges
            IOVirtualRange ranges[2];
            ranges[0].address = reinterpret_cast<IOVirtualAddress>(hdr);
            ranges[0].length = kTransmitCIPHeaderSize;
            uint32_t numRanges = 1;
            
            if (pktInfo.isNoData) {
                // NO-DATA: Only CIP header, NEVER call fillPacketData!
                noDataPacketsSent_.fetch_add(1);
            } else {
                // DATA: Fill audio
                uint32_t absIdx = fillGroup * config_.packetsPerGroup + p;
                uint8_t* audioPtr = bufferManager_->getClientAudioBufferPtr() +
                    (absIdx * bufferManager_->getAudioPayloadSizePerPacket()) % 
                    bufferManager_->getClientAudioBufferSize();
                
                TransmitPacketInfo info = {
                    .segmentIndex = fillGroup,
                    .packetIndexInGroup = p,
                    .absolutePacketIndex = absIdx
                };
                
                auto fillRes = packetProvider_->fillPacketData(
                    audioPtr,
                    bufferManager_->getAudioPayloadSizePerPacket(),
                    info
                );
                
                bool haveAudio = fillRes.dataLength > 0;
                
                if (!haveAudio) {
                    // SHOULD NOT HAPPEN: If pre-calc says DATA, we should have audio!
                    // COMMENTED OUT: Audio availability critical log temporarily disabled
                    // logger_->critical("Pre-calc says DATA but no audio available for G={}, P={}",
                    //               fillGroup, p);
                } else {
                    ranges[1].address = reinterpret_cast<IOVirtualAddress>(audioPtr);
                    ranges[1].length = fillRes.dataLength;
                    numRanges = 2;
                    dataPacketsSent_.fetch_add(1);
                }
            }
            
            // Update DCL packet ranges
            auto updateResult = dclManager_->updateDCLPacket(fillGroup, p, ranges, numRanges);
            if (updateResult) {
                if (auto dclRef = dclManager_->getDCLRef(fillGroup, p)) {
                    groupDCLs.push_back(dclRef);
                }
            }
        }
        
        // Update transmitter state from pre-calculated group
        this->dbc_count_ = grp.finalDbc;
        this->wasNoData_ = grp.finalWasNoData;
    }
    
    // Notify hardware of all DCL updates for this group in a single batch
    if (!groupDCLs.empty()) {
        if (auto localPort = portChannelManager_->getLocalPort()) {
            auto notifyResult = dclManager_->notifyGroupUpdate(localPort, groupDCLs);
            if (!notifyResult) {
                logger_->error("Failed to notify hardware of DCL updates for group {}.", fillGroup);
            }
        }
    }
}

// --- Static Callbacks ---
void AmdtpTransmitter::DCLCompleteCallback_Helper(uint32_t completedGroupIndex, void* refCon) {
    AmdtpTransmitter* self = static_cast<AmdtpTransmitter*>(refCon);
    if (self) self->handleDCLCompleteFastPath(completedGroupIndex);  // Use new fast path
}

void AmdtpTransmitter::DCLOverrunCallback_Helper(void* refCon) {
    AmdtpTransmitter* self = static_cast<AmdtpTransmitter*>(refCon);
    if (self) self->handleDCLOverrun();
}

void AmdtpTransmitter::TransportFinalize_Helper(void* refCon) {
    // AmdtpTransmitter* self = static_cast<AmdtpTransmitter*>(refCon);
    // if (self) self->handleFinalize(); // If finalize handling is needed
}

// Constructor
AmdtpTransmitter::AmdtpTransmitter(const TransmitterConfig& config)
    : config_(config),
      logger_(config.logger ? config.logger : spdlog::default_logger()) {
    
    logger_->info("AmdtpTransmitter: Initializing with configuration: {}", config_.configSummary());
    
    if (!config_.isValid()) {
        logger_->error("AmdtpTransmitter: Received configuration violates safety requirements!");
        throw std::invalid_argument("Invalid TransmitterConfig provided to AmdtpTransmitter.");
    }
    
    bufferGroupStates_.resize(config_.numGroups);
    timestampBufferArray_.resize(config_.numGroups);
    
    // DCLBatcher removed - using direct group-based notification
}

// Destructor
AmdtpTransmitter::~AmdtpTransmitter() {
    logger_->info("AmdtpTransmitter destructing...");
    if (running_.load()) {
        // Attempt to stop, log errors but don't throw from destructor
        auto result = stopTransmit();
        if (!result) logger_->error("stopTransmit failed during destruction");
    }
    cleanup();
}

// cleanup
void AmdtpTransmitter::cleanup() noexcept {
    logger_->debug("AmdtpTransmitter cleanup starting...");
    packetProvider_.reset();
    transportManager_.reset();
    dclManager_.reset();
    portChannelManager_.reset();
    bufferManager_.reset();
    initialized_ = false;
    running_     = false;
    logger_->debug("AmdtpTransmitter cleanup finished."); 
}

// setupComponents
std::expected<void, IOKitError> AmdtpTransmitter::setupComponents(IOFireWireLibNubRef interface) {
    logger_->debug("AmdtpTransmitter::setupComponents - STUB");
    // Create instances...
    bufferManager_      = std::make_unique<IsochTransmitBufferManager>(logger_);
    portChannelManager_ = std::make_unique<IsochPortChannelManager>(logger_, interface,
                                                                      runLoopRef_, true /*isTalker*/);
    dclManager_        = std::make_unique<IsochTransmitDCLManager>(logger_);
    transportManager_  = std::make_unique<IsochTransportManager>(logger_);
    // Old one:
    // packetProvider_ = std::make_unique<IsochPacketProvider>(logger_, config_.clientBufferSize);
    packetProvider_ = std::make_unique<IsochPacketProvider>(logger_);

    interface_ = interface; // Store the interface reference

    // Initialize... (Error checking omitted for brevity in stub)
    bufferManager_->setupBuffers(config_);
    portChannelManager_->initialize();
    auto dclPool = portChannelManager_->getNuDCLPool();
    if (!dclPool) return std::unexpected(IOKitError::NotReady);

    auto dclProgResult = dclManager_->createDCLProgram(config_, dclPool, *bufferManager_);
    if (!dclProgResult) return std::unexpected(dclProgResult.error());
    DCLCommand* dclProgramHandle = dclProgResult.value();

    portChannelManager_->setupLocalPortAndChannel(dclProgramHandle,
                                                 bufferManager_->getBufferRange());
    dclManager_->setDCLCompleteCallback(DCLCompleteCallback_Helper, this); // Set internal callback forwarder
    dclManager_->setDCLOverrunCallback(DCLOverrunCallback_Helper, this);

    return {};
}

// initialize
std::expected<void, IOKitError> AmdtpTransmitter::initialize(IOFireWireLibNubRef interface) {
    logger_->debug("AmdtpTransmitter::initialize");
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (initialized_) return std::unexpected(IOKitError::Busy);
    if (!interface)  return std::unexpected(IOKitError::BadArgument);

    runLoopRef_ = CFRunLoopGetCurrent(); // Assign runloop

    auto setupResult = setupComponents(interface); // Call setup
    if (!setupResult) {
        cleanup();
        return setupResult;
    }
    initialized_ = true;
    return {};
}

// configure
std::expected<void, IOKitError> AmdtpTransmitter::configure(IOFWSpeed speed, uint32_t channel) {
    logger_->debug("AmdtpTransmitter::configure(Speed={}, Channel={})", (int)speed, channel);
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!initialized_)               return std::unexpected(IOKitError::NotReady);
    if (running_)                    return std::unexpected(IOKitError::Busy);
    if (!portChannelManager_)        return std::unexpected(IOKitError::NotReady);

    config_.initialSpeed   = speed;
    config_.initialChannel = channel;
    return portChannelManager_->configure(speed, channel);
}

// pushAudioData
bool AmdtpTransmitter::pushAudioData(const void* buffer, size_t bufferSizeInBytes) {
    // logger_->trace("AmdtpTransmitter::pushAudioData"); // Too noisy
    // TODO: REMOVE!!!
    if (!initialized_ || !packetProvider_) return false;
    return packetProvider_->pushAudioData(buffer, bufferSizeInBytes);
}

// setMessageCallback
void AmdtpTransmitter::setMessageCallback(MessageCallback callback, void* refCon) {
    logger_->debug("AmdtpTransmitter::setMessageCallback");
    std::lock_guard<std::mutex> lock(stateMutex_);
    messageCallback_      = callback;
    messageCallbackRefCon_ = refCon;
}

// notifyMessage
void AmdtpTransmitter::notifyMessage(TransmitterMessage msg, uint32_t p1, uint32_t p2) {
    // logger_->trace("AmdtpTransmitter::notifyMessage ({})", static_cast<uint32_t>(msg));
    MessageCallback callback = nullptr;
    void* refCon             = nullptr;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        callback = messageCallback_;
        refCon   = messageCallbackRefCon_;
    }
    if (callback) {
        callback(static_cast<uint32_t>(msg), p1, p2, refCon);
    }
}

// initializeCIPState
void AmdtpTransmitter::initializeCIPState() {
    logger_->debug("AmdtpTransmitter::initializeCIPState");
    dbc_count_   = 0;
    wasNoData_   = true; // Start assuming previous was NoData for both modes initially

    // Initialize state for "NonBlocking" (current) AmdtpTransmitter SYT logic
    sytOffset_   = TICKS_PER_CYCLE;
    sytPhase_    = 0;

    // Initialize state for "Blocking" (UniversalTransmitter-style) SYT logic
    sytOffset_blocking_ = TICKS_PER_CYCLE;
    sytPhase_blocking_  = 0;

    firstDCLCallbackOccurred_ = false;
    expectedTimeStampCycle_   = 0;
    
    // Initialize DBC continuity check state
    lastDataPacketDbc_ = 0xFF;     // 0xFF indicates uninitialized
    lastPacketDbc_ = 0xFF;         // 0xFF indicates uninitialized
    prevPacketWasNoData_ = false;
    hasValidDbcState_ = false;
}


// Performance monitoring and statistics
void AmdtpTransmitter::logPerformanceStatistics() const {
    if (!cipPreCalc_) return;
    
    // Log CIP pre-calculator statistics
    // cipPreCalc_->logStatistics();
    
    // Log transmitter callback statistics
    uint64_t total = perfStats_.totalCallbacks.load();
    uint64_t slow = perfStats_.slowCallbacks.load();
    uint64_t missed = perfStats_.missedPrecalc.load();
    uint64_t maxNs = perfStats_.maxCallbackNs.load();
    
    if (total == 0) {
        logger_->info("Transmitter Performance: No callbacks performed yet");
        return;
    }
    
    double slowPercentage = (double)slow / total * 100.0;
    double maxUs = maxNs / 1000.0;
    
    logger_->info(
        "Transmitter Performance: {} callbacks, {:.2f}% slow (>10ms), "
        "{} missed pre-calc, max {:.1f}μs",
        total, slowPercentage, missed, maxUs
    );
    
    // Log packet statistics
    uint64_t dataPkts = dataPacketsSent_.load();
    uint64_t noDataPkts = noDataPacketsSent_.load();
    uint64_t totalPkts = dataPkts + noDataPkts;
    
    if (totalPkts > 0) {
        double noDataPercentage = (double)noDataPkts / totalPkts * 100.0;
        logger_->info("Packet Statistics: {} total ({} data, {} no-data, {:.1f}% no-data)",
                     totalPkts, dataPkts, noDataPkts, noDataPercentage);
    }
}

// Apple UniversalTransmitter style initial DCL content priming
IOKitError AmdtpTransmitter::primeInitialDCLContent(IOFireWireLibLocalIsochPortRef localPort) {
    logger_->info("Priming DCL program with initial NO-DATA content (Apple style)...");
    
    if (!localPort) {
        logger_->error("primeInitialDCLContent: localPort is null");
        return IOKitError::BadArgument;
    }
    
    uint16_t nodeID = portChannelManager_->getLocalNodeID().value_or(0x3F);
    std::vector<NuDCLRef> allDCLsForNotify;
    allDCLsForNotify.reserve(config_.totalDCLCommands());
    
    // Step 1: Fill all DCLs with initial NO-DATA content using SetDCLRanges
    for (uint32_t g = 0; g < config_.numGroups; ++g) {
        for (uint32_t p = 0; p < config_.packetsPerGroup; ++p) {
            // Get CIP header buffer for this DCL
            auto cipHdrPtrExp = bufferManager_->getPacketCIPHeaderPtr(g, p);
            if (!cipHdrPtrExp) {
                logger_->error("primeInitialDCLContent: Failed to get CIP header for DCL({}, {})", g, p);
                return cipHdrPtrExp.error();
            }
            
            auto* cipHdrTarget = reinterpret_cast<FWA::Isoch::CIPHeader*>(cipHdrPtrExp.value());
            
            // Initialize NO-DATA CIP header
            cipHdrTarget->sid_byte = static_cast<uint8_t>(nodeID & 0x3F);
            cipHdrTarget->dbs = 0;  // No data blocks for NO-DATA
            cipHdrTarget->fn_qpc_sph_rsv = 0;
            cipHdrTarget->dbc = 0;  // Will be set by CIP pre-calculator
            cipHdrTarget->fmt_eoh1 = FWA::Isoch::CIP::kFmtEohValue; // MBLA format
            cipHdrTarget->fdf = 0xFF;  // NO-DATA indicator
            cipHdrTarget->syt = FWA::Isoch::CIP::makeBigEndianSyt(FWA::Isoch::CIP::kSytNoData);
            
            // Set up NO-DATA ranges (only CIP header, no payload)
            IOVirtualRange noDataRanges[1];
            noDataRanges[0].address = reinterpret_cast<IOVirtualAddress>(cipHdrTarget);
            noDataRanges[0].length = sizeof(FWA::Isoch::CIPHeader);
            
            // Update DCL content in memory (this only updates the DCL's internal ranges)
            auto updateResult = dclManager_->updateDCLPacket(g, p, noDataRanges, 1);
            if (!updateResult) {
                logger_->error("primeInitialDCLContent: Failed to update DCL({}, {}) ranges: {}", 
                              g, p, iokit_error_category().message(static_cast<int>(updateResult.error())));
                return updateResult.error();
            }
            
            // Collect DCL reference for batched notification
            if (auto dclRef = dclManager_->getDCLRef(g, p)) {
                allDCLsForNotify.push_back(dclRef);
            } else {
                logger_->error("primeInitialDCLContent: Failed to get DCL reference for DCL({}, {})", g, p);
                return IOKitError::InternalError;
            }
        }
    }
    
    logger_->debug("Initial DCL content (NO-DATA) prepared in memory for {} DCLs", allDCLsForNotify.size());
    
    // Step 2: Notify hardware of initial DCL content in batches (Apple style)
    logger_->info("Notifying hardware of initial DCL content in batches...");
    
    for (size_t i = 0; i < allDCLsForNotify.size(); i += kMaxDCLsPerModifyNotify) {
        size_t batchEnd = std::min(i + kMaxDCLsPerModifyNotify, allDCLsForNotify.size());
        std::vector<NuDCLRef> currentBatch;
        
        for (size_t k = i; k < batchEnd; ++k) {
            currentBatch.push_back(allDCLsForNotify[k]);
        }
        
        if (!currentBatch.empty()) {
            logger_->debug("Notifying batch of {} DCLs (starting at index {})", currentBatch.size(), i);
            auto notifyResult = dclManager_->notifyGroupUpdate(localPort, currentBatch);
            if (!notifyResult) {
                logger_->error("primeInitialDCLContent: Batch notification failed at index {}: {}", 
                              i, iokit_error_category().message(static_cast<int>(notifyResult.error())));
                return notifyResult.error();
            }
        }
    }
    
    logger_->info("Hardware notified of initial DCL content for {} DCLs in {} batches", 
                 allDCLsForNotify.size(), 
                 (allDCLsForNotify.size() + kMaxDCLsPerModifyNotify - 1) / kMaxDCLsPerModifyNotify);
    
    return IOKitError::Success;
}

} // namespace Isoch
} // namespace FWA
