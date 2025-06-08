#include "Isoch/core/AmdtpTransmitter.hpp"
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
            // Directly set a minimal NO_DATA header for initial prep
            cipHdrTarget->sid_byte       = 0; // Will be set by HW or Port later
            cipHdrTarget->dbs            = 2; // AM824 Stereo
            cipHdrTarget->fn_qpc_sph_rsv = 0;
            cipHdrTarget->fmt_eoh1       = (0x10 << 2) | 0x01; // FMT=0x10 (AM824), EOH=1
            cipHdrTarget->fdf           = 0xFF;                // NO_DATA
            cipHdrTarget->syt           = 0xFFFF; // NO_INFO
            cipHdrTarget->dbc           = 0;                   // Initial DBC

            // --- 2d. Prepare Isoch Header Template ---
            // Set the channel, tag, tcode in the template memory
            uint8_t fwChannel = portChannelManager_->getActiveChannel()
                                    .value_or(config_.initialChannel & 0x3F);
            // Calculate expected data_length (CIP + Payload, even if payload is silence for now)
            uint16_t dataLength = kTransmitCIPHeaderSize + audioPayloadTargetSize;
            isochHdrTarget->data_length  = OSSwapHostToBigInt16(dataLength);
            isochHdrTarget->tag_channel  = (1 << 6) | (fwChannel & 0x3F); // Tag=1
            isochHdrTarget->tcode_sy     = (0xA << 4) | 0;              // TCode=A, Sy=0
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

