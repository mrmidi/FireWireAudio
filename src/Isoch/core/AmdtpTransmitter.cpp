#include "Isoch/core/AmdtpTransmitter.hpp"
#include "Isoch/core/IsochTransmitBufferManager.hpp"
#include "Isoch/core/IsochPortChannelManager.hpp"
#include "Isoch/core/IsochTransmitDCLManager.hpp"
#include "Isoch/core/IsochTransportManager.hpp"
#include "Isoch/core/IsochPacketProvider.hpp"
#include <mach/mach_time.h> // For mach_absolute_time
#include <CoreServices/CoreServices.h> // For endian swap
#include <vector>
#include <chrono> // For timing/sleep 

namespace FWA {
namespace Isoch {

// --- Factory Method ---
std::shared_ptr<AmdtpTransmitter> AmdtpTransmitter::create(const TransmitterConfig& config) {
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
            if(bufferManager_->getClientAudioBufferPtr() && bufferManager_->getClientAudioBufferSize() > 0) {
               audioDataTargetPtr = bufferManager_->getClientAudioBufferPtr()
                               + (absPktIdx * bufferManager_->getAudioPayloadSizePerPacket()) % bufferManager_->getClientAudioBufferSize();
            }
            auto isochHdrPtrExp = bufferManager_->getPacketIsochHeaderPtr(g, p); // For template update

            if (!cipHdrPtrExp || !audioDataTargetPtr || !isochHdrPtrExp) {
                logger_->error("startTransmit: Failed to get buffer pointers for initial prep G={}, P={}", g, p);
                // Don't start if buffers aren't right
                error_code = IOKitError::InternalError;
                // Go to end of locked scope
                break;
            }
            CIPHeader* cipHdrTarget = reinterpret_cast<CIPHeader*>(cipHdrPtrExp.value());
            size_t audioPayloadTargetSize = bufferManager_->getAudioPayloadSizePerPacket();
            IsochHeaderData* isochHdrTarget = reinterpret_cast<IsochHeaderData*>(isochHdrPtrExp.value());

            // --- 2b. Fill Audio Payload (Initial Silence) ---
            // Ask provider to fill - it should generate silence if its buffer is empty.
            TransmitPacketInfo dummyInfo = { .segmentIndex = g, .packetIndexInGroup = p, .absolutePacketIndex = absPktIdx };
            PreparedPacketData packetDataStatus = packetProvider_->fillPacketData(
                audioDataTargetPtr,
                audioPayloadTargetSize,
                dummyInfo
            );

            // --- 2c. Prepare CIP Header (Initial State) ---
            prepareCIPHeader(cipHdrTarget);

            // --- 2d. Prepare Isoch Header Template ---
            // Set the channel, tag, tcode in the template memory
            uint8_t fwChannel = portChannelManager_->getActiveChannel().value_or(config_.initialChannel & 0x3F);
            // Calculate expected data_length (CIP + Payload, even if payload is silence for now)
            uint16_t dataLength = kTransmitCIPHeaderSize + audioPayloadTargetSize;
            isochHdrTarget->data_length = OSSwapHostToBigInt16(dataLength); // HW might overwrite if ranges differ
            isochHdrTarget->tag_channel = (1 << 6) | (fwChannel & 0x3F); // Tag=1
            isochHdrTarget->tcode_sy = (0xA << 4) | 0; // TCode=A, Sy=0
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
            running_ = true;
            firstDCLCallbackOccurred_ = false; // Reset for timing measurements
            logger_->info("AmdtpTransmitter transmit started successfully.");

            // -- Read callback info while lock is held --
            callback_to_notify = messageCallback_;
            refcon_to_notify = messageCallbackRefCon_;
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

// --- IMPLEMENTATION OF handleDCLComplete (Instance Method) ---
// This is the core real-time loop function called from the RunLoop via the static helper
void AmdtpTransmitter::handleDCLComplete(uint32_t completedGroupIndex) {
    // --- 1. State Check ---
    // Check running state *without* lock first for performance optimisation
    if (!running_.load(std::memory_order_relaxed)) {
        // logger_->trace("handleDCLComplete: Not running, ignoring callback for group {}", completedGroupIndex);
        return;
    }

    // Get essential components (check for null - should ideally not happen if running)
    auto localPort = portChannelManager_ ? portChannelManager_->getLocalPort() : nullptr;
    if (!localPort || !dclManager_ || !bufferManager_ || !packetProvider_) {
        logger_->error("handleDCLComplete: Required manager component is missing! Stopping stream.");
        // Attempt to stop cleanly, ignoring potential errors during error handling
        auto stopExp = stopTransmit(); // This will acquire the lock if needed
        notifyMessage(TransmitterMessage::Error); // Notify client of error
        return;
    }

    // --- 2. Timing & Debug ---
    uint64_t callbackEntryTime = mach_absolute_time(); // Measure entry time
    if (!firstDCLCallbackOccurred_) {
        firstDCLCallbackOccurred_ = true;
        // Potentially record first callback time for latency estimation
        logger_->info("First DCL completion callback received for group {}", completedGroupIndex);
    }

    // Read hardware completion timestamp for the completed group
    uint32_t completionTimestamp = 0;
    auto tsExp = bufferManager_->getGroupTimestampPtr(completedGroupIndex);
    if (tsExp) {
        completionTimestamp = *tsExp.value();
        // logger_->trace("Completed Group {}: HW Timestamp = {:#010x}", completedGroupIndex, completionTimestamp);
        // TODO: Use this timestamp for PLL/rate estimation later
    } else {
         logger_->warn("Could not get completion timestamp for group {}", completedGroupIndex);
    }


    // --- 3. Determine Next Segment to Fill ---
    // We need to fill the segment that the hardware will encounter *after*
    // the currently executing segments. With a callback interval of 1,
    // when group N completes, the hardware might be starting group N+1.
    // We should prepare group N+2. If the interval is larger, adjust accordingly.
    // For simplicity and robustness (double buffering), let's prepare the group
    // immediately following the completed one in the ring.
    uint32_t fillGroupIndex = (completedGroupIndex + 1) % config_.numGroups;
    // logger_->trace("handleDCLComplete: Completed Group = {}, Preparing Group = {}", completedGroupIndex, fillGroupIndex);


    // --- 4. Prepare Next Segment Loop ---
    // Iterate through all packets within the 'fillGroupIndex' segment
    for (uint32_t p = 0; p < config_.packetsPerGroup; ++p) {
        uint32_t absolutePacketIndex = fillGroupIndex * config_.packetsPerGroup + p;

        // --- 4a. Get Buffer Pointers ---
        // Pointers to where we write the headers in DMA memory
        auto isochHdrPtrExp = bufferManager_->getPacketIsochHeaderPtr(fillGroupIndex, p);
        auto cipHdrPtrExp = bufferManager_->getPacketCIPHeaderPtr(fillGroupIndex, p);
        // Pointer to where the *provider* writes audio data in DMA memory
        uint8_t* audioDataTargetPtr = nullptr;
         if(bufferManager_->getClientAudioBufferPtr() && bufferManager_->getClientAudioBufferSize() > 0) {
            audioDataTargetPtr = bufferManager_->getClientAudioBufferPtr()
                           + (absolutePacketIndex * bufferManager_->getAudioPayloadSizePerPacket()) % bufferManager_->getClientAudioBufferSize();
         }

        if (!isochHdrPtrExp || !cipHdrPtrExp || !audioDataTargetPtr) {
            logger_->error("handleDCLComplete: Failed to get buffer pointers for G={}, P={}. Skipping packet.", fillGroupIndex, p);
            continue; // Skip this packet
        }
        
        IsochHeaderData* isochHdrTarget = reinterpret_cast<IsochHeaderData*>(isochHdrPtrExp.value());
        CIPHeader* cipHdrTarget = reinterpret_cast<CIPHeader*>(cipHdrPtrExp.value());
        size_t audioPayloadTargetSize = bufferManager_->getAudioPayloadSizePerPacket();


        // --- 4b. Prepare TransmitPacketInfo ---
        // Estimate timing for this future packet (basic estimation for now)
        // TODO: Integrate with PLL/Timing module for accurate prediction
        uint64_t estimatedHostTimeNano = 0; // Placeholder
        uint32_t estimatedFirewireTimestamp = 0; // Placeholder

        TransmitPacketInfo packetInfo = {
            .segmentIndex = fillGroupIndex,
            .packetIndexInGroup = p,
            .absolutePacketIndex = absolutePacketIndex,
            .hostTimestampNano = estimatedHostTimeNano,
            .firewireTimestamp = estimatedFirewireTimestamp
        };


        // --- 4c. Fill Audio Data ---
        // Ask provider to fill the audio data directly into the DMA buffer slot
        PreparedPacketData packetDataStatus = packetProvider_->fillPacketData(
            audioDataTargetPtr,
            audioPayloadTargetSize,
            packetInfo
        );

        // Handle Underrun Notification
        if (packetDataStatus.generatedSilence) {
            // Notify client about underrun for this specific packet
             // Throttle notification?
             // logger_->warn("handleDCLComplete: Underrun preparing G={}, P={}", fillGroupIndex, p);
            notifyMessage(TransmitterMessage::BufferUnderrun, fillGroupIndex, p);
        }


        // --- 4d. Prepare CIP Header ---
        // Generate the CIP header content (with updated DBC, SYT=0xFFFF for now)
        // and write it directly into the DMA buffer slot.
        prepareCIPHeader(cipHdrTarget); // NEW - Function now calculates isNoData internally


        // --- 4e. Update Isoch Header ---
        // Update Isoch header template with appropriate data_length, channel, etc.
        uint8_t fwChannel = portChannelManager_->getActiveChannel().value_or(config_.initialChannel & 0x3F);
        isochHdrTarget->data_length = OSSwapHostToBigInt16(kTransmitCIPHeaderSize + packetDataStatus.dataLength);
        isochHdrTarget->tag_channel = (1 << 6) | (fwChannel & 0x3F);
        isochHdrTarget->tcode_sy = (0xA << 4) | 0; // TCode=0xA (Isoch Data Block)


        // --- 4f. Update DCL Ranges (if needed) ---
        // Determine the correct number of ranges based on data availability
        IOVirtualRange ranges[2];
        uint32_t numRanges = 0;

        // Range 0: CIP Header (Always present)
        ranges[0].address = reinterpret_cast<IOVirtualAddress>(cipHdrTarget);
        ranges[0].length = kTransmitCIPHeaderSize;
        numRanges++;

        // Range 1: Audio Data (Only if data is available/not NO_DATA packet)
        if (!packetDataStatus.generatedSilence && packetDataStatus.dataLength > 0) {
             // Ensure the length matches what the provider gave, even if it generated silence (length would be targetSize)
             // But we only add the range if !generatedSilence (meaning it's real data or intended silence padding)
             // And dataLength should match audioPayloadTargetSize unless something went wrong.
            ranges[1].address = reinterpret_cast<IOVirtualAddress>(audioDataTargetPtr);
            ranges[1].length = packetDataStatus.dataLength; // Use length from provider status
            numRanges++;
        } else if (packetDataStatus.generatedSilence) {
             // It's a NO_DATA packet conceptually, but we might still need to send
             // the zeroed buffer if hardware requires fixed packet sizes.
             // OR we tell the DCL to only send the CIP header.
             // Let's assume for now we send only CIP for NO_DATA (FDF=0xFF).
             numRanges = 1; // Only send CIP header range for NO_DATA packets
             logger_->trace("handleDCLComplete: Setting numRanges=1 for NO_DATA G={}, P={}", fillGroupIndex, p);
        }

        // Update the DCL command's ranges *if* the number of ranges changed
        // (e.g., switching between NO_DATA and data)
        // TODO: Need a way to get the *current* range count from the DCL to compare.
        // For now, let's call update unconditionally, assuming SetDCLRanges handles it.
        auto updateExp = dclManager_->updateDCLPacket(fillGroupIndex, p, ranges, numRanges, nullptr);
        if (!updateExp) {
             logger_->error("handleDCLComplete: Failed to update DCL packet ranges for G={}, P={}: {}",
                           fillGroupIndex, p, iokit_error_category().message(static_cast<int>(updateExp.error())));
             // Decide how to handle this error - skip packet? stop stream?
        }

    } // --- End packet loop (p) ---


    // --- 5. Notify Hardware of Memory Updates ---
    // Tell the hardware that the *memory content* (CIP headers, audio data)
    // for the 'fillGroupIndex' has been updated and needs to be re-read before execution.
    auto notifyContentExp = dclManager_->notifySegmentUpdate(localPort, fillGroupIndex);
     if (!notifyContentExp) {
         logger_->error("handleDCLComplete: Failed to notify segment content update for G={}: {}",
                       fillGroupIndex, iokit_error_category().message(static_cast<int>(notifyContentExp.error())));
        // Handle error - might lead to hardware sending stale data.
    }

    // --- 6. No DCL Jump Target Updates Needed ---
    // We use a static circular DCL program configured during setup
    // Hardware follows the pre-defined circular path

    // --- 7. Performance Monitoring (Optional) ---
    uint64_t callbackExitTime = mach_absolute_time();
    uint64_t callbackDuration = callbackExitTime - callbackEntryTime;
    
    // Convert to milliseconds (this is a simple approximation)
    static mach_timebase_info_data_t timebase;
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    
    uint64_t durationNanos = callbackDuration * timebase.numer / timebase.denom;
    double durationMs = static_cast<double>(durationNanos) / 1000000.0;
    
    // Log if duration exceeds threshold (e.g., 1ms for real-time audio)
    static const double kWarningThresholdMs = 1.0; // 1ms is quite strict for real-time audio
//    if (durationMs > kWarningThresholdMs) {
//        logger_->warn("handleDCLComplete: Long processing time for G={}: {:.3f}ms", 
//                     completedGroupIndex, durationMs);
//    }
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
    // AmdtpTransmitter* self = static_cast<AmdtpTransmitter*>(refCon);
    // if (self) self->handleFinalize(); // If finalize handling is needed
}




// Constructor
AmdtpTransmitter::AmdtpTransmitter(const TransmitterConfig& config)
 : config_(config), logger_(config.logger ? config.logger : spdlog::default_logger()) {
    logger_->info("AmdtpTransmitter constructing...");
    // Initialize other members if necessary
}

// Destructor
AmdtpTransmitter::~AmdtpTransmitter() {
    logger_->info("AmdtpTransmitter destructing...");
     if (running_.load()) {
        // Attempt to stop, log errors but don't throw from destructor
        auto result = stopTransmit();
        if(!result) logger_->error("stopTransmit failed during destruction");
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
    running_ = false;
    logger_->debug("AmdtpTransmitter cleanup finished.");
}

// setupComponents
std::expected<void, IOKitError> AmdtpTransmitter::setupComponents(IOFireWireLibNubRef interface) {
    logger_->debug("AmdtpTransmitter::setupComponents - STUB");
    // Create instances...
     bufferManager_ = std::make_unique<IsochTransmitBufferManager>(logger_);
     portChannelManager_ = std::make_unique<IsochPortChannelManager>(logger_, interface, runLoopRef_, true /*isTalker*/);
     dclManager_ = std::make_unique<IsochTransmitDCLManager>(logger_);
     transportManager_ = std::make_unique<IsochTransportManager>(logger_);
     packetProvider_ = std::make_unique<IsochPacketProvider>(logger_, config_.clientBufferSize);

     // Initialize... (Error checking omitted for brevity in stub)
     bufferManager_->setupBuffers(config_);
     portChannelManager_->initialize();
     auto dclPool = portChannelManager_->getNuDCLPool();
     if (!dclPool) return std::unexpected(IOKitError::NotReady);
     auto dclProgResult = dclManager_->createDCLProgram(config_, dclPool, *bufferManager_);
     if (!dclProgResult) return std::unexpected(dclProgResult.error());
      DCLCommand* dclProgramHandle = dclProgResult.value();
     portChannelManager_->setupLocalPortAndChannel(dclProgramHandle, bufferManager_->getBufferRange());
     dclManager_->setDCLCompleteCallback(DCLCompleteCallback_Helper, this); // Set internal callback forwarder
     dclManager_->setDCLOverrunCallback(DCLOverrunCallback_Helper, this);

    return {};
}

// initialize
std::expected<void, IOKitError> AmdtpTransmitter::initialize(IOFireWireLibNubRef interface) {
     logger_->debug("AmdtpTransmitter::initialize");
     std::lock_guard<std::mutex> lock(stateMutex_);
     if (initialized_) return std::unexpected(IOKitError::Busy);
     if (!interface) return std::unexpected(IOKitError::BadArgument);
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
    if (!initialized_) return std::unexpected(IOKitError::NotReady);
    if (running_) return std::unexpected(IOKitError::Busy);
    if (!portChannelManager_) return std::unexpected(IOKitError::NotReady);
    config_.initialSpeed = speed;
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
    messageCallback_ = callback;
    messageCallbackRefCon_ = refCon;
}

// notifyMessage
void AmdtpTransmitter::notifyMessage(TransmitterMessage msg, uint32_t p1, uint32_t p2) {
     // logger_->trace("AmdtpTransmitter::notifyMessage ({})", static_cast<uint32_t>(msg));
     MessageCallback callback = nullptr;
     void* refCon = nullptr;
     {
        std::lock_guard<std::mutex> lock(stateMutex_);
        callback = messageCallback_;
        refCon = messageCallbackRefCon_;
     }
     if (callback) {
        callback(static_cast<uint32_t>(msg), p1, p2, refCon);
     }
}

// initializeCIPState
void AmdtpTransmitter::initializeCIPState() {
     logger_->debug("AmdtpTransmitter::initializeCIPState");
     dbc_count_ = 0;
     wasNoData_ = true; // Start assuming previous was NoData

     // --- Initialize SYT state ---
     // Start offset >= TICKS_PER_CYCLE to ensure first packets are NO_DATA
     // until the first callback establishes real timing.
     sytOffset_ = TICKS_PER_CYCLE; // Initialize to 3072
     sytPhase_ = 0;
     // --- End Initialize SYT state ---

     firstDCLCallbackOccurred_ = false;
     expectedTimeStampCycle_ = 0;
}

// prepareCIPHeader
void AmdtpTransmitter::prepareCIPHeader(CIPHeader* outHeader) {
    // logger_->trace("AmdtpTransmitter::prepareCIPHeader()");
    if (!outHeader || !portChannelManager_ || !bufferManager_) { /* error */ return; }

    // --- Get Node ID, SFC, Set static fields ---
    uint16_t nodeID = portChannelManager_->getLocalNodeID().value_or(0x3F); // Default to local node ID

    // --- Determine SFC from config ---
    uint8_t sfc = 0x00; // Default 32kHz
    if (config_.sampleRate == 44100.0) sfc = 0x01;      // SFC for 44.1kHz
    else if (config_.sampleRate == 48000.0) sfc = 0x02; // SFC for 48kHz
    else if (config_.sampleRate == 88200.0) sfc = 0x03; // SFC for 88.2kHz
    else if (config_.sampleRate == 96000.0) sfc = 0x04; // SFC for 96kHz
    else if (config_.sampleRate == 176400.0) sfc = 0x05; // SFC for 176.4kHz
    else if (config_.sampleRate == 192000.0) sfc = 0x06; // SFC for 192kHz
    else {
        logger_->warn("prepareCIPHeader: Unsupported sample rate {:.1f}Hz, using SFC for 48kHz.", config_.sampleRate);
        sfc = 0x02; // Fallback
    }

    // --- Set static fields ---
    outHeader->sid_byte = 0; // Assuming HW/Port sets SID correctly
    outHeader->dbs = 2;      // AM824 Stereo (8 bytes/4 = 2)
    outHeader->fn_qpc_sph_rsv = 0; // Usually 0 for AMDTP
    outHeader->fmt_eoh1 = (0x10 << 2) | 0x01; // FMT=0x10 (AM824), EOH=1

    // --- Calculate SYT and isNoData for 44.1kHz ---
    bool calculated_isNoData = false;
    uint16_t calculated_sytVal = 0xFFFF;

    if (!firstDCLCallbackOccurred_) {
        // Before first callback, timing is unknown, force NO_DATA
        calculated_isNoData = true;
        // sytOffset_ remains >= TICKS_PER_CYCLE from initialization
    } else {
        // Apply 44.1kHz SYT offset logic based on decompiled code
        if (sytOffset_ >= TICKS_PER_CYCLE) {
            // Was NO_DATA previously, or just wrapped. Reset offset within the cycle.
            sytOffset_ -= TICKS_PER_CYCLE;
        } else {
            // Normal increment logic for 44.1kHz
            uint32_t phase = sytPhase_ % SYT_PHASE_MOD;
            bool addExtra = (phase && !(phase & 3)) || (sytPhase_ == (SYT_PHASE_RESET - 1)); // Adjusted phase check
            sytOffset_ += BASE_TICKS; // Add ~1386
            if (addExtra) {
                sytOffset_ += 1; // Add occasional extra tick
            }

            // Increment and wrap phase accumulator
            if (++sytPhase_ >= SYT_PHASE_RESET) {
                sytPhase_ = 0;
            }
        }

        // Check if the *new* offset exceeds the cycle boundary
        if (sytOffset_ >= TICKS_PER_CYCLE) {
            calculated_isNoData = true; // Will send NO_DATA this time
        } else {
            calculated_isNoData = false; // Will send valid data
            calculated_sytVal = static_cast<uint16_t>(sytOffset_);
        }
    }
    // --- End SYT Calculation ---

    // --- Set Dynamic Fields (FDF, SYT, DBC) ---
    if (calculated_isNoData) {
        outHeader->fdf = 0xFF; // FDF for NO_DATA
        outHeader->syt = OSSwapHostToBigInt16(0xFFFF); // SYT for NO_DATA
        // DBC: Repeat the previous DBC value if sending NO_DATA
        outHeader->dbc = dbc_count_;
    } else {
        outHeader->fdf = sfc; // FDF for the specific sample rate
        outHeader->syt = OSSwapHostToBigInt16(calculated_sytVal); // Calculated SYT value
        // DBC: Increment only if the *previous* packet was *not* NO_DATA
        uint8_t blocksPerPacket = bufferManager_->getAudioPayloadSizePerPacket() / 8; // 64/8 = 8 blocks typically
        uint8_t increment = blocksPerPacket;
        // Use wasNoData_ (state *before* this packet)
        uint8_t next_dbc = wasNoData_ ? dbc_count_ : (dbc_count_ + increment);
        outHeader->dbc = next_dbc & 0xFF;
    }
    // --- End Set Dynamic Fields ---

    // --- Update State for *Next* Call ---
    dbc_count_ = outHeader->dbc;    // Store the DBC we *just* put in the header
    wasNoData_ = calculated_isNoData; // Store the type of packet we *just* prepared
    // Note: sytOffset_ and sytPhase_ were already updated during calculation
}



} // namespace Isoch
} // namespace FWA
