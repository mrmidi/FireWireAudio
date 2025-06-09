#include "Isoch/core/AmdtpTransmitter.hpp"
#include "Isoch/core/CIPHeader.hpp"
#include "Isoch/core/IsochTransmitBufferManager.hpp"
#include "Isoch/core/IsochPortChannelManager.hpp"
#include "Isoch/core/IsochTransmitDCLManager.hpp"
#include "Isoch/core/IsochTransportManager.hpp"
#include "Isoch/core/IsochPacketProvider.hpp"
#include <mach/mach_time.h>        // For mach_absolute_time
#include <CoreServices/CoreServices.h> // For endian swap
#include <vector>
#include <chrono>                  // For timing/sleep
#include <os/log.h>
#include <spdlog/fmt/bin_to_hex.h> // For spdlog::to_hex

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
            CIPHeader* cipHdrTarget = reinterpret_cast<CIPHeader*>(cipHdrPtrExp.value());
            size_t audioPayloadTargetSize = bufferManager_->getAudioPayloadSizePerPacket();

            // --- 2b. Fill Audio Payload (Initial Silence) ---
            // Ask provider to fill - it should generate silence if its buffer is empty.
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
            // Directly set a minimal NO_DATA header for initial prep using new structure
            cipHdrTarget->sid            = 0; // Will be set by HW or Port later
            cipHdrTarget->dbs            = CIP::kDataBlockSizeStereo;
            cipHdrTarget->fn_qpc_sph_rsv = 0;
            cipHdrTarget->dbc            = 0;                // Initial DBC
            cipHdrTarget->fmt_eoh        = CIP::kFmtEoh;     // 0x90 for MBLA
            cipHdrTarget->fdf            = CIP::kFDF_NoDat;  // 0xFF for NO_DATA
            cipHdrTarget->syt            = CIP::makeBigEndianSyt(CIP::kSytNoData); // 0xFFFF

            // NOTE: Isoch header is now handled automatically by hardware via SetDCLUserHeaderPtr
            // No manual header preparation needed - the value/mask was set during DCL creation
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

        if (portChannelManager_) {
            IOFireWireLibNuDCLPoolRef dclPool = portChannelManager_->getNuDCLPool();
            if (dclPool) {
                CFArrayRef dclArray = (*dclPool)->GetDCLs(dclPool);
                if (dclArray) {
                    CFIndex dclCount = CFArrayGetCount(dclArray);
                    os_log(OS_LOG_DEFAULT,
                           "AmdtpTransmitter::startTransmit: NuDCLPool GetDCLs reports %ld DCLs",
                           dclCount);
                    CFRelease(dclArray);
                }

                os_log(OS_LOG_DEFAULT, "--- DUMPING DCL PROGRAM (TRANSMIT) BEFORE TRANSPORT START ---");
                (*dclPool)->PrintProgram(dclPool);
                logger_->info("--- END DCL PROGRAM DUMP (TRANSMIT) ---");
            } else {
                logger_->error("AmdtpTransmitter::startTransmit: NuDCLPool is null from portChannelManager_, cannot dump DCL program.");
                os_log(OS_LOG_DEFAULT,
                       "AmdtpTransmitter::startTransmit: NuDCLPool is null from portChannelManager_, cannot dump DCL program");
            }
        } else {
            os_log(OS_LOG_DEFAULT,
                   "AmdtpTransmitter::startTransmit: portChannelManager_ is null, cannot get NuDCLPool to dump DCL program");
        }

        // --- 4. Start Transport (only if no error so far) ---
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

        // --- 5. Update State (only if no error so far) ---
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