// --- IMPLEMENTATION OF handleDCLComplete (Instance Method) ---
// This is the core real-time loop function called from the RunLoop via the static helper
void AmdtpTransmitter::handleDCLComplete(uint32_t completedGroupIndex) {

    // EVEN MORE DEBUG LOGGING
    // --- Callback Rate Monitoring ---
    static thread_local uint64_t callbackCountSinceLastLog = 0;
    static thread_local auto lastRateLogTime = std::chrono::steady_clock::now();
    static const auto loggingInterval = std::chrono::seconds(1); // Log roughly every second

    callbackCountSinceLastLog++; // Increment for this current callback

    auto nowForRateLog = std::chrono::steady_clock::now();
    auto elapsedSinceLastRateLog = nowForRateLog - lastRateLogTime;

    if (elapsedSinceLastRateLog >= loggingInterval) {
        // Calculate the actual elapsed time in seconds (as a double for precision)
        double elapsedSeconds = std::chrono::duration<double>(elapsedSinceLastRateLog).count();

        // Calculate the rate
        double callbacksPerSecond = 0.0;
        if (elapsedSeconds > 0) { // Avoid division by zero if interval is tiny (unlikely but safe)
            callbacksPerSecond = static_cast<double>(callbackCountSinceLastLog) / elapsedSeconds;
        }

        // Log the rate
        // You can use logger_->info, logger_->debug, or os_log depending on preference
        logger_->info("DCLComplete Rate: {:.2f} callbacks/sec ({} callbacks in {:.3f}s)",
                    callbacksPerSecond,
                    callbackCountSinceLastLog,
                    elapsedSeconds);
        // os_log(OS_LOG_DEFAULT, "DCLComplete Rate: %.2f callbacks/sec (%llu callbacks in %.3f s)",
        //        callbacksPerSecond,
        //        callbackCountSinceLastLog,
        //        elapsedSeconds);


        // Reset for the next logging interval
        callbackCountSinceLastLog = 0;
        lastRateLogTime = nowForRateLog; // Or just `lastRateLogTime = std::chrono::steady_clock::now();`
    }
    // --- End Callback Rate Monitoring ---

    // DEBUG LOGGING
    // uint32_t cycleTime;
    // IOReturn result = (*interface_)->GetCycleTime(interface_, &cycleTime);
    
    // logger_->info("Callback: group={}, cycle={:08x}", completedGroupIndex, cycleTime);
    
    // // Check if we're being called too often
    // static uint32_t lastCycle = 0;
    // uint32_t cycleDiff = (cycleTime >> 12) - (lastCycle >> 12);
    // if (cycleDiff == 0) {
    //     // logger_->error("🚨 Multiple callbacks in same cycle!");
    // }
    // lastCycle = cycleTime;

    
    // os_log(OS_LOG_DEFAULT, "AmdtpTransmitter::handleDCLComplete FIRED");
    // logger_->critical("<<<<< AmdtpTransmitter::handleDCLComplete ENTERED for Group: {} >>>>>", completedGroupIndex);
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
    // We should prepare group N+2 to ensure double buffering.
    uint32_t fillGroupIndex = (completedGroupIndex + 2) % config_.numGroups;
    logger_->debug("handleDCLComplete: Completed Group = {}, Preparing Group = {}",
                   completedGroupIndex, fillGroupIndex);

    // --- 4. Prepare Next Segment Loop ---
    // Iterate through all packets within the 'fillGroupIndex' segment
    for (uint32_t p = 0; p < config_.packetsPerGroup; ++p) {
        uint32_t absolutePacketIndex = fillGroupIndex * config_.packetsPerGroup + p;

        // --- 4a. Get Buffer Pointers ---
        auto isochHdrPtrExp = bufferManager_->getPacketIsochHeaderPtr(fillGroupIndex, p);
        auto cipHdrPtrExp   = bufferManager_->getPacketCIPHeaderPtr(fillGroupIndex, p);
        uint8_t* audioDataTargetPtr = nullptr;
        if (bufferManager_->getClientAudioBufferPtr() && bufferManager_->getClientAudioBufferSize() > 0) {
            audioDataTargetPtr = bufferManager_->getClientAudioBufferPtr()
                               + (absolutePacketIndex * bufferManager_->getAudioPayloadSizePerPacket())
                                    % bufferManager_->getClientAudioBufferSize();
        }

        if (!isochHdrPtrExp || !cipHdrPtrExp || !audioDataTargetPtr) {
            logger_->error("handleDCLComplete: Failed to get buffer pointers for G={}, P={}. Skipping packet.", fillGroupIndex, p);
            continue;
        }

        IsochHeaderData* isochHdrTarget = reinterpret_cast<IsochHeaderData*>(isochHdrPtrExp.value());
        CIPHeader*       cipHdrTarget   = reinterpret_cast<CIPHeader*>(cipHdrPtrExp.value());
        size_t           audioPayloadTargetSize = bufferManager_->getAudioPayloadSizePerPacket();

        // --- 4b. Prepare TransmitPacketInfo ---
        uint64_t estimatedHostTimeNano    = 0;
        uint32_t estimatedFirewireTimestamp = 0;
        TransmitPacketInfo packetInfo = {
            .segmentIndex        = fillGroupIndex,
            .packetIndexInGroup  = p,
            .absolutePacketIndex = absolutePacketIndex,
            .hostTimestampNano   = estimatedHostTimeNano,
            .firewireTimestamp   = estimatedFirewireTimestamp
        };

        // --- 4c. Fill Audio Data (ask provider) ---
        PreparedPacketData packetDataStatus = packetProvider_->fillPacketData(
            audioDataTargetPtr,
            audioPayloadTargetSize,
            packetInfo
        );

        // --- 4d. Prepare CIP Header Content (using the new centralized method) ---
        uint8_t next_dbc_val_for_state_update;
        bool    next_wasNoData_val_for_state_update;
        generateCIPHeaderContent(cipHdrTarget,                       // Output: where to write the header
                                 this->dbc_count_,                   // Input: current DBC state from member
                                 this->wasNoData_,                   // Input: previous packet type state from member
                                 this->firstDCLCallbackOccurred_.load(), // Input: current atomic bool state
                                 next_dbc_val_for_state_update,    // Output: by reference
                                 next_wasNoData_val_for_state_update // Output: by reference
        );

        // --- 4e. POST-CORRECTION: Handle forceNoDataCIP from provider ---
        if (packetDataStatus.forceNoDataCIP) {
            if (cipHdrTarget->fdf != 0xFF) { // If SYT logic decided DATA, but provider says NO_DATA
                static thread_local uint32_t forceNoDataLogCounter = 0;
                if ((forceNoDataLogCounter++ % 100) == 0) { // Throttle logging
                    logger_->debug("Provider forcing NO_DATA over SYT's DATA decision (G:{}, P:{}, count:{})", 
                                  fillGroupIndex, p, forceNoDataLogCounter);
                }
                
                // Override CIP header to NO_DATA
                cipHdrTarget->fdf = 0xFF;
                cipHdrTarget->syt = 0xFFFF; // Already big-endian for 0xFFFF
                
                // Correct DBC state: NO_DATA packets don't increment DBC
                next_dbc_val_for_state_update = this->dbc_count_;
                next_wasNoData_val_for_state_update = true;
            }
        }

        // Update packet counters based on final CIP header state
        if (cipHdrTarget->fdf == 0xFF) {
           noDataPacketsSent_.fetch_add(1, std::memory_order_relaxed);
        } else {
            dataPacketsSent_.fetch_add(1, std::memory_order_relaxed);
        }

        // Update transmitter's persistent state AFTER all corrections
        this->dbc_count_ = next_dbc_val_for_state_update;
        this->wasNoData_ = next_wasNoData_val_for_state_update;

        // --- 4e. Update Isoch Header (based on whether it's a NO_DATA packet from CIP gen) ---
        uint8_t fwChannel = portChannelManager_->getActiveChannel()
                                .value_or(config_.initialChannel & 0x3F);
        isochHdrTarget->tag_channel = (1 << 6) | (fwChannel & 0x3F);
        isochHdrTarget->tcode_sy    = (0xA << 4) | 0; 


        // --- NEW: Conditional Packet Logging ---
        uint64_t currentPacketCount = packetLogCounter_.fetch_add(1, std::memory_order_relaxed);
        if ((currentPacketCount % PACKET_LOG_INTERVAL) == 0) {
            logPacketDetails(
                fillGroupIndex,
                p,
                isochHdrTarget,       // Pointer to the prepared Isoch Header
                cipHdrTarget,         // Pointer to the prepared CIP Header
                audioDataTargetPtr,   // Pointer to the prepared audio payload
                audioPayloadTargetSize, // Size of the audio payload for this packet
                packetInfo            // The TransmitPacketInfo struct
            );
        }

        // --- NEW: Packet Pattern Logging to verify Apple Duet pattern ---
        logPacketPattern(cipHdrTarget);

        // --- 4f. Update DCL Ranges (crucial change here) ---
        IOVirtualRange ranges[2];
        uint32_t numRanges = 0;

        // Range 0: CIP Header (Always present)
        ranges[0].address = reinterpret_cast<IOVirtualAddress>(cipHdrTarget);
        ranges[0].length  = kTransmitCIPHeaderSize;
        numRanges++;

        // Determine if we are sending audio data based on the FINAL FDF field (after forceNoDataCIP correction)
        bool sendAudioPayload = (cipHdrTarget->fdf != 0xFF);

        if (sendAudioPayload) {
            ranges[1].address = reinterpret_cast<IOVirtualAddress>(audioDataTargetPtr);
            ranges[1].length  = packetDataStatus.dataLength;
            numRanges++;
            isochHdrTarget->data_length = OSSwapHostToBigInt16(
                kTransmitCIPHeaderSize + packetDataStatus.dataLength);
        } else {
            numRanges = 1;
            isochHdrTarget->data_length = OSSwapHostToBigInt16(kTransmitCIPHeaderSize);
            // This covers both SYT-driven NO_DATA and provider-forced NO_DATA packets
        }

        auto updateExp = dclManager_->updateDCLPacket(fillGroupIndex, p, ranges, numRanges, nullptr);
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
    double   durationMs    = static_cast<double>(durationNanos) / 1000000.0;

    // Log if duration exceeds threshold (e.g., 1ms for real-time audio)
    static const double kWarningThresholdMs = 1.0; // 1ms is quite strict for real-time audio
    // if (durationMs > kWarningThresholdMs) {
    //     logger_->warn("handleDCLComplete: Long processing time for G={}: {:.3f}ms",
    //                   completedGroupIndex, durationMs);
    // }


    // Log the number of packets sent in the last second
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

    // Initialize state for "NonBlocking" (Linux-style) SYT logic
    sytOffset_   = 0;  // Start at 0 for Linux algorithm  
    sytPhase_    = 0;

    // Initialize state for "Blocking" (Apple-style) SYT logic  
    lastRawSytOffset_blocking_ = 0;  // Start at 0 as in Apple's algorithm
    sytPhase_blocking_  = 0;

    firstDCLCallbackOccurred_ = false;
    expectedTimeStampCycle_   = 0;
}


// Constants for SFC (Sample Frequency Code)
constexpr uint8_t SFC_44K1HZ = 0x01;
constexpr uint8_t SFC_48KHZ  = 0x02;

void AmdtpTransmitter::generateCIPHeaderContent(CIPHeader* outHeader,
                                                uint8_t current_dbc_state,
                                                bool previous_wasNoData_state,
                                                bool first_dcl_callback_occurred_state_param,
                                                uint8_t& next_dbc_for_state,
                                                bool& next_wasNoData_for_state) {
    // Check preconditions
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

    // --- Get Node ID, Set static fields ---
    uint16_t nodeID   = portChannelManager_->getLocalNodeID().value_or(0x3F);
    outHeader->sid_byte       = static_cast<uint8_t>(nodeID & 0x3F);
    outHeader->dbs            = 2;
    outHeader->fn_qpc_sph_rsv = 0;
    outHeader->fmt_eoh1       = (0x10 << 2) | 0x01;

    // Local variables for this function's calculations
    bool   calculated_isNoData_for_this_packet  = true;
    uint16_t calculated_sytVal_for_this_packet = 0xFFFF;
    uint8_t  sfc_for_this_packet                = SFC_48KHZ;

    // Determine SFC based on config
    if (config_.sampleRate == 44100.0) {
        sfc_for_this_packet = SFC_44K1HZ;
    } else if (config_.sampleRate == 48000.0) {
        sfc_for_this_packet = SFC_48KHZ;
    } else {
        if (logger_) {
            logger_->warn("generateCIPHeaderContent: Unsupported sample rate {:.1f}Hz, using SFC for 48kHz as fallback.",
                          config_.sampleRate);
        }
    }

    // Always use Blocking for 44.1 kHz
    BlockingSytParams sytParams = calculateBlockingSyt();
    calculated_isNoData_for_this_packet = sytParams.isNoData;
    calculated_sytVal_for_this_packet   = sytParams.syt_value;

    // --- FIXED: Set Dynamic Fields (FDF, SYT, DBC) based on Apple Duet capture analysis ---
    if (calculated_isNoData_for_this_packet) {
        // NO-DATA packet
        outHeader->fdf = 0xFF;
        outHeader->syt = 0xFFFF; // NO NEED TO CONVERT ENDIANESS FOR 0xFFFF!
        outHeader->dbc = current_dbc_state;
        
        // CRITICAL: NO-DATA packets don't increment DBC! (from Apple capture analysis)
        next_dbc_for_state = current_dbc_state;
    } else {
        // DATA packet: fixed 8 frames
        outHeader->fdf = sfc_for_this_packet;
        outHeader->syt = OSSwapHostToBigInt16(calculated_sytVal_for_this_packet);
        outHeader->dbc = current_dbc_state;
        next_dbc_for_state = (current_dbc_state + 8) & 0xFF;
    }

    // --- Update state for the *next* call (via output parameters) ---
    next_wasNoData_for_state = calculated_isNoData_for_this_packet;
}

// Old implementation commented out - replaced with Apple's correct algorithm

AmdtpTransmitter::BlockingSytParams AmdtpTransmitter::calculateBlockingSyt() {
    BlockingSytParams params;

    // Apple's "blocking" algorithm exactly as implemented in the Python script
    // Corresponds to the fixed Apple algorithm for 44.1kHz

    // 1) If we haven't had a DCL callback yet, hold off on sending data
    if (!this->firstDCLCallbackOccurred_.load()) {
        params.isNoData  = true;
        params.syt_value = 0xFFFF;   // no-data
        return params;
    }

    // 2) Apple's exact phase accumulator algorithm for 44.1kHz (fixed overshoot issue):
    
    // Check if we accumulated a full cycle from previous iteration
    if (lastRawSytOffset_blocking_ >= TICKS_PER_CYCLE) {
        lastRawSytOffset_blocking_ -= TICKS_PER_CYCLE;
        params.isNoData = true;
        params.syt_value = 0xFFFF;
        return params;  // Important: return here to prevent overshoot!
    }
    
    // Apple's phase accumulator logic (from decompiled code analysis)
    // For 44.1kHz: sampleRateMultiplier = 441
    const uint32_t sampleRateMultiplier = 441;  // 44100 / 100
    const uint32_t phaseModulo = 80;            // 0x50 in Apple's code
    
    // Phase accumulator step
    sytPhase_blocking_ += (sampleRateMultiplier % phaseModulo);  // 441 % 80 = 41
    uint32_t cycles = sampleRateMultiplier / phaseModulo;        // 441 / 80 = 5
    
    if (sytPhase_blocking_ > (phaseModulo - 1)) {  // if > 79
        sytPhase_blocking_ -= phaseModulo;
        cycles += 1;  // Add extra cycle when accumulator overflows
    }
    
    // Update SYT offset based on calculated cycles
    // Each cycle represents BASE_INCREMENT_BLOCKING ticks
    lastRawSytOffset_blocking_ += (BASE_INCREMENT_BLOCKING * cycles) / 5;  // Normalize cycles to ticks
    
    // Determine packet type based on final offset
    if (lastRawSytOffset_blocking_ >= TICKS_PER_CYCLE) {
        params.isNoData = true;
        params.syt_value = 0xFFFF;
    } else {
        params.isNoData = false;
        params.syt_value = static_cast<uint16_t>(lastRawSytOffset_blocking_);
    }

    return params;
}
// Helper function implementation
void AmdtpTransmitter::logPacketDetails(
    uint32_t groupIndex,
    uint32_t packetIndexInGroup,
    const IsochHeaderData* isochHeader,
    const CIPHeader* cipHeader,
    const uint8_t* audioPayload,
    size_t audioPayloadSize,
    const TransmitPacketInfo& packetInfo // For additional context if needed
) {
    if (!logger_ || !cipHeader || !audioPayload || !isochHeader) {
        return;
    }

    logger_->info("---------- DETAILED PACKET DUMP (G:{}, P:{}, AbsPkt:{}) ----------",
                  groupIndex, packetIndexInGroup, packetInfo.absolutePacketIndex);

    // --- Log Isoch Header (Raw and Parsed) ---
    // Assuming IsochHeaderData is a struct matching the on-wire format after endian swap
    // If it's just a byte pointer, you'd cast and parse.
    // For now, let's assume it's a struct that is already host-endian if needed for parsing,
    // or we log its raw big-endian bytes.
    std::vector<uint8_t> raw_isoch_header_bytes(kTransmitIsochHeaderSize);
    std::memcpy(raw_isoch_header_bytes.data(), isochHeader, kTransmitIsochHeaderSize);
    logger_->info("  Isoch Header (Big Endian Raw): {}", spdlog::to_hex(raw_isoch_header_bytes));

    // You would parse fields from isochHeader similar to IsochPacketProcessor if needed,
    // but for transmit, you mostly care about what you *set*.
    // Example: (assuming IsochHeaderData has members data_length, tag_channel, tcode_sy)
    logger_->info("    data_length (from IsochHdr): {}", OSSwapBigToHostInt16(isochHeader->data_length));
    logger_->info("    tag_channel (from IsochHdr): {:#04x}", isochHeader->tag_channel); // Assuming it's already set for transmit
    logger_->info("    tcode_sy    (from IsochHdr): {:#04x}", isochHeader->tcode_sy);

    // --- Log CIP Header (Raw and Parsed) ---
    // Your CIPHeader struct is byte-wise. We can log it directly.
    std::vector<uint8_t> raw_cip_header_bytes(kTransmitCIPHeaderSize);
    std::memcpy(raw_cip_header_bytes.data(), cipHeader, kTransmitCIPHeaderSize);
    logger_->info("  CIP Header (Big Endian Raw):   {}", spdlog::to_hex(raw_cip_header_bytes));

    // Parse and log human-readable CIP fields (already big-endian as per transmit prep)
    // This assumes your CIPHeader struct directly maps to the byte layout
    // and you want to interpret it as if it were on the wire (big-endian).
    // The values inside cipHeader are already prepared for transmission.
    uint16_t syt_be = cipHeader->syt; // Already big-endian from OSSwapHostToBigInt16
    uint16_t syt_host = OSSwapBigToHostInt16(syt_be); // Swap back for logging if you want host value

    logger_->info("    SID: {:#04x} (Source Node ID)", cipHeader->sid_byte & 0x3F);
    logger_->info("    DBS: {} (Data Blocks in CIP payload, quadlets)", cipHeader->dbs);
    // Further parsing of fn_qpc_sph_rsv if needed:
    // uint8_t fn  = (cipHeader->fn_qpc_sph_rsv >> 6) & 0x03;
    // uint8_t qpc = (cipHeader->fn_qpc_sph_rsv >> 3) & 0x07;
    // uint8_t sph = (cipHeader->fn_qpc_sph_rsv >> 2) & 0x01;
    // logger_->info("    FN: {}, QPC: {}, SPH: {}", fn, qpc, sph);
    logger_->info("    FMT_EOH: {:#04x} (FMT should be 0x10<<2 for AM824)", cipHeader->fmt_eoh1);
    logger_->info("    FDF: {:#04x} (SFC. 0xFF=NoData, 0x01=44.1k, 0x02=48k)", cipHeader->fdf);
    logger_->info("    SYT: {:#06x} (Host Endian: {:#06x})", syt_be, syt_host);
    logger_->info("    DBC: {:#04x} ({})", cipHeader->dbc, cipHeader->dbc);


    // --- Log Audio Payload (First few AM824 samples) ---
    if (cipHeader->fdf != 0xFF && audioPayloadSize > 0) { // If it's a DATA packet
        logger_->info("  Audio Payload ({} bytes, AM824 formatted, Big Endian):", audioPayloadSize);
        size_t bytesToLog = std::min(audioPayloadSize, (size_t)64); // Log up to 64 bytes
        std::vector<uint8_t> payload_sample_bytes(audioPayload, audioPayload + bytesToLog);
        logger_->info("    Raw Hex: {}", spdlog::to_hex(payload_sample_bytes));

        // Log first few 32-bit AM824 words (as host endian for easier reading of sample values)
        logger_->info("    First AM824 words (Host Endian, Label|Sample24):");
        for (size_t i = 0; i < bytesToLog / 4 && i < 4; ++i) { // Log up to 4 AM824 words
            uint32_t am824_word_be;
            std::memcpy(&am824_word_be, audioPayload + i * 4, sizeof(uint32_t));
            uint32_t am824_word_host = OSSwapBigToHostInt32(am824_word_be);
            uint8_t label = (am824_word_host >> 24) & 0xFF;
            int32_t sample_val_24bit = am824_word_host & 0x00FFFFFF;
            // Sign extend if necessary (AM824 typically stores signed 24-bit)
            if (sample_val_24bit & 0x00800000) { // if 24th bit (sign bit) is set
                sample_val_24bit |= 0xFF000000; // sign extend to 32-bit
            }
            logger_->info("      Word {}: {:#010x} (Label: {:#04x}, Sample24: {:#08x} ({}) )",
                          i, am824_word_host, label, (sample_val_24bit & 0x00FFFFFF), sample_val_24bit);
        }
    } else if (cipHeader->fdf == 0xFF) {
        logger_->info("  Audio Payload: NO_DATA packet");
    } else {
        logger_->info("  Audio Payload: Zero length but FDF indicates data - unusual.");
    }
    logger_->info("---------- END DETAILED PACKET DUMP ----------");
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