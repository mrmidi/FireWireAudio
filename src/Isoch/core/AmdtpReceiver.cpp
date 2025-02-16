#include "Isoch/core/AmdtpReceiver.hpp"
// Include the needed headers
#include "Isoch/core/IsochDCLManager.hpp"
#include "Isoch/core/IsochPortChannelManager.hpp"
#include "Isoch/core/IsochBufferManager.hpp"
#include "Isoch/core/IsochTransportManager.hpp"
#include "Isoch/core/IsochPacketProcessor.hpp"
#include "Isoch/core/IsochMonitoringManager.hpp"
#include "Isoch/core/AudioClockPLL.hpp"     // Include the new AudioClockPLL header
#include "Isoch/utils/RingBuffer.hpp"       // Include RingBuffer header
#include <spdlog/spdlog.h>
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <mach/mach_time.h>                // For mach_absolute_time
#include <time.h>                          // For clock_gettime_nsec_np
#include <unistd.h>
// to_hex for spdlog
#include <spdlog/fmt/bin_to_hex.h>

#include "Isoch/utils/RingBuffer.hpp"

namespace FWA {
namespace Isoch {

// Helper (can be moved to utils)
static inline uint64_t absolute_to_nanoseconds(uint64_t mach_time) {
    mach_timebase_info_data_t timebase_info;
    mach_timebase_info(&timebase_info);
    if (timebase_info.denom == 0) return 0;
    long double conversion_factor = static_cast<long double>(timebase_info.numer) / timebase_info.denom;
    return static_cast<uint64_t>(mach_time * conversion_factor);
}
// --- End Helper ---

std::shared_ptr<AmdtpReceiver> AmdtpReceiver::create(const ReceiverConfig& config) {
    if (!config.logger) {
        // Use default logger if none provided
        auto defaultLogger = spdlog::default_logger();
        ReceiverConfig updatedConfig = config;
        updatedConfig.logger = defaultLogger;
        return std::shared_ptr<AmdtpReceiver>(new AmdtpReceiver(updatedConfig));
    }
    
    return std::shared_ptr<AmdtpReceiver>(new AmdtpReceiver(config));
}

AmdtpReceiver::AmdtpReceiver(const ReceiverConfig& config)
    : config_(config)
    , logger_(config.logger) {
    
    if (logger_) {
        logger_->info("AmdtpReceiver created with numGroups={}, packetsPerGroup={}, packetDataSize={}",
                     config_.numGroups, config_.packetsPerGroup, config_.packetDataSize);
    }
}

AmdtpReceiver::~AmdtpReceiver() {
    // Stop receiver if still running
    if (running_) {
        stopReceive();
    }
    
    // Clean up resources
    cleanup();
    
    if (logger_) {
        logger_->info("AmdtpReceiver destroyed");
    }
}

void AmdtpReceiver::cleanup() noexcept {
    // Release components in reverse order of creation
    monitoringManager_.reset();
    packetProcessor_.reset();
    transportManager_.reset();
    dclManager_.reset();
    portChannelManager_.reset();
    bufferManager_.reset();

    // Reset placeholder components
    pll_.reset();
    appRingBuffer_.reset();

    // Clear callback data store
    callbackDataStore_.clear();

    initialized_ = false;
    running_ = false;

    if (logger_) {
        logger_->debug("AmdtpReceiver::cleanup: Resources released");
    }
}

std::expected<void, IOKitError> AmdtpReceiver::initialize(IOFireWireLibNubRef interface) {
    if (initialized_) {
        if (logger_) {
            logger_->error("AmdtpReceiver::initialize: Already initialized");
        }
        return std::unexpected(IOKitError::Busy);
    }
    
    if (!interface) {
        if (logger_) {
            logger_->error("AmdtpReceiver::initialize: Interface is null");
        }
        return std::unexpected(IOKitError::BadArgument);
    }
    
    // Set up RunLoop
    runLoopRef_ = CFRunLoopGetCurrent();
    
    if (logger_) {
        logger_->info("AmdtpReceiver::initialize: Using RunLoop={:p}", (void*)runLoopRef_);
    }
    
    // Set up components
    auto result = setupComponents(interface);
    if (!result) {
        cleanup();
        return std::unexpected(result.error());
    }
    
    initialized_ = true;
    
    if (logger_) {
        logger_->info("AmdtpReceiver::initialize: Initialized successfully");
    }
    
    return {};
}

std::expected<void, IOKitError> AmdtpReceiver::setupComponents(IOFireWireLibNubRef interface) {
    if (logger_) {
        logger_->debug("AmdtpReceiver::setupComponents (Kernel Style)");
    }

    // 1. Create IsochBufferManager (using new config)
    bufferManager_ = std::make_unique<IsochBufferManager>(logger_);
    IsochBufferManager::Config bufferConfig = {
        .numGroups = config_.numGroups,
        .packetsPerGroup = config_.packetsPerGroup,
        .packetDataSize = config_.packetDataSize
    };
    auto bufferSetupResult = bufferManager_->setupBuffers(bufferConfig);
    if (!bufferSetupResult) {
         if (logger_) logger_->error("Failed to set up buffers: {}", iokit_error_category().message(static_cast<int>(bufferSetupResult.error())));
        return bufferSetupResult;
    }

    // 2. Create and initialize IsochPortChannelManager (unchanged)
    portChannelManager_ = std::make_unique<IsochPortChannelManager>(logger_, interface, runLoopRef_, false); // false=listener
    auto initPortResult = portChannelManager_->initialize();
     if (!initPortResult) {
         if (logger_) logger_->error("Failed to initialize PortChannelManager: {}", iokit_error_category().message(static_cast<int>(initPortResult.error())));
        return initPortResult;
    }

    // 3. Create IsochDCLManager (using new config)
    IOFireWireLibNuDCLPoolRef nuDCLPool = portChannelManager_->getNuDCLPool();
     if (!nuDCLPool) {
        if (logger_) logger_->error("Failed to get NuDCLPool");
        portChannelManager_->reset();
        return std::unexpected(IOKitError::NotReady);
    }
    IsochDCLManager::Config dclConfig = {
        .numGroups = config_.numGroups,
        .packetsPerGroup = config_.packetsPerGroup,
        .callbackGroupInterval = config_.callbackGroupInterval
    };
    // Pass const ref to bufferManager
    dclManager_ = std::make_unique<IsochDCLManager>(logger_, nuDCLPool, *bufferManager_, dclConfig);

    // 4. Set up DCL callbacks on the DCL Manager (unchanged)
    dclManager_->setDCLCompleteCallback(AmdtpReceiver::handleDCLComplete, this);
    dclManager_->setDCLOverrunCallback(AmdtpReceiver::handleDCLOverrun, this);

    // 5. Create DCL program structure via DCL Manager (unchanged interface)
    auto createDCLResult = dclManager_->createDCLProgram();
     if (!createDCLResult) {
         if (logger_) logger_->error("Failed to create DCL program: {}", iokit_error_category().message(static_cast<int>(createDCLResult.error())));
        portChannelManager_->reset();
        return std::unexpected(createDCLResult.error());
    }
    DCLCommand* dclProgramHandle = createDCLResult.value();

    // 6. Finish Port/Channel setup (unchanged)
    auto setupChannelResult = portChannelManager_->setupLocalPortAndChannel(
        dclProgramHandle,
        bufferManager_->getBufferRange()); // Still pass the full range
     if (!setupChannelResult) {
         if (logger_) logger_->error("Failed to set up Local Port/Channel: {}", iokit_error_category().message(static_cast<int>(setupChannelResult.error())));
        dclManager_->reset();
        portChannelManager_->reset();
        return setupChannelResult;
    }

    // 7. Create IsochTransportManager (unchanged)
    transportManager_ = std::make_unique<IsochTransportManager>(logger_);
    transportManager_->setFinalizeCallback(handleTransportFinalize, this);

    // 8. Create IsochPacketProcessor (updated to use new callback)
    packetProcessor_ = std::make_unique<IsochPacketProcessor>(logger_);
    packetProcessor_->setProcessedDataCallback(AmdtpReceiver::handleProcessedDataStatic, this);
    packetProcessor_->setOverrunCallback(handleDCLOverrun, this);

    // --- Instantiate PLL and Ring Buffer ---
    pll_ = std::make_unique<AudioClockPLL>(logger_);

    // Configure Ring Buffer Size (Example: ~200ms at 48kHz Stereo Float)
    const size_t frameSize = sizeof(ProcessedAudioFrame); // float L, float R, uint64_t ts
    const size_t desiredLatencyMs = 200;
    const size_t sampleRate = 48000; // TODO: Get actual rate from config/FDF later
    const size_t framesForLatency = (sampleRate * desiredLatencyMs) / 1000;
    // Calculate power-of-two size, ensure it's large enough for buffer + safety margin
    const size_t ringBufferSize = framesForLatency * frameSize * 2;
    appRingBuffer_ = std::make_unique<raul::RingBuffer>(ringBufferSize, logger_);
    if (!appRingBuffer_) {
         if (logger_) logger_->error("Failed to create application ring buffer");
         return std::unexpected(IOKitError::NoMemory);
    }
     if (logger_) logger_->info("Application Ring Buffer created with size: {} bytes (for {} frames)",
                              ringBufferSize, ringBufferSize / frameSize);
    // --- End Instantiation ---

    // 9. Create IsochMonitoringManager (unchanged)
    monitoringManager_ = std::make_unique<IsochMonitoringManager>(logger_, runLoopRef_);

    // Attempt initial PLL synchronization
    auto syncResult = synchronizeAndInitializePLL();
    if (!syncResult) {
        // Log warning, but don't fail the whole setup
        // PLL can potentially initialize later when first timestamp arrives
        if (logger_) logger_->warn("Initial PLL synchronization failed: {}",
            iokit_error_category().message(static_cast<int>(syncResult.error())));
    } else {
        if (logger_) logger_->info("Initial PLL synchronization successful");
    }

    if (logger_) logger_->info("AmdtpReceiver::setupComponents: Components set up successfully");
    return {};
}

// ... configure() remains similar, passes speed/channel to portChannelManager_ ...

std::expected<void, IOKitError> AmdtpReceiver::startReceive() {
     // ... (initial checks) ...
     if (!initialized_) {
        if (logger_) logger_->error("AmdtpReceiver::startReceive: Not initialized");
        return std::unexpected(IOKitError::NotReady);
    }
    if (running_) {
        if (logger_) logger_->warn("AmdtpReceiver::startReceive: Already running");
        return {}; // Not an error
    }
     if (!dclManager_ || !portChannelManager_ || !transportManager_) {
        if (logger_) logger_->error("AmdtpReceiver::startReceive: Required components not available");
        return std::unexpected(IOKitError::NotReady);
    }

    // Fix DCL jump targets via DCLManager, passing the Local Port
    IOFireWireLibLocalIsochPortRef localPort = portChannelManager_->getLocalPort();
     if (!localPort) {
        if (logger_) logger_->error("AmdtpReceiver::startReceive: Failed to get Local Port");
        return std::unexpected(IOKitError::NotReady);
    }
    auto fixupResult = dclManager_->fixupDCLJumpTargets(localPort);
     if (!fixupResult) {
        if (logger_) logger_->error("AmdtpReceiver::startReceive: Failed to fix up DCL jump targets: {}", 
            iokit_error_category().message(static_cast<int>(fixupResult.error())));
        return fixupResult;
    }

    // Start transport via Transport Manager, passing the Isoch Channel
    IOFireWireLibIsochChannelRef channelRef = portChannelManager_->getIsochChannel();
      if (!channelRef) {
        if (logger_) logger_->error("AmdtpReceiver::startReceive: Failed to get Isoch Channel");
        return std::unexpected(IOKitError::NotReady);
    }
    auto result = transportManager_->start(channelRef);
     if (!result) {
        if (logger_) logger_->error("AmdtpReceiver::startReceive: Failed to start transport: {}", 
            iokit_error_category().message(static_cast<int>(result.error())));
        return result;
    }

    // Start monitoring if configured
    if (monitoringManager_ && noDataCallback_ && config_.timeout > 0) {
        monitoringManager_->startMonitoring(config_.timeout);
    }

    running_ = true;
    if (logger_) logger_->info("AmdtpReceiver::startReceive: Started receiving (Kernel Style)");
    return {};
}

std::expected<void, IOKitError> AmdtpReceiver::stopReceive() {
    if (!initialized_) {
        if (logger_) { logger_->error("AmdtpReceiver::stopReceive: Not initialized"); }
        return std::unexpected(IOKitError::NotReady);
    }
    if (!running_) {
        if (logger_) { logger_->debug("AmdtpReceiver::stopReceive: Not running"); }
        return {};
    }
    // Check required managers
    if (!transportManager_ || !portChannelManager_) {
        if (logger_) { logger_->error("AmdtpReceiver::stopReceive: Required components not available"); }
        return std::unexpected(IOKitError::NotReady);
    }

    // Stop monitoring
    if (monitoringManager_) {
        monitoringManager_->stopMonitoring();
    }

    // Get the channel from PortChannelManager
    IOFireWireLibIsochChannelRef channelRef = portChannelManager_->getIsochChannel();
    if (!channelRef) {
        if (logger_) { logger_->error("AmdtpReceiver::stopReceive: Failed to get Isoch Channel"); }
        return std::unexpected(IOKitError::NotReady);
    }

    // Stop using TransportManager
    auto result = transportManager_->stop(channelRef);
    if (!result) {
        if (logger_) { 
            logger_->error("AmdtpReceiver::stopReceive: Failed to stop transport: {}", 
                          iokit_error_category().message(static_cast<int>(result.error()))); 
        }
        return result;
    }

    running_ = false;
    if (logger_) { logger_->info("AmdtpReceiver::stopReceive: Stopped receiving"); }
    return {};
}

std::expected<void, IOKitError> AmdtpReceiver::configure(IOFWSpeed speed, uint32_t channel) {
    if (!initialized_) {
        if (logger_) { logger_->error("AmdtpReceiver::configure: Not initialized"); }
        return std::unexpected(IOKitError::NotReady);
    }
    if (running_) {
        if (logger_) { logger_->error("AmdtpReceiver::configure: Cannot configure while running"); }
        return std::unexpected(IOKitError::Busy);
    }
    
    if (!portChannelManager_) {
        if (logger_) { logger_->error("AmdtpReceiver::configure: PortChannelManager not available"); }
        return std::unexpected(IOKitError::NotReady);
    }

    auto result = portChannelManager_->configure(speed, channel);
    if (!result) {
        if (logger_) {
            logger_->error("AmdtpReceiver::configure: Configuration failed: {}",
                         iokit_error_category().message(static_cast<int>(result.error())));
        }
        return result;
    }

    if (logger_) {
        logger_->info("AmdtpReceiver::configure: Configured with speed={}, channel={}",
                     static_cast<int>(speed), channel);
    }
    return {};
}

// REMOVE old setPacketCallback method
// void AmdtpReceiver::setPacketCallback(PacketCallback callback, void* refCon) { ... }

// ADD new setProcessedDataCallback method
void AmdtpReceiver::setProcessedDataCallback(ProcessedDataCallback callback, void* refCon) {
     if (!packetProcessor_) {
        if (logger_) logger_->error("AmdtpReceiver::setProcessedDataCallback: PacketProcessor not available");
        return;
    }
    // Store client's callback info
    processedDataCallback_ = callback;
    processedDataCallbackRefCon_ = refCon;

    // Note: The callback on the processor is already set during setupComponents
    // and points to our static handler with 'this' as refCon.
    // We store the client's callback here to be called by our handler.

    if (logger_) logger_->debug("AmdtpReceiver::setProcessedDataCallback: Stored client callback={:p}, refCon={:p}",
                     (void*)callback, refCon);
}

void AmdtpReceiver::setMessageCallback(MessageCallback callback, void* refCon) {
    messageCallback_ = callback;
    messageCallbackRefCon_ = refCon;
    
    if (logger_) {
        logger_->debug("AmdtpReceiver::setMessageCallback: Set callback={:p}, refCon={:p}",
                     (void*)callback, refCon);
    }
}

void AmdtpReceiver::setStructuredCallback(StructuredDataCallback callback, void* refCon) {
    structuredCallback_ = callback;
    structuredCallbackRefCon_ = refCon;
    
    // Create a CallbackData record for forwarding properly
    auto callbackData = std::make_unique<CallbackData>();
    callbackData->receiver = this;
    callbackData->clientRefCon = refCon;
    callbackDataStore_.push_back(std::move(callbackData));
    
    if (logger_) {
        logger_->debug("AmdtpReceiver::setStructuredCallback: Set callback={:p}, refCon={:p}",
                     (void*)callback, refCon);
    }
}

void AmdtpReceiver::setNoDataCallback(NoDataCallback callback, void* refCon, 
                                      uint32_t timeout, bool cipOnlyMode) {
    noDataCallback_ = callback;
    noDataCallbackRefCon_ = refCon;
    
    // Create a CallbackData record for forwarding properly
    auto callbackData = std::make_unique<CallbackData>();
    callbackData->receiver = this;
    callbackData->clientRefCon = refCon;
    callbackDataStore_.push_back(std::move(callbackData));
    
    // Configure the monitoring manager with the callback
    if (monitoringManager_ && callback) {
        config_.timeout = timeout; // Store in config for potential restarts
        monitoringManager_->setNoDataCallback(
            &AmdtpReceiver::handleNoDataCallback,
            callbackDataStore_.back().get()
        );
        
        if (running_) {
            // If we're already running, start monitoring now
            monitoringManager_->startMonitoring(timeout);
        }
    }
    
    if (logger_) {
        logger_->debug("AmdtpReceiver::setNoDataCallback: Set callback={:p}, refCon={:p}, timeout={}ms, cipOnly={}",
                     (void*)callback, refCon, timeout, cipOnlyMode);
    }
}

void AmdtpReceiver::setGroupCompletionCallback(GroupCompletionCallback callback, void* refCon) {
    groupCompletionCallback_ = callback;
    groupCompletionRefCon_ = refCon;
    
    if (logger_) {
        logger_->debug("AmdtpReceiver::setGroupCompletionCallback: Set callback={:p}, refCon={:p}",
                    (void*)callback, refCon);
    }
}

// --- Implement New Handlers ---

// Static handler called by IsochPacketProcessor
void AmdtpReceiver::handleProcessedDataStatic(const std::vector<ProcessedSample>& samples,
                                             const PacketTimingInfo& timing,
                                             void* refCon) {
    auto receiver = static_cast<AmdtpReceiver*>(refCon);
    if (receiver) {
        receiver->handleProcessedData(samples, timing);
    }
}

// Instance method doing the work for Phase 2 (PLL/RingBuffer)
void AmdtpReceiver::handleProcessedData(const std::vector<ProcessedSample>& samples,
                                       const PacketTimingInfo& timing) {

    if (!pll_ || !appRingBuffer_) {
        if (logger_) logger_->error("handleProcessedData called before PLL/RingBuffer initialization!");
        return;
    }

    // Get current host time *once* in absolute units
    uint64_t nowHostTimeAbs = mach_absolute_time();

    // Update the PLL state. It will internally handle initialization check.
    pll_->update(timing, nowHostTimeAbs); // Pass absolute time

    // --- Write processed samples to the application ring buffer ---
    if (!samples.empty()) {
        // Only proceed if PLL is initialized and ready to provide timestamps
        if (pll_->isInitialized()) {
            if (logger_) logger_->trace("Writing {} samples to App Ring Buffer. First AbsIdx: {}", samples.size(), samples[0].absoluteSampleIndex);

            ProcessedAudioFrame frame; // Reuse frame struct
            for (const auto& sample : samples) {
                // Calculate presentation time using the PLL
                // This now happens *inside* the loop for potentially better accuracy per frame
                frame.presentationNanos = pll_->getPresentationTimeNs(sample.absoluteSampleIndex);

                // Check for valid timestamp (e.g., PLL might return 0 if it can't estimate yet)
                if (frame.presentationNanos == 0) {
                     if (logger_) logger_->warn("PLL returned 0 presentation time for sample index {}, skipping frame.", sample.absoluteSampleIndex);
                     continue; // Skip writing this frame
                }

                // Assemble the rest of the frame
                frame.sampleL = sample.sampleL;
                frame.sampleR = sample.sampleR;

                // Write frame to appRingBuffer_
                size_t written = appRingBuffer_->write(sizeof(frame), &frame);
                if (written != sizeof(frame)) {
                     if (logger_) logger_->error("Failed to write complete frame (AbsIdx {}) to ring buffer! Buffer full?", sample.absoluteSampleIndex);
                     // Handle ring buffer full - break is reasonable for now
                     break;
                }
            }
             if (logger_ && !samples.empty()) logger_->trace("Finished writing samples. Last AbsIdx: {}", samples.back().absoluteSampleIndex);

        } else {
            // PLL not ready, drop samples for this packet
             if (logger_) logger_->warn("PLL not initialized, dropping {} samples for packet with FW TS {}", samples.size(), timing.fwTimestamp);
        }
    } else if (timing.numSamplesInPacket == 0 && timing.fdf != 0xFF) {
        // Handle case where processor signaled discontinuity via empty vector
         if (logger_) logger_->warn("handleProcessedData received empty sample vector (DBC discontinuity?). FW TS: {}, SYT: {:#06x}, DBC: {}",
                      timing.fwTimestamp, timing.syt, timing.firstDBC);
    }

    // --- Client Callback (DEPRECATE / REMOVE LATER) ---
    // This should eventually be removed. The client (ASP via XPC)
    // will read directly from the ring buffer.
     if (processedDataCallback_) {
         // Pass empty vector if samples were dropped due to PLL init state?
         // Or pass original samples? Let's pass original for now.
         processedDataCallback_(samples, timing, processedDataCallbackRefCon_);
     }
     // --- End Client Callback ---
}

void AmdtpReceiver::handleBufferGroupComplete(uint32_t groupIndex) {
    if (!running_) return;
    if (!bufferManager_ || !packetProcessor_) {
        if (logger_) logger_->error("handleBufferGroupComplete: Required components missing");
        notifyMessage(static_cast<uint32_t>(ReceiverMessage::BufferError));
        return;
    }

    // Log which group completed
    // logger_->info("*** PROCESSING GROUP {} ***", groupIndex);

    const uint32_t packetsInGroup = bufferManager_->getPacketsPerGroup();
    const size_t expectedTotalPacketSize = bufferManager_->getTotalPacketSize(); // Get expected total packet size

    // Process all packets within this completed group
    for (uint32_t packetIdx = 0; packetIdx < packetsInGroup && running_; ++packetIdx) {
        // --- Get Raw Packet Pointer and Log Raw Data ---
        auto rawPacketPtrExp = bufferManager_->getRawPacketSlotPtr(groupIndex, packetIdx);
        auto tsPtrExp = bufferManager_->getPacketTimestampPtr(groupIndex, packetIdx);

        if (!rawPacketPtrExp || !tsPtrExp) {
            logger_->error("Failed to get raw pointers for G:{} P:{}", groupIndex, packetIdx);
            continue;
        }

        uint8_t* rawPacketPtr = rawPacketPtrExp.value();
        uint32_t timestamp = *tsPtrExp.value(); // Get timestamp

        // Determine how much data to dump (avoid reading past buffer end if possible)
        size_t dumpSize = std::min((size_t)80, expectedTotalPacketSize); // Dump up to 80 bytes or expected size

        std::vector<uint8_t> raw_packet_bytes(dumpSize);
        std::memcpy(raw_packet_bytes.data(), rawPacketPtr, dumpSize);

//        logger_->debug("Raw Packet G:{} P:{} @ {:p} (Expected Size: {}):",
//                      groupIndex, packetIdx, (void*)rawPacketPtr, expectedTotalPacketSize);
//        logger_->debug("  Hex Dump ({} bytes): {}", dumpSize, spdlog::to_hex(raw_packet_bytes));
//        // --- End Raw Logging ---

        // --- Now, get the separated pointers as before for processing ---
        auto isochHdrPtrExp = bufferManager_->getPacketIsochHeaderPtr(groupIndex, packetIdx);
        auto cipHdrPtrExp = bufferManager_->getPacketCIPHeaderPtr(groupIndex, packetIdx);
        auto dataPtrExp = bufferManager_->getPacketDataPtr(groupIndex, packetIdx);

        if (!isochHdrPtrExp || !cipHdrPtrExp || !dataPtrExp) {
            logger_->error("Failed to get separated pointers for G:{} P:{}", groupIndex, packetIdx);
            notifyMessage(static_cast<uint32_t>(ReceiverMessage::BufferError));
            continue; // Skip this packet
        }

        // Extract pointers after validation
        auto isochHdrPtr = isochHdrPtrExp.value();
        auto cipHdrPtr = cipHdrPtrExp.value();
        auto dataPtr = dataPtrExp.value();
        uint32_t dataSize = bufferManager_->getPacketDataSize();

        // Process packet with the separated data
        auto procResult = packetProcessor_->processPacket(
            groupIndex, packetIdx,
            isochHdrPtr, 
            cipHdrPtr,
            dataPtr, 
            dataSize,
            timestamp);

        if (!procResult) {
            logger_->error("Failed to process packet G:{} P:{}: {}", 
                         groupIndex, packetIdx, 
                         static_cast<int>(procResult.error()));
            notifyMessage(static_cast<uint32_t>(ReceiverMessage::PacketError));
        }
    }

    // Reset the no-data timer after processing the group
    if (monitoringManager_) {
        monitoringManager_->resetTimer();
        monitoringManager_->updateLastCycle(groupIndex); // Use groupIndex as a proxy for 'cycle'
    }

    // Call group completion callback if registered
    if (groupCompletionCallback_) {
        // Get timestamp for the first packet in group as representative timestamp
        auto tsPtrExp = bufferManager_->getPacketTimestampPtr(groupIndex, 0);
        uint32_t timestamp = tsPtrExp ? *tsPtrExp.value() : 0;
        
        groupCompletionCallback_(groupIndex, timestamp, groupCompletionRefCon_);
    }

    // logger_->info("*** COMPLETED GROUP {} ***", groupIndex);
}

// --- Static Callbacks ---
void AmdtpReceiver::handleDCLComplete(uint32_t groupIndex, void* refCon) {
    // refCon is AmdtpReceiver*
    auto receiver = static_cast<AmdtpReceiver*>(refCon);
    if (receiver) {
        // Call the new instance method
        receiver->handleBufferGroupComplete(groupIndex);
    }
}

void AmdtpReceiver::handleDCLOverrun(void* refCon) {
    auto receiver = static_cast<AmdtpReceiver*>(refCon);
    if (receiver) {
        receiver->handleOverrun();
    }
}

void AmdtpReceiver::handleTransportFinalize(void* refCon) {
    auto receiver = static_cast<AmdtpReceiver*>(refCon);
    if (receiver) {
        // Handle transport finalization
        receiver->notifyMessage(0, 0, 0); // Could use a specific message code
    }
}

void AmdtpReceiver::handleOverrun() {
    if (!running_) {
        return;
    }
    
    // Attempt to recover from the overrun
    auto result = handleOverrunRecovery();
    if (!result) {
        logger_->error("AmdtpReceiver::handleOverrun: Failed to recover from overrun: {}", 
                     static_cast<int>(result.error()));
        
        // If recovery failed, notify the client and stop
        notifyMessage(static_cast<uint32_t>(ReceiverMessage::OverrunError));
        stopReceive();
    }
}

std::expected<void, IOKitError> AmdtpReceiver::handleOverrunRecovery() {
    if (!running_) { return {}; }

    if (logger_) { logger_->warn("AmdtpReceiver::handleOverrunRecovery: Attempting recovery"); }

    // Notify client
    notifyMessage(static_cast<uint32_t>(ReceiverMessage::OverrunError));

    // Get channel from PortChannelManager
    auto channelRef = portChannelManager_->getIsochChannel();
    if (!channelRef) {
        logger_->error("AmdtpReceiver::handleOverrunRecovery: No active channel");
        return std::unexpected(IOKitError::NotReady);
    }

    // Stop the channel 
    IOReturn io_result = (*channelRef)->Stop(channelRef);
    if (io_result != kIOReturnSuccess) {
        logger_->error("AmdtpReceiver::handleOverrunRecovery: Failed to stop channel: 0x{:08X}", io_result);
    }

    // Release the channel
    (*channelRef)->ReleaseChannel(channelRef);

    // Fix up DCL jump targets
    IOFireWireLibLocalIsochPortRef localPort = portChannelManager_->getLocalPort();
    if (!localPort) {
        if (logger_) { logger_->error("AmdtpReceiver::handleOverrunRecovery: Failed to get Local Port for fixup"); }
        running_ = false; 
        return std::unexpected(IOKitError::NotReady);
    }
    auto fixupResult = dclManager_->fixupDCLJumpTargets(localPort);
    if (!fixupResult) {
        logger_->error("AmdtpReceiver::handleOverrunRecovery: Failed to fix up DCL jump targets: {}",
                    iokit_error_category().message(static_cast<int>(fixupResult.error())));
        running_ = false; 
        return fixupResult;
    }

    // Re-allocate the channel
    io_result = (*channelRef)->AllocateChannel(channelRef);
    if (io_result != kIOReturnSuccess) {
        logger_->error("AmdtpReceiver::handleOverrunRecovery: Failed to reallocate channel: 0x{:08X}", io_result);
        running_ = false;
        return std::unexpected(IOKitError(io_result));
    }

    // Re-start the channel
    io_result = (*channelRef)->Start(channelRef);
    if (io_result != kIOReturnSuccess) {
        logger_->error("AmdtpReceiver::handleOverrunRecovery: Failed to restart channel: 0x{:08X}", io_result);
        (*channelRef)->ReleaseChannel(channelRef);
        running_ = false;
        return std::unexpected(IOKitError(io_result));
    }

    // Reset monitor timer
    if (monitoringManager_) {
        monitoringManager_->resetTimer();
    }

    logger_->info("AmdtpReceiver::handleOverrunRecovery: Successfully recovered");
    return {};
}

void AmdtpReceiver::notifyMessage(uint32_t msg, uint32_t param1, uint32_t param2) {
    if (messageCallback_) {
        messageCallback_(msg, param1, param2, messageCallbackRefCon_);
    }
}

void AmdtpReceiver::handleStructuredCallback(const ReceivedCycleData& data, void* refCon) {
    auto* callbackData = static_cast<CallbackData*>(refCon);
    if (callbackData && callbackData->receiver && callbackData->receiver->structuredCallback_) {
        ReceivedCycleData modifiedData = data;
        modifiedData.refCon = callbackData->clientRefCon;
        callbackData->receiver->structuredCallback_(modifiedData, callbackData->clientRefCon);
    }
}

void AmdtpReceiver::handleNoDataCallback(uint32_t lastCycle, void* refCon) {
    auto* callbackData = static_cast<CallbackData*>(refCon);
    if (callbackData && callbackData->receiver && callbackData->receiver->noDataCallback_) {
        callbackData->receiver->noDataCallback_(lastCycle, callbackData->clientRefCon);
    }
}

std::expected<void, IOKitError> AmdtpReceiver::synchronizeAndInitializePLL() {
    if (!portChannelManager_ || !pll_) {
        if (logger_) logger_->error("AmdtpReceiver::synchronizeAndInitializePLL: Required components missing");
        return std::unexpected(IOKitError::NotReady);
    }

    IOFireWireLibNubRef nubInterface = portChannelManager_->getNubInterface();
    if (!nubInterface) {
        if (logger_) logger_->error("AmdtpReceiver::synchronizeAndInitializePLL: Failed to get nub interface");
        return std::unexpected(IOKitError::NotReady);
    }

    UInt32 fwCycleTime = 0;
    UInt64 hostUptimeAbs = 0; // Get host time in absolute units

    // Use the direct correlation method to get synchronized timestamps
    IOReturn result = (*nubInterface)->GetCycleTimeAndUpTime(nubInterface, &fwCycleTime, &hostUptimeAbs);

    if (result != kIOReturnSuccess) {
        if (logger_) logger_->error("AmdtpReceiver::synchronizeAndInitializePLL: Failed to get CycleTime and UpTime: 0x{:08X}", result);
        return std::unexpected(IOKitError(result));
    }

    if (logger_) logger_->info("PLL Sync Point: FW CycleTime={:#010x}, Host UptimeAbs={}", 
                             fwCycleTime, hostUptimeAbs);

    // Initialize the PLL with this correlation point (absolute time)
    pll_->initialize(hostUptimeAbs, fwCycleTime);

    return {};
}

raul::RingBuffer* AmdtpReceiver::getAppRingBuffer() const {
    return appRingBuffer_.get(); // Simply return the raw pointer from unique_ptr
}


} // namespace Isoch
} // namespace FWA