void AmdtpTransmitter::handleDCLComplete(uint32_t completedGroupIndex) {
    if (!running_.load(std::memory_order_relaxed)) {
        return;
    }

    auto localPort = portChannelManager_ ? portChannelManager_->getLocalPort() : nullptr;
    if (!localPort || !dclManager_ || !bufferManager_ || !packetProvider_) {
        logger_->error("handleDCLComplete: Required manager component is missing! Stopping stream.");
        auto stopExp = stopTransmit();
        notifyMessage(TransmitterMessage::Error);
        return;
    }

    uint32_t completionTimestamp = 0;
    bool hw_time_acquired = false;
    auto tsExp = bufferManager_->getGroupTimestampPtr(completedGroupIndex);
    if (tsExp && *tsExp.value() != 0) {
        completionTimestamp = *tsExp.value();
        hw_time_acquired = true;
    } else {
        if (interface_) {
            IOReturn cycleTimeResult = (*interface_)->GetCycleTime(interface_, &completionTimestamp);
            if (cycleTimeResult == kIOReturnSuccess && completionTimestamp != 0) {
                hw_time_acquired = true;
            } else {
                logger_->warn("GetCycleTime failed (0x{:X}) or returned 0. SYT might drift.", cycleTimeResult);
            }
        }
        if (!hw_time_acquired) {
            logger_->warn("Could not get completion timestamp for group {}", completedGroupIndex);
        }
    }

    if (m_appleSyTGenerator && hw_time_acquired) {
        bool is_first_ever_callback = !firstDCLCallbackOccurred_.load(std::memory_order_acquire);
        if (is_first_ever_callback) {
            bool expected_false = false;
            if (firstDCLCallbackOccurred_.compare_exchange_strong(expected_false, true,
                                                                std::memory_order_acq_rel,
                                                                std::memory_order_relaxed)) {
                m_appleSyTGenerator->seedWithHardwareTime(completionTimestamp);
            } else {
                m_appleSyTGenerator->updateCurrentTimeReference(completionTimestamp);
            }
        } else {
            m_appleSyTGenerator->updateCurrentTimeReference(completionTimestamp);
        }
    }

    uint32_t fillGroupIndex = (completedGroupIndex + 2) % config_.numGroups;
    logger_->debug("handleDCLComplete: Completed Group = {}, Preparing Group = {}",
                   completedGroupIndex, fillGroupIndex);

    for (uint32_t p = 0; p < config_.packetsPerGroup; ++p) {
        uint32_t absolutePacketIndex = fillGroupIndex * config_.packetsPerGroup + p;
        auto cipHdrPtrExp   = bufferManager_->getPacketCIPHeaderPtr(fillGroupIndex, p);
        uint8_t* audioDataTargetPtr = nullptr;
        if (bufferManager_->getClientAudioBufferPtr() && bufferManager_->getClientAudioBufferSize() > 0) {
            audioDataTargetPtr = bufferManager_->getClientAudioBufferPtr()
                               + (absolutePacketIndex * bufferManager_->getAudioPayloadSizePerPacket())
                                    % bufferManager_->getClientAudioBufferSize();
        }

        if (!cipHdrPtrExp || !audioDataTargetPtr) {
            logger_->error("handleDCLComplete: Failed to get buffer pointers for G={}, P={}. Skipping packet.", fillGroupIndex, p);
            continue;
        }

        CIPHeader*       cipHdrTarget   = reinterpret_cast<CIPHeader*>(cipHdrPtrExp.value());
        size_t           audioPayloadTargetSize = bufferManager_->getAudioPayloadSizePerPacket();

        TransmitPacketInfo packetInfo = {
            .segmentIndex        = fillGroupIndex,
            .packetIndexInGroup  = p,
            .absolutePacketIndex = absolutePacketIndex,
            .hostTimestampNano   = 0,
            .firewireTimestamp   = 0
        };

        PreparedPacketData packetDataStatus = packetProvider_->fillPacketData(
            audioDataTargetPtr,
            audioPayloadTargetSize,
            packetInfo
        );

        uint8_t next_dbc_val_for_state_update;
        bool    next_wasNoData_val_for_state_update;
        generateCIPHeaderContent(cipHdrTarget,
                                 this->dbc_count_,
                                 this->wasNoData_,
                                 this->firstDCLCallbackOccurred_.load(),
                                 next_dbc_val_for_state_update,
                                 next_wasNoData_val_for_state_update
        );

        if (packetDataStatus.forceNoDataCIP) {
            if (cipHdrTarget->fdf != 0xFF) {
                static thread_local uint32_t forceNoDataLogCounter = 0;
                if ((forceNoDataLogCounter++ % 100) == 0) {
                    logger_->debug("Provider forcing NO_DATA over SYT's DATA decision (G:{}, P:{}, count:{})", 
                                  fillGroupIndex, p, forceNoDataLogCounter);
                }
                
                cipHdrTarget->fdf = 0xFF;
                cipHdrTarget->syt = 0xFFFF;
                next_dbc_val_for_state_update = this->dbc_count_;
                next_wasNoData_val_for_state_update = true;
            }
        }

        if (cipHdrTarget->fdf == 0xFF) {
           noDataPacketsSent_.fetch_add(1, std::memory_order_relaxed);
        } else {
            dataPacketsSent_.fetch_add(1, std::memory_order_relaxed);
        }

        this->dbc_count_ = next_dbc_val_for_state_update;
        this->wasNoData_ = next_wasNoData_val_for_state_update;

        // NOTE: Isoch header channel/tag/tcode are now handled automatically by hardware
        // via SetDCLUserHeaderPtr - no manual updates needed

        uint64_t currentPacketCount = packetLogCounter_.fetch_add(1, std::memory_order_relaxed);
        if ((currentPacketCount % PACKET_LOG_INTERVAL) == 0) {
            logPacketDetails(
                fillGroupIndex,
                p,
                cipHdrTarget,
                audioDataTargetPtr,
                audioPayloadTargetSize,
                packetInfo
            );
        }

        logPacketPattern(cipHdrTarget);

        IOVirtualRange ranges[2];
        uint32_t numRanges = 1; // Always send at least the CIP header

        ranges[0].address = reinterpret_cast<IOVirtualAddress>(cipHdrTarget);
        ranges[0].length  = sizeof(FWA::Isoch::CIPHeader);

        bool sendAudioPayload = (cipHdrTarget->fdf != 0xFF);

        if (sendAudioPayload) {
            ranges[1].address = reinterpret_cast<IOVirtualAddress>(audioDataTargetPtr);
            ranges[1].length  = audioPayloadTargetSize; // Using fixed size for consistency
            numRanges++;
        }
        
        // The hardware will now automatically calculate the correct data_length
        // based on the sum of the lengths of the ranges we provide.

        // Call the corrected updateDCLPacket (no header parameter)
        auto updateExp = dclManager_->updateDCLPacket(fillGroupIndex, p, ranges, numRanges);
        if (!updateExp) {
            logger_->error("handleDCLComplete: Failed to update DCL packet ranges for G={}, P={}: {}",
                fillGroupIndex, p,
                iokit_error_category().message(static_cast<int>(updateExp.error())));
        }
    } // --- End packet loop (p) ---

    // --- 5. Notify Hardware of Memory Updates ---
    // Tell the hardware that the *memory content* (CIP headers, audio data)
    // for the 'fillGroupIndex' has been updated and needs to be re-read before execution.

    // After updating all packet content, before notifying hardware:
    std::atomic_thread_fence(std::memory_order_release);


    auto notifyContentExp = dclManager_->notifySegmentUpdate(localPort, fillGroupIndex);
    if (!notifyContentExp) {
        logger_->error("handleDCLComplete: Failed to notify segment content update for G={}: {}",
                       fillGroupIndex,
                       iokit_error_category().message(static_cast<int>(notifyContentExp.error())));
        // Handle error - might lead to hardware sending stale data.
    }

    // Log packet counts periodically
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastPacketLogTime_);
    if (elapsed.count() >= 1) {
        uint64_t dCnt = dataPacketsSent_.exchange(0, std::memory_order_relaxed);
        uint64_t nCnt = noDataPacketsSent_.exchange(0, std::memory_order_relaxed);
        logger_->info("Packets last second: data={}  no_data={} total= {}", dCnt, nCnt, (dCnt + nCnt));
        lastPacketLogTime_ = now;
    }
}

// --- Static Callbacks ---
void AmdtpTransmitter::DCLCompleteCallback_Helper(uint32_t completedGroupIndex, void* refCon) {
    AmdtpTransmitter* self = static_cast<AmdtpTransmitter*>(refCon);
    if (self) self->handleDCLComplete(completedGroupIndex);
}

void AmdtpTransmitter::DCLOverrunCallback_Helper(void* refCon) {
    AmdtpTransmitter* self = static_cast<AmdtpTransmitter*>(refCon);
    if (self) self->handleDCLOverrun();
}

void AmdtpTransmitter::TransportFinalize_Helper(void* refCon) {
    // Unused
}

AmdtpTransmitter::AmdtpTransmitter(const TransmitterConfig& config)
    : config_(config),
      logger_(config.logger ? config.logger : spdlog::default_logger()) {
    logger_->info("AmdtpTransmitter constructing...");
    
    if (config_.transmissionType == TransmissionType::Blocking) {
        m_appleSyTGenerator = std::make_unique<AppleSyTGenerator>(logger_);
    }
}

AmdtpTransmitter::~AmdtpTransmitter() {
    logger_->info("AmdtpTransmitter destructing...");
    if (running_.load()) {
        auto result = stopTransmit();
        if (!result) logger_->error("stopTransmit failed during destruction");
    }
    cleanup();
}

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

    // Initialize AppleSyTGenerator for "Blocking" (Apple-style) SYT logic
    if (m_appleSyTGenerator) {
        m_appleSyTGenerator->initialize(config_.sampleRate);
    }

    firstDCLCallbackOccurred_ = false;
    expectedTimeStampCycle_   = 0;
}


// Constants for SFC (Sample Frequency Code)
// SFC constants moved to CIP namespace in CIPHeader.hpp

void AmdtpTransmitter::generateCIPHeaderContent(FWA::Isoch::CIPHeader* outHeader,
                                                uint8_t current_dbc_state,
                                                bool previous_wasNoData_state,
                                                bool first_dcl_callback_occurred_state_param,
                                                uint8_t& next_dbc_for_state,
                                                bool& next_wasNoData_for_state) {
    if (!outHeader || !portChannelManager_ || !this->bufferManager_) {
        if (logger_) logger_->error("generateCIPHeaderContent: Preconditions not met (null pointers).");
        if (outHeader) {
            outHeader->fdf = 0xFF;
            outHeader->syt = 0xFFFF;
            outHeader->dbc = current_dbc_state;
        }
        next_dbc_for_state      = current_dbc_state;
        next_wasNoData_for_state = true;
        return;
    }

    uint16_t nodeID   = portChannelManager_->getLocalNodeID().value_or(0x3F);
    outHeader->sid            = static_cast<uint8_t>(nodeID & 0x3F);
    outHeader->dbs            = CIP::kDataBlockSizeStereo;   // 0x02
    outHeader->fn_qpc_sph_rsv = 0x00;

    // --- DBC comes *before* FMT/EOH/FDF now ---
    outHeader->dbc            = current_dbc_state;

    outHeader->fmt_eoh        = CIP::kFmtEoh;                // 0x90

    bool   calculated_isNoData_for_this_packet  = true;
    uint16_t calculated_sytVal_for_this_packet = 0xFFFF;

    if (config_.transmissionType == TransmissionType::Blocking && m_appleSyTGenerator) {
        AppleSyTGenerator::SyTResult appleResult = m_appleSyTGenerator->calculateSyT();
        calculated_isNoData_for_this_packet = appleResult.isNoData;
        calculated_sytVal_for_this_packet   = appleResult.sytValue;
    } else {
        calculated_isNoData_for_this_packet = true;
        calculated_sytVal_for_this_packet   = 0xFFFF;
    }

    if (calculated_isNoData_for_this_packet) {
        outHeader->fdf          = CIP::kFDF_NoDat;             // 0xFF
        next_dbc_for_state      = current_dbc_state;           // don't bump
    } else {
        outHeader->fdf          = (config_.sampleRate == 44100.0)
                                  ? CIP::kFDF_44k1
                                  : CIP::kFDF_48k;
        next_dbc_for_state      = static_cast<uint8_t>((current_dbc_state + 8) & 0xFF);
    }

    outHeader->syt              = CIP::makeBigEndianSyt(calculated_sytVal_for_this_packet);
    next_wasNoData_for_state = calculated_isNoData_for_this_packet;
}

// New refactored helper method for generating CIP headers from SYT decisions
void AmdtpTransmitter::generateCIPHeaderContent_from_decision(
                                FWA::Isoch::CIPHeader* outHeader,
                                bool isNoDataPacket,
                                uint16_t sytValueForDataPacket, // Host endian, 0-3071
                                uint8_t current_dbc_state,
                                uint8_t& next_dbc_for_state,
                                bool& next_wasNoData_for_state) {
    if (!outHeader || !portChannelManager_) {
        if (outHeader) {
            outHeader->fdf = CIP::kFDF_NoDat;
            outHeader->syt = CIP::makeBigEndianSyt(CIP::kSytNoData);
            outHeader->dbc = current_dbc_state;
        }
        next_dbc_for_state = current_dbc_state;
        next_wasNoData_for_state = true;
        return;
    }

    uint16_t nodeID = portChannelManager_->getLocalNodeID().value_or(0x3F);
    
    outHeader->sid            = static_cast<uint8_t>(nodeID & 0x3F);
    outHeader->dbs            = CIP::kDataBlockSizeStereo;   // 0x02
    outHeader->fn_qpc_sph_rsv = 0x00;

    // --- DBC comes *before* FMT/EOH/FDF now ---
    outHeader->dbc            = current_dbc_state;

    outHeader->fmt_eoh        = CIP::kFmtEoh;                // 0x90

    if (isNoDataPacket) {
        outHeader->fdf          = CIP::kFDF_NoDat;             // 0xFF
        outHeader->syt          = CIP::makeBigEndianSyt(CIP::kSytNoData); // 0xFFFF
        next_dbc_for_state      = current_dbc_state;           // don't bump
    } else {
        outHeader->fdf          = (config_.sampleRate == 44100.0)
                                  ? CIP::kFDF_44k1
                                  : CIP::kFDF_48k;
        outHeader->syt          = CIP::makeBigEndianSyt(sytValueForDataPacket);
        next_dbc_for_state      = static_cast<uint8_t>((current_dbc_state + 8) & 0xFF);
    }

    next_wasNoData_for_state = isNoDataPacket;
}
// Helper function implementation
void AmdtpTransmitter::logPacketDetails(
    uint32_t groupIndex,
    uint32_t packetIndexInGroup,
    const CIPHeader* cipHeader,
    const uint8_t* audioPayload,
    size_t audioPayloadSize,
    const TransmitPacketInfo& packetInfo // For additional context if needed
) { // NO-OP, PowerMac G3 is fixed for packet logging
    return;
}

// Helper to log packet patterns for verification against Apple Duet capture
void AmdtpTransmitter::logPacketPattern(const CIPHeader* cipHeader) {
    static thread_local std::string pattern;
    static thread_local uint32_t patternCount = 0;
    static thread_local uint32_t dataCount = 0;
    static thread_local uint32_t noDataCount = 0;
    
    // Long-term ratio tracking (as suggested in analysis)
    static thread_local uint64_t totalDataPackets = 0;
    static thread_local uint64_t totalNoDataPackets = 0;
    static thread_local auto startTime = std::chrono::steady_clock::now();
    
    if (!cipHeader || !logger_) return;
    
    if (cipHeader->fdf == 0xFF) {
        pattern += "N";
        noDataCount++;
        totalNoDataPackets++;
    } else {
        pattern += "D";
        dataCount++;
        totalDataPackets++;
    }
    
    // Every second, log long-term ratio
    auto elapsed = std::chrono::steady_clock::now() - startTime;
    if (elapsed > std::chrono::seconds(10)) {  // Every 10 seconds to avoid spam
        double actualRatio = (totalNoDataPackets > 0) ? (double)totalDataPackets / totalNoDataPackets : 999.0;
        double expectedRatio = 5512.5 / 2487.5;  // ≈ 2.1875 for 44.1kHz
        logger_->info("Long-term ratio: {:.4f} (expected: {:.4f}) | Total D:{} N:{}", 
                     actualRatio, expectedRatio, totalDataPackets, totalNoDataPackets);
        startTime = std::chrono::steady_clock::now();
    }
    
    if (++patternCount == 18) {  // Log every 18 packets (matches Apple capture analysis)
        double ratio = (noDataCount > 0) ? (double)dataCount / noDataCount : 999.0;
        // too noisy
        // logger_->info("Packet pattern (18): {} | D:{} N:{} | Ratio: {:.3f} | Expected: D N D D N D D D N D D N D D N D D N", 
        //              pattern, dataCount, noDataCount, ratio);
        
        // Reset for next cycle
        pattern.clear();
        patternCount = 0;
        dataCount = 0;
        noDataCount = 0;
    }
}

} // namespace Isoch
} // namespace FWA