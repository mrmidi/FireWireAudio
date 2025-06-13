#include "Isoch/core/AmdtpTransmitter.hpp"
#include "Isoch/core/CIPHeader.hpp" // Include for CIPHeader definition
#include "Isoch/core/CIPPreCalculator.hpp"
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
    constexpr uint8_t DBC_WRAP = 256;              // DBC is 8-bit, wraps at 256
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
                    logger->critical("DBC CONTINUITY ERROR: No-data packet DBC=0x{:02X}, expected=0x{:02X} "
                                   "(last_data=0x{:02X})", currentDbc, expectedDbc, lastDataPacketDbc);
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
                    logger->critical("DBC CONTINUITY ERROR: Data packet DBC=0x{:02X}, expected=0x{:02X} "
                                   "(prev_no_data={}, last_data=0x{:02X}, last_pkt=0x{:02X})", 
                                   currentDbc, expectedDbc, prevPacketWasNoData, 
                                   lastDataPacketDbc, lastPacketDbc);
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

        // --- 2. Initial DCL Memory Preparation Loop ---
        // Pre-fill the *memory* associated with *all* DCLs with initial safe values
        logger_->debug("Performing initial memory preparation for DCL ring...");
        uint32_t totalPacketsToPrep = config_.numGroups * config_.packetsPerGroup;

        for (uint32_t absPktIdx = 0; absPktIdx < totalPacketsToPrep; ++absPktIdx) {
            uint32_t g = absPktIdx / config_.packetsPerGroup;
            uint32_t p = absPktIdx % config_.packetsPerGroup;

            // --- 2a. Get Buffer Pointers ---
            auto cipHdrPtrExp = bufferManager_->getPacketCIPHeaderPtr(g, p);
            uint8_t* audioDataTargetPtr = nullptr;
            if (bufferManager_->getClientAudioBufferPtr() && bufferManager_->getClientAudioBufferSize() > 0) {
                audioDataTargetPtr = bufferManager_->getClientAudioBufferPtr()
                                   + (absPktIdx * bufferManager_->getAudioPayloadSizePerPacket())
                                        % bufferManager_->getClientAudioBufferSize();
            }
            if (!cipHdrPtrExp || !audioDataTargetPtr) {
                logger_->error("startTransmit: Failed to get buffer pointers for initial prep G={}, P={}", g, p);
                // Don't start if buffers aren't right
                error_code = IOKitError::InternalError;
                // Go to end of locked scope
                break;
            }
            FWA::Isoch::CIPHeader* cipHdrTarget = reinterpret_cast<FWA::Isoch::CIPHeader*>(cipHdrPtrExp.value());
            size_t audioPayloadTargetSize = bufferManager_->getAudioPayloadSizePerPacket();

            // --- 2b. Fill Audio Payload (Initial Silence) ---
            TransmitPacketInfo dummyInfo = {
                .segmentIndex        = g,
                .packetIndexInGroup  = p,
                .absolutePacketIndex = absPktIdx
            };
            PreparedPacketData packetDataStatus = packetProvider_->fillPacketData(
                audioDataTargetPtr,
                audioPayloadTargetSize,
                dummyInfo
            );

            // --- 2c. Prepare CIP Header (Initial State) ---
            // Set an initial NO_DATA header using the new constants for clarity.
            cipHdrTarget->sid_byte       = 0; // Will be set by HW or Port later
            cipHdrTarget->dbs            = 2; // 2 quadlets (8 bytes) per frame for stereo
            cipHdrTarget->fn_qpc_sph_rsv = 0;
            cipHdrTarget->dbc            = 0;
            cipHdrTarget->fmt_eoh1       = FWA::Isoch::CIP::kFmtEohValue;
            cipHdrTarget->fdf            = (config_.sampleRate == 44100.0) ? 
                                           FWA::Isoch::CIP::kFDF_44k1 : 
                                           FWA::Isoch::CIP::kFDF_48k;
            cipHdrTarget->syt            = FWA::Isoch::CIP::kSytNoData; // don't need to conver 0xFFFF to big endian
        } // End initial prep loop

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

        // --- 4. Initialize and Start CIP Pre-calculator (only if no error so far) ---
        if (error_code == IOKitError::Success) {
            cipPreCalc_ = std::make_unique<CIPPreCalculator>();
            uint16_t nodeID = portChannelManager_->getLocalNodeID().value_or(0x3F);
            nodeID_ = nodeID;  // Store for emergency use
            cipPreCalc_->initialize(config_, nodeID);
            
            // Sync initial state to prevent startup discrepancy
            cipPreCalc_->forceSync(this->dbc_count_, this->wasNoData_);
            
            cipPreCalc_->start();
            
            // Wait longer for initial buffer fill
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            logger_->debug("CIP pre-calculator started with synchronized state");
        }

        // --- 5. Start Transport (only if no error so far) ---
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

        // --- 6. Update State (only if no error so far) ---
        if (error_code == IOKitError::Success) {
            running_                 = true;
            firstDCLCallbackOccurred_ = false; // Reset for timing measurements
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


// --- NEW FAST-PATH DCL CALLBACK with CIP Pre-calculation ---
void AmdtpTransmitter::handleDCLCompleteFastPath(uint32_t completedGroupIndex) {
    uint64_t startTime = mach_absolute_time();
    
    if (!running_.load(std::memory_order_relaxed)) return;
    
    auto localPort = portChannelManager_->getLocalPort();
    if (!localPort || !dclManager_ || !bufferManager_ || !packetProvider_ || !cipPreCalc_) {
        logger_->critical("Fast path: Required component missing!");
        return;
    }
    
    uint32_t fillGroup = (completedGroupIndex + kGroupsPerCallback) % config_.numGroups;
    
    // Use version-based getGroupState with retry logic
    const auto* groupState = cipPreCalc_->getGroupState(fillGroup);
    if (!groupState) {
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
            dclManager_->updateDCLPacket(fillGroup, p, ranges, numRanges);
        }
        
        // Force sync to ensure DBC alignment between transmitter and pre-calc
        cipPreCalc_->forceSync(this->dbc_count_, this->wasNoData_);
        
    } else {
        // FAST PATH: Use pre-calculated headers
        for (uint32_t p = 0; p < config_.packetsPerGroup; ++p) {
            const auto& pktInfo = groupState->packets[p];
            
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
                // DATA: Fill audio and potentially reconcile
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
                
                // Overwrite SYT if it was pre-calc'd as DATA but we got no data:
                if (!haveAudio) {
                    // SHOULD NOT HAPPEN: If pre-calc says DATA, we should have audio!
                    logger_->critical("Pre-calc says DATA but no audio available for G={}, P={}",
                                  fillGroup, p);
                    // hdr->syt = FWA::Isoch::CIP::kSytNoData;
                    // noDataPacketsSent_.fetch_add(1);
                } else {
                    ranges[1].address = reinterpret_cast<IOVirtualAddress>(audioPtr);
                    ranges[1].length = fillRes.dataLength;
                    numRanges = 2;
                    dataPacketsSent_.fetch_add(1);
                }
            }
            
            // Update DCL packet ranges - NO zero-length audio ranges!
            dclManager_->updateDCLPacket(fillGroup, p, ranges, numRanges);
        }
        
        // Mark group as consumed for flow control
        cipPreCalc_->markGroupConsumed(fillGroup);
        
        // keep transmitter and pre-calc DBC in lock-step
        this->dbc_count_ = groupState->finalDbc;
        this->wasNoData_ = groupState->packets.back().isNoData;
    }
    
    // Notify hardware
    std::atomic_thread_fence(std::memory_order_release);
    dclManager_->notifySegmentUpdate(localPort, fillGroup);
    
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
    
    // Log statistics periodically (every ~5 seconds at 8kHz callback rate)
    static thread_local uint64_t lastStatsLog = 0;
    if ((perfStats_.totalCallbacks.load() % 40000) == 0 && 
        perfStats_.totalCallbacks.load() != lastStatsLog) {
        lastStatsLog = perfStats_.totalCallbacks.load();
        logPerformanceStatistics();
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
    logger_->info("AmdtpTransmitter constructing...");
    // Initialize other members if necessary
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

AmdtpTransmitter::NonBlockingSytParams AmdtpTransmitter::calculateNonBlockingSyt(
    uint8_t current_dbc_state, bool previous_wasNoData_state) {
    NonBlockingSytParams params;

    if (!firstDCLCallbackOccurred_) {
        params.isNoData   = true;
        params.syt_value  = 0xFFFF;
    } else {
        if (sytOffset_ >= TICKS_PER_CYCLE) {
            sytOffset_ -= TICKS_PER_CYCLE;
        } else {
            uint32_t phase    = sytPhase_ % SYT_PHASE_MOD;
            bool     addExtra = (phase && !(phase & 3)) || (sytPhase_ == (SYT_PHASE_RESET - 1));
            sytOffset_ += BASE_TICKS;
            if (addExtra) {
                sytOffset_ += 1;
            }
            if (++sytPhase_ >= SYT_PHASE_RESET) {
                sytPhase_ = 0;
            }
        }

        if (sytOffset_ >= TICKS_PER_CYCLE) {
            params.isNoData  = true;
            params.syt_value = 0xFFFF;
        } else {
            params.isNoData  = false;
            params.syt_value = static_cast<uint16_t>(sytOffset_);
        }
    }

    return params;
}

// These constants are now defined in CIPHeader.hpp as FWA::Isoch::CIP::kFDF_44k1, etc.

void AmdtpTransmitter::generateCIPHeaderContent(FWA::Isoch::CIPHeader* outHeader,
                                                uint8_t current_dbc_state,
                                                bool previous_wasNoData_state,
                                                bool first_dcl_callback_occurred_state_param,
                                                uint8_t& next_dbc_for_state,
                                                bool& next_wasNoData_for_state) {
    // Check preconditions
    if (!outHeader || !portChannelManager_ || !this->bufferManager_) {
        if (logger_) logger_->error("generateCIPHeaderContent: Preconditions not met (null pointers).");
        if (outHeader) {
            outHeader->fdf = (config_.sampleRate == 44100.0) ? 
                             FWA::Isoch::CIP::kFDF_44k1 : 
                             FWA::Isoch::CIP::kFDF_48k;
            outHeader->syt = FWA::Isoch::CIP::makeBigEndianSyt(FWA::Isoch::CIP::kSytNoData);
            outHeader->dbc = current_dbc_state;
        }
        next_dbc_for_state      = current_dbc_state;
        next_wasNoData_for_state = true;
        return;
    }

    // --- Get Node ID, Set static fields ---
    uint16_t nodeID   = portChannelManager_->getLocalNodeID().value_or(0x3F);
    outHeader->sid_byte       = static_cast<uint8_t>(nodeID & 0x3F); // TODO: Check if this should be part of CIP or Isoch header
    outHeader->dbs            = 2; // Stereo = 2 quadlets per block
    outHeader->fn_qpc_sph_rsv = 0;
    outHeader->fmt_eoh1       = FWA::Isoch::CIP::kFmtEohValue;

    // Local variables for this function's calculations
    bool   calculated_isNoData_for_this_packet  = true;
    uint16_t calculated_sytVal_for_this_packet = FWA::Isoch::CIP::kSytNoData;
    uint8_t  sfc_for_this_packet                = FWA::Isoch::CIP::kFDF_48k;

    // Determine SFC based on config
    if (config_.sampleRate == 44100.0) {
        sfc_for_this_packet = FWA::Isoch::CIP::kFDF_44k1;
    } else if (config_.sampleRate == 48000.0) {
        sfc_for_this_packet = FWA::Isoch::CIP::kFDF_48k;
    } else {
        if (logger_) {
            logger_->warn("generateCIPHeaderContent: Unsupported sample rate {:.1f}Hz, using SFC for 48kHz as fallback.",
                          config_.sampleRate);
        }
    }

    // --- Call Strategy-Specific SYT Calculation ---
    switch (config_.transmissionType) {
        case TransmissionType::Blocking: {
            BlockingSytParams sytParams = calculateBlockingSyt();
            calculated_isNoData_for_this_packet = sytParams.isNoData;
            calculated_sytVal_for_this_packet   = sytParams.syt_value;
            break;
        }
        case TransmissionType::NonBlocking:
        default: {
            NonBlockingSytParams sytParams = calculateNonBlockingSyt(current_dbc_state,
                                                                      previous_wasNoData_state);
            calculated_isNoData_for_this_packet = sytParams.isNoData;
            calculated_sytVal_for_this_packet   = sytParams.syt_value;
            break;
        }
    }

    // --- Set Dynamic Fields (FDF, SYT, DBC) ---
    uint8_t blocksPerPacket = this->bufferManager_->getAudioPayloadSizePerPacket() / 8;
    if (blocksPerPacket == 0) {
        blocksPerPacket = 8; // stereo, 8 blocks per packet
    }

    if (calculated_isNoData_for_this_packet) {
        outHeader->fdf = sfc_for_this_packet;  // Always use sample rate, not 0xFF
        outHeader->syt = FWA::Isoch::CIP::makeBigEndianSyt(FWA::Isoch::CIP::kSytNoData);
        outHeader->dbc = (current_dbc_state + blocksPerPacket) & 0xFF;  // +8
    } else {
        outHeader->fdf = sfc_for_this_packet;
        outHeader->syt = FWA::Isoch::CIP::makeBigEndianSyt(calculated_sytVal_for_this_packet);
        
        if (previous_wasNoData_state) {
            outHeader->dbc = current_dbc_state;                      // +0
        } else {
            outHeader->dbc = (current_dbc_state + blocksPerPacket) & 0xFF; // +8
        }
    }

    // --- Update state for the *next* call (via output parameters) ---
    next_dbc_for_state       = outHeader->dbc;
    next_wasNoData_for_state = calculated_isNoData_for_this_packet;
}


AmdtpTransmitter::BlockingSytParams AmdtpTransmitter::calculateBlockingSyt() {
    BlockingSytParams params;

    // 1) If we haven't had a DCL callback yet, hold off on sending data
    if (!this->firstDCLCallbackOccurred_.load()) {
        params.isNoData  = true;
        params.syt_value = 0xFFFF;   // no-data
        return params;
    }

    // 2) If offset >= one full FireWire cycle (3072), subtract and emit NO_DATA
    if (sytOffset_blocking_ >= TICKS_PER_CYCLE) {
        sytOffset_blocking_ -= TICKS_PER_CYCLE;
        params.isNoData  = true;     // mark no-data on that overflow
        params.syt_value = 0xFFFF;
        return params;
    }

    // 3) Otherwise, advance by BASE_TICKS_BLOCKING + occasional “+1” jitter
    uint32_t phase    = sytPhase_blocking_ % SYT_PHASE_MOD_BLOCKING;   // 0..146
    bool     addExtra = (phase && !(phase & 3)) 
                         || (sytPhase_blocking_ == (SYT_PHASE_RESET_BLOCKING - 1));
    sytOffset_blocking_ += BASE_TICKS_BLOCKING;  // = 565 on each iteration
    if (addExtra) {
        sytOffset_blocking_ += 1;                // +1 every 4th iteration
    }
    if (++sytPhase_blocking_ >= SYT_PHASE_RESET_BLOCKING) {
        sytPhase_blocking_ = 0;
    }

    // 4) Now decide per-packet if it’s data or no-data
    if (sytOffset_blocking_ >= TICKS_PER_CYCLE) {
        // overflow→no-data
        params.isNoData  = true;
        params.syt_value = 0xFFFF;
    } else {
        // within cycle → data packet, embed the SYT
        params.isNoData  = false;
        params.syt_value = static_cast<uint16_t>(sytOffset_blocking_);
    }

    return params;
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

} // namespace Isoch
} // namespace FWA
