#include "Isoch/IsoStreamHandler.hpp"
#include <cstdio>
#include <cmath>
#include <unistd.h>
#include <iostream>
#include <spdlog/spdlog.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <mach/mach.h>
#include <pthread.h>
#include "Isoch/utils/RunLoopHelper.hpp"
#include <fstream> // For file output
#include <string>  // For filename
#include <vector>  // To buffer samples before writing
#include <chrono>  // For sleep_for & time points
#include <ctime>   // For std::time
#include "Isoch/interfaces/ITransmitPacketProvider.hpp" // Include for the packet provider interface

// Define stream direction enable/disable flags
#define RECEIVE 0  // Set to 0 to disable receiver (input stream)
#define TRANSMIT 1 // Set to 0 to disable transmitter (output stream)

// Define RECORD macro - Set to 0 to disable recording
#define RECORD 0

namespace FWA {

IsoStreamHandler::IsoStreamHandler(std::shared_ptr<AudioDevice> device,
                                   std::shared_ptr<spdlog::logger> logger,
                                   std::shared_ptr<CommandInterface> commandInterface,
                                   IOFireWireLibDeviceRef interface)
: m_audioDevice(std::move(device))
, m_logger(std::move(logger))
, m_commandInterface(std::move(commandInterface))
, m_lastTimestamp(std::chrono::steady_clock::now())
, m_lastTransmitterTimestamp(std::chrono::steady_clock::now())
, m_interface(interface)
, m_processingRunning(false)
, m_inputStream(nullptr) // Initialize stream pointers
, m_outputStream(nullptr)
#ifdef __OBJC__
, m_xpcClient(nil)
#endif
{
    if (!m_logger) {
        m_logger = spdlog::default_logger();
    }

    m_logger->info("IsoStreamHandler: Initialized");

    UInt32 outGeneration = 0;

    // Get the generation count for the device
    auto result = (*interface)->GetBusGeneration(interface, &outGeneration);

    UInt16 nodeId = 0;
    result = (*interface)->GetRemoteNodeID(interface, outGeneration, &nodeId);
    if (result != kIOReturnSuccess) {
        m_logger->error("IsoStreamHandler: Failed to get node ID");
    } else {
        m_nodeId = nodeId;
        m_logger->info("IsoStreamHandler: Node ID: {}", m_nodeId);
    }

    // --- REMOVE Obsolete XPC Initialization ---
    m_logger->info("IsoStreamHandler: Constructor finished (obsolete XPC init removed).");
    // --- End Removal ---
}

IsoStreamHandler::~IsoStreamHandler() {
    // Ensure stop is called which should handle ShmIsochBridge::stop()
    stop();

#ifdef __OBJC__
    @autoreleasepool {
        if (m_xpcClient) {
            // [m_xpcClient release]; // ARC will handle this if it's an ARC-managed object
            m_xpcClient = nil;
        }
    }
#endif

    m_logger->info("IsoStreamHandler: Destroyed");
}

std::expected<void, IOKitError> IsoStreamHandler::start() {
    std::lock_guard<std::mutex> lock(m_streamMutex);
    m_logger->info("IsoStreamHandler: Starting streams (RECEIVE={}, TRANSMIT={})", RECEIVE, TRANSMIT);

    // Update bus generation
    UInt32 outGeneration = 0;

    m_logger->debug("IsoStreamHandler: Getting bus generation");
    auto result = (*m_interface)->GetBusGeneration(m_interface, &outGeneration);

    if (result != kIOReturnSuccess) {
        m_logger->error("IsoStreamHandler: Failed to get bus generation");
        return std::unexpected(IOKitError::InternalError);
    }

    // Get FW speed
    m_logger->debug("IsoStreamHandler: Getting speed");
    result = (*m_interface)->GetSpeedToNode(m_interface, outGeneration, &m_speed);

    if (result != kIOReturnSuccess) {
        // Set minimum speed as fallback
        m_speed = kFWSpeed100MBit;
        m_logger->warn("IsoStreamHandler: Failed to get speed, defaulting to 100Mbps");
    }

    switch (m_speed) {
        case kFWSpeed100MBit: m_logger->info("IsoStreamHandler: Speed: 100Mbps"); break;
        case kFWSpeed200MBit: m_logger->info("IsoStreamHandler: Speed: 200Mbps"); break;
        case kFWSpeed400MBit: m_logger->info("IsoStreamHandler: Speed: 400Mbps"); break;
        case kFWSpeed800MBit: m_logger->info("IsoStreamHandler: Speed: 800Mbps"); break;
        default: m_logger->info("IsoStreamHandler: Unknown speed ({}), defaulting to 100Mbps", static_cast<int>(m_speed)); m_speed = kFWSpeed100MBit; break;
    }

#if RECEIVE
    // --- Create Input Stream (Receiver) ---
    // Common parameters (can be adjusted)
    const unsigned int rxPacketsPerGroup = 16;
    const unsigned int rxNumGroups = 2;
    const unsigned int rxPacketDataSize = 64; // Expected audio payload size per packet

    m_logger->info("IsoStreamHandler: Creating receiver stream with: packetsPerGroup={}, numGroups={}, packetDataSize={}",
                   rxPacketsPerGroup, rxNumGroups, rxPacketDataSize);

    // Assume device OUTPUT plug 0 for receiving audio FROM device
    uint8_t receivePlugNum = 0;

    auto inputStreamResult = AudioDeviceStream::createReceiverForDevicePlug(
        m_audioDevice,
        receivePlugNum,
        &IsoStreamHandler::handleDataPush, // Static callback
        this, // RefCon for data callback is 'this' pointer
        &IsoStreamHandler::handleMessage,  // Static callback
        this, // RefCon for message callback is 'this' pointer
        m_logger,
        rxPacketsPerGroup, // cyclesPerSegment equivalent
        rxNumGroups,       // numSegments equivalent
        rxPacketDataSize,  // Size of each *packet's* data area
        m_interface
    );

    if (!inputStreamResult) {
        m_logger->error("IsoStreamHandler: Failed to create input stream: {}",
                       iokit_error_category().message(static_cast<int>(inputStreamResult.error())));
        return std::unexpected(inputStreamResult.error());
    }

    m_inputStream = inputStreamResult.value();
    m_logger->info("IsoStreamHandler: Input stream created successfully");

    // Configure the input stream speed
    auto rxSpeedResult = m_inputStream->setIsochSpeed(m_speed);
    if (!rxSpeedResult) {
        m_logger->error("IsoStreamHandler: Failed to set input stream speed: {}",
                       iokit_error_category().message(static_cast<int>(rxSpeedResult.error())));
        m_inputStream.reset(); // Clean up partially configured stream
        return std::unexpected(rxSpeedResult.error());
    }

    // Start the input stream
    auto rxStartResult = m_inputStream->start();
    if (!rxStartResult) {
        m_logger->error("IsoStreamHandler: Failed to start input stream: {}",
                       iokit_error_category().message(static_cast<int>(rxStartResult.error())));
        m_inputStream.reset(); // Clean up
        return std::unexpected(rxStartResult.error());
    }
    m_logger->info("IsoStreamHandler: Input stream started");
#else // RECEIVE == 0
    m_logger->info("IsoStreamHandler: Receiver disabled by build configuration.");
#endif // RECEIVE


#if TRANSMIT
    // --- Create Output Stream (Transmitter) ---
    m_logger->info("IsoStreamHandler: Creating transmitter stream...");

    // isochronous parameters for transmitter
    // 256 callbacks per second
    const unsigned int txPacketsPerGroup = 32;      
    const unsigned int txNumGroups = 8;            
    const unsigned int txPacketDataSize = 64; // Audio payload size per packet
    unsigned int transmitProviderBufferSize = 2 * txNumGroups * txPacketsPerGroup * txPacketDataSize; // Example buffer size

    m_logger->info("IsoStreamHandler: Creating transmitter stream with: packetsPerGroup={}, numGroups={}, transmitProviderBufferSize={}",
                   txPacketsPerGroup, txNumGroups, transmitProviderBufferSize);

    // --- REMOVED check for m_amdtpTxProcessor ---
    // The check for m_amdtpTxProcessor has been removed as it's no longer a member variable
    // We don't need to check for this processor anymore before creating the output stream

    // Assume device INPUT plug 0 for transmitting audio TO device
    uint8_t transmitPlugNum = 0;

    auto outputStreamResult = AudioDeviceStream::createTransmitterForDevicePlug(
        m_audioDevice,
        transmitPlugNum,
        nullptr, // No data pull callback needed from handler side
        nullptr, // Refcon for pull callback
        &IsoStreamHandler::handleMessage, // Reuse message handler for errors/status
        this, // RefCon for message handler
        m_logger,
        txPacketsPerGroup, // Use same cycle/segment config for DCL structure
        txNumGroups,
        transmitProviderBufferSize, // Buffer size for the internal provider
        m_interface
    );
    // If you want to set transmissionType here, modify AudioDeviceStream::createTransmitterForDevicePlug to accept and forward it.

    if (!outputStreamResult) {
        m_logger->error("IsoStreamHandler: Failed to create output stream: {}",
                       iokit_error_category().message(static_cast<int>(outputStreamResult.error())));
        // Clean up input stream if output stream creation fails
#if RECEIVE
        if (m_inputStream) m_inputStream->stop();
        m_inputStream.reset();
#endif
        return std::unexpected(outputStreamResult.error());
    }

    m_outputStream = outputStreamResult.value();
    m_logger->info("IsoStreamHandler: Output stream created successfully");

    // Configure the output stream speed
    auto txSpeedResult = m_outputStream->setIsochSpeed(m_speed);
    if (!txSpeedResult) {
        m_logger->error("IsoStreamHandler: Failed to set output stream speed: {}",
                       iokit_error_category().message(static_cast<int>(txSpeedResult.error())));
#if RECEIVE
        if (m_inputStream) m_inputStream->stop(); m_inputStream.reset();
#endif
        m_outputStream.reset(); // Clean up output stream too
        return std::unexpected(txSpeedResult.error());
    }

    // Start the output stream
    auto txStartResult = m_outputStream->start();
    if (!txStartResult) {
        m_logger->error("IsoStreamHandler: Failed to start output stream: {}",
                       iokit_error_category().message(static_cast<int>(txStartResult.error())));
#if RECEIVE
        if (m_inputStream) m_inputStream->stop(); m_inputStream.reset();
#endif
        m_outputStream.reset();
        return std::unexpected(txStartResult.error());
    }
    m_logger->info("IsoStreamHandler: Output stream started");

    // --- +++ Start ShmIsochBridge AFTER output stream is started +++ ---
    m_logger->info("IsoStreamHandler: Starting ShmIsochBridge...");
    Isoch::ITransmitPacketProvider* provider = getTransmitPacketProvider();
    if (provider) {
        // ShmIsochBridge::instance().start(provider); // Pass the provider pointer
        // We are using RingBufferManager instead of ShmIsochBridge
        // RingBufferManager::instance().setPacketProvider(provider);
        m_logger->warn("IsoStreamHandler: THIS SHOULD NOT HAPPEN! ShmIsochBridge is not used anymore, RingBufferManager is used instead.");
    } else {
        m_logger->error("IsoStreamHandler: Failed to get Packet Provider from output stream! RingBufferManager NOT started.");
        // Decide if this is fatal? Probably should be.
#if RECEIVE
        if (m_inputStream) m_inputStream->stop(); m_inputStream.reset();
#endif
        if (m_outputStream) m_outputStream->stop(); m_outputStream.reset();
        return std::unexpected(IOKitError::NotReady); // Indicate SHM bridge failed to start
    }
    // --- +++ End ShmIsochBridge Start +++ ---

#else // TRANSMIT == 0
    m_logger->info("IsoStreamHandler: Transmitter disabled by build configuration.");
#endif // TRANSMIT


    // --- Start Background Threads ---
#if RECEIVE
    // Start the consumer thread AFTER input stream is started and buffer is available
    if (getInputStreamRingBuffer()) {
        m_consumerRunning = true;
        m_consumerThread = std::thread(&IsoStreamHandler::consumerLoop, this);
        makeThreadRealtime(m_consumerThread); // Make consumer high priority
        m_logger->info("IsoStreamHandler: Ring buffer consumer thread started.");
    } else {
        m_logger->error("IsoStreamHandler: Cannot start consumer thread, input stream ring buffer is null.");
        // Stop streams if we can't start the consumer
        if (m_inputStream) m_inputStream->stop(); m_inputStream.reset();
#if TRANSMIT
        if (m_outputStream) {
            ShmIsochBridge::instance().stop(); // Stop bridge first
            m_outputStream->stop();
            m_outputStream.reset();
        }
#endif
        return std::unexpected(IOKitError::NotReady);
    }
#endif // RECEIVE

    // Start the data processing thread (potentially common logic or adjusted based on defines)
    m_processingRunning = true;
    m_processingThread = std::thread(&IsoStreamHandler::processData, this);
    makeThreadRealtime(m_processingThread);
    m_logger->info("IsoStreamHandler: Data processing thread started");

    m_logger->info("IsoStreamHandler: Start sequence completed.");
    return {};
}

void IsoStreamHandler::stop() {
    std::lock_guard<std::mutex> lock(m_streamMutex);
    m_logger->info("IsoStreamHandler: Stopping streams...");

#if RECEIVE
    // Stop the consumer thread FIRST if receiver was enabled
    if (m_consumerRunning) {
        m_consumerRunning = false; // Signal the loop to stop
        if (m_consumerThread.joinable()) {
            m_consumerThread.join(); // Wait for it to finish
            m_logger->info("IsoStreamHandler: Ring buffer consumer thread stopped.");
        }
    }
#endif // RECEIVE

    // Stop the data processing thread
    if (m_processingRunning) {
        m_processingRunning = false;
        if (m_processingThread.joinable()) {
            m_processingThread.join();
            m_logger->info("IsoStreamHandler: Data processing thread stopped");
        }
    }

    // --- +++ Stop ShmIsochBridge BEFORE stopping output stream +++ ---
#if TRANSMIT
    if (m_outputStream) { // Only stop bridge if transmitter was active
        //  m_logger->info("IsoStreamHandler: Stopping ShmIsochBridge...");
        //  ShmIsochBridge::instance().stop();
        //  m_logger->info("IsoStreamHandler: ShmIsochBridge stopped.");
    }
#endif
    // --- +++ End ShmIsochBridge Stop +++ ---


#if RECEIVE
    // Stop input stream if active
    if (m_inputStream) {
        auto result = m_inputStream->stop();
        if (!result) {
            m_logger->error("IsoStreamHandler: Error stopping input stream: {}",
                           iokit_error_category().message(static_cast<int>(result.error())));
        } else {
            m_logger->info("IsoStreamHandler: Input stream stopped");
        }
        m_inputStream.reset();
    }
#endif // RECEIVE

#if TRANSMIT
    // Stop output stream if active
    if (m_outputStream) {
        auto result = m_outputStream->stop();
        if (!result) {
            m_logger->error("IsoStreamHandler: Error stopping output stream: {}",
                           iokit_error_category().message(static_cast<int>(result.error())));
        } else {
            m_logger->info("IsoStreamHandler: Output stream stopped");
        }
        m_outputStream.reset();
    }
#endif // TRANSMIT

    // Release direct receiver reference if it was ever assigned
    m_directReceiver.reset();
    m_logger->info("IsoStreamHandler: All active streams stopped.");
}

// --- UPDATED getTransmitPacketProvider ---
Isoch::ITransmitPacketProvider* IsoStreamHandler::getTransmitPacketProvider() {
    if (m_outputStream) {
       // Delegate to AudioDeviceStream's getter
       return m_outputStream->getTransmitPacketProvider();
    }
    m_logger->warn("getTransmitPacketProvider: Output stream not available.");
    return nullptr;
}
// --- END getTransmitPacketProvider ---


// Static callback functions that forward to instance methods
void IsoStreamHandler::handleDataPush(const uint8_t* pPayload, size_t payloadLength, void* refCon) {
    // Log callback using RunLoopHelper
//    logCallbackThreadInfo("IsoStreamHandler", "handleDataPush", refCon);

    // Get the IsoStreamHandler instance from the refcon
    auto handler = static_cast<IsoStreamHandler*>(refCon);
    if (handler) {
        handler->handleDataPushImpl(pPayload, payloadLength);
    }
}

void IsoStreamHandler::handleMessage(uint32_t msg, uint32_t param1, uint32_t param2, void* refCon) {
    // Get the IsoStreamHandler instance from the refcon
    auto handler = static_cast<IsoStreamHandler*>(refCon);
    if (handler) {
        handler->handleMessageImpl(msg, param1, param2);
    }
}

void IsoStreamHandler::handleNoData(uint32_t lastCycle, void* refCon) {
    // Get the IsoStreamHandler instance from the refcon
    auto handler = static_cast<IsoStreamHandler*>(refCon);
    if (handler) {
        handler->handleNoDataImpl(lastCycle);
    }
}

void IsoStreamHandler::handleStructuredData(const Isoch::ReceivedCycleData& data, void* refCon) {
    // Get the IsoStreamHandler instance from the refcon
    auto handler = static_cast<IsoStreamHandler*>(refCon);
    if (handler) {
        handler->handleStructuredDataImpl(data);
    }
}

// Instance methods that handle the actual work
void IsoStreamHandler::handleDataPushImpl(const uint8_t* pPayload, size_t payloadLength) {
    // This callback is now mostly informational for legacy purposes.
    // The actual data processing happens in the consumerLoop reading the ring buffer.

    // Track packet rate (can keep this)
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastTimestamp).count();
    m_packetCounter++;
    if (elapsed >= 5) { // Log less frequently
        m_logger->debug("IsoStreamHandler: Received ~{} packets/sec (legacy callback trigger)", m_packetCounter/elapsed);
        m_packetCounter = 0;
        m_lastTimestamp = now;
    }

    // NOTE: Do NOT process pPayload here. It might be null or invalid
    // if the underlying callback (handleProcessedDataStatic in AudioDeviceStream)
    // is just signalling data arrival. Data should be read from the ring buffer.
}

void IsoStreamHandler::handleMessageImpl(uint32_t msg, uint32_t param1, uint32_t param2) {
    // Log callback using RunLoopHelper
//    logCallbackThreadInfo("IsoStreamHandler", "handleMessageImpl", this);

    // Check if message is from Receiver or Transmitter range
    if (msg >= static_cast<uint32_t>(Isoch::ReceiverMessage::BufferError) && msg <= static_cast<uint32_t>(Isoch::ReceiverMessage::DBCDiscontinuity)) {
        auto rxMsg = static_cast<Isoch::ReceiverMessage>(msg);
//        m_logger->info("IsoStreamHandler: Received RX message {:#x}, p1={}, p2={}", msg, param1, param2);
        switch (rxMsg) {
            case Isoch::ReceiverMessage::BufferError: m_logger->error("IsoStreamHandler: AMDTP RX Buffer error occurred"); break;
            case Isoch::ReceiverMessage::PacketError: m_logger->error("IsoStreamHandler: AMDTP RX Packet error occurred"); break;
            case Isoch::ReceiverMessage::OverrunError: m_logger->error("IsoStreamHandler: AMDTP RX Overrun error occurred"); break;
            case Isoch::ReceiverMessage::GroupError: m_logger->error("IsoStreamHandler: AMDTP RX Group error occurred"); break;
            case Isoch::ReceiverMessage::NoDataTimeout: m_logger->error("IsoStreamHandler: AMDTP RX No data timeout, last cycle: {}", param1); handleNoDataImpl(param1); break;
            case Isoch::ReceiverMessage::DBCDiscontinuity: m_logger->warn("IsoStreamHandler: AMDTP RX DBC Discontinuity detected"); break;
        }
    } else if (msg >= static_cast<uint32_t>(Isoch::TransmitterMessage::StreamStarted) && msg <= static_cast<uint32_t>(Isoch::TransmitterMessage::Error)) {
        auto txMsg = static_cast<Isoch::TransmitterMessage>(msg);
         // REMOVED magic_enum::enum_name
//        m_logger->info("IsoStreamHandler: Received TX message {:#x}, p1={}, p2={}", msg, param1, param2);
        switch (txMsg) {
            case Isoch::TransmitterMessage::StreamStarted: m_logger->info("IsoStreamHandler: AMDTP TX Stream Started"); break;
            case Isoch::TransmitterMessage::StreamStopped: m_logger->info("IsoStreamHandler: AMDTP TX Stream Stopped"); break;
            case Isoch::TransmitterMessage::BufferUnderrun: m_logger->warn("IsoStreamHandler: AMDTP TX Buffer Underrun (Seg={}, Pkt={})", param1, param2); break;
            case Isoch::TransmitterMessage::OverrunError: m_logger->error("IsoStreamHandler: AMDTP TX DCL Overrun error occurred"); break;
            // Add cases for other transmitter messages as needed
            case Isoch::TransmitterMessage::Error: m_logger->error("IsoStreamHandler: AMDTP TX Generic Error occurred"); break;
            default: break; // Already logged as unknown TX message
        }
    } else {
        m_logger->warn("IsoStreamHandler: Unknown message type: {:#x}", msg);
    }
}

void IsoStreamHandler::handleNoDataImpl(uint32_t lastCycle) {
    // Log callback using RunLoopHelper
//    logCallbackThreadInfo("IsoStreamHandler", "handleNoDataImpl", this);

    m_logger->warn("IsoStreamHandler: No data received since cycle {}", lastCycle);
    m_packetCounterNoData++;

    // Handle no-data condition (could restart the stream, notify UI, etc.)
    // ...
}

void IsoStreamHandler::handleStructuredDataImpl(const Isoch::ReceivedCycleData& data) {
    // Log callback using RunLoopHelper
//    logCallbackThreadInfo("IsoStreamHandler", "handleStructuredDataImpl", this);

    if (!data.payload || data.payloadLength == 0) {
        return;
    }

    m_logger->debug("IsoStreamHandler: Structured data received: {} bytes at cycle {}",
                   data.payloadLength, data.fireWireTimeStamp);

    // Process structured data (e.g., extract audio samples, MIDI data, etc.)
    // ...
}

void IsoStreamHandler::processData() {
    m_logger->info("IsoStreamHandler: Data processing thread started");

    // Set thread name for debugging
    pthread_setname_np("FWA_DataProcessor");

    // Log the processing thread info
//    logCallbackThreadInfo("IsoStreamHandler", "processData", this);

    while (m_processingRunning) {
        // This thread will handle any asynchronous data processing
        // that shouldn't happen in the FireWire callback threads

        // Process any queued data
        // ...

        // Example: Check if XPC client exists and send data (if applicable)
#ifdef __OBJC__
        // if (m_xpcClient) {
        //    // Logic to get data (e.g., from a queue filled by consumerLoop)
        //    // and send via [m_xpcClient sendAudioData:...];
        // }
#endif

        // Sleep to avoid consuming CPU when idle
//        m_logger->debug("IsoStreamHandler: Data processing thread running");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    m_logger->info("IsoStreamHandler: Data processing thread exiting");
}

void IsoStreamHandler::makeThreadRealtime(std::thread& th) {
    // Set thread priority using machport
    thread_time_constraint_policy_data_t policy;
    mach_port_t thread = pthread_mach_thread_np(th.native_handle());

    // Configure time constraints for audio processing
    // These values may need tuning based on your specific audio requirements
    policy.period = 2000000;      // 2ms period
    policy.computation = 600000;   // 0.6ms of computation time
    policy.constraint = 1200000;   // 1.2ms deadline
    policy.preemptible = 1;        // Allow preemption

    // Apply the policy
    kern_return_t result = thread_policy_set(
        thread,
        THREAD_TIME_CONSTRAINT_POLICY,
        (thread_policy_t)&policy,
        THREAD_TIME_CONSTRAINT_POLICY_COUNT
    );

    if (result == KERN_SUCCESS) {
        m_logger->info("IsoStreamHandler: Successfully set thread to real-time priority");
    } else {
        m_logger->warn("IsoStreamHandler: Failed to set thread to real-time priority, error: {}", result);

        // Fall back to POSIX thread scheduling
        struct sched_param param;
        param.sched_priority = sched_get_priority_max(SCHED_RR);

        if (pthread_setschedparam(th.native_handle(), SCHED_RR, &param) == 0) {
            m_logger->info("IsoStreamHandler: Successfully set thread to SCHED_RR priority");
        } else {
            m_logger->warn("IsoStreamHandler: Failed to set thread priority: {}", strerror(errno));
        }
    }
}

// Helper method to get ring buffer from AudioDeviceStream
raul::RingBuffer* IsoStreamHandler::getInputStreamRingBuffer() {
    if (m_inputStream) {
        return m_inputStream->getReceiverRingBuffer();
    }
    return nullptr;
}

void IsoStreamHandler::consumerLoop() {
#if RECORD
    m_logger->info("IsoStreamHandler: Consumer loop running - RECORDING TO PCM FILE...");
    pthread_setname_np("FWA_RingConsumerRec"); // Set thread name
#else
    m_logger->info("IsoStreamHandler: Consumer loop running - DISCARDING DATA...");
    pthread_setname_np("FWA_RingConsumerDiscard"); // Set thread name
#endif

    raul::RingBuffer* ringBuffer = getInputStreamRingBuffer();
    if (!ringBuffer) {
        m_logger->error("Consumer loop: Ring buffer is null. Exiting.");
        return;
    }

#if RECORD
    // --- File Output Setup ---
    std::string filename = "firewire_audio_capture_" + std::to_string(std::time(nullptr)) + ".pcm";
    std::ofstream outputFile(filename, std::ios::binary);
    if (!outputFile.is_open()) {
        m_logger->error("Consumer loop: Failed to open output file '{}'. Exiting.", filename);
        return;
    }
    m_logger->info("Consumer loop: Recording audio to '{}'", filename);
    // --- End File Output Setup ---

    // Temporary buffer for interleaved float samples before writing to file
    std::vector<float> fileWriteBuffer;
    // Reserve based on chunk size to minimize reallocations
    const size_t READ_CHUNK_FRAMES = 256; // How many ProcessedAudioFrames to read at once
    fileWriteBuffer.reserve(READ_CHUNK_FRAMES * 2); // Reserve space for L/R samples
#else
    // Define READ_CHUNK_FRAMES even if not recording, for the read buffer size
    const size_t READ_CHUNK_FRAMES = 256;
#endif

    // Buffer for reading from ring buffer
    Isoch::ProcessedAudioFrame frameBuffer[READ_CHUNK_FRAMES];
    const size_t frameSize = sizeof(Isoch::ProcessedAudioFrame);
    const size_t framesToRead = READ_CHUNK_FRAMES; // Read this many frames

    uint64_t totalFramesProcessed = 0; // Changed name for clarity
    auto lastLogTime = std::chrono::steady_clock::now();

    while (m_consumerRunning) {
        // Try to read a chunk of frames
        size_t bytesToRead = framesToRead * frameSize;
        size_t bytesRead = ringBuffer->read(bytesToRead, frameBuffer);

        if (bytesRead > 0) {
            if (bytesRead % frameSize != 0) {
                m_logger->error("Consumer loop: Read partial frame data ({} bytes)! Buffer corruption?", bytesRead);
                continue; // Skip corrupted chunk
            }

            size_t framesReadInChunk = bytesRead / frameSize;
            totalFramesProcessed += framesReadInChunk;

#if RECORD
            fileWriteBuffer.clear(); // Clear buffer for this chunk

            // --- Prepare data for file ---
            for (size_t i = 0; i < framesReadInChunk; ++i) {
                fileWriteBuffer.push_back(frameBuffer[i].sampleL);
                fileWriteBuffer.push_back(frameBuffer[i].sampleR);
            }

            // --- Write interleaved data to file ---
            if (!fileWriteBuffer.empty()) {
                outputFile.write(reinterpret_cast<const char*>(fileWriteBuffer.data()),
                                 fileWriteBuffer.size() * sizeof(float));
                if (!outputFile) {
                     m_logger->error("Consumer loop: Error writing to output file '{}'. Stopping recording.", filename);
                     m_consumerRunning = false; // Stop the loop on file error
                     break;
                }
            }
            // --- End Write ---
#endif // RECORD

            // Optional: Log rate periodically (Common to both modes)
            auto now = std::chrono::steady_clock::now();
            if (now - lastLogTime > std::chrono::seconds(5)) {
#if RECORD
                 m_logger->debug("Consumer loop: Wrote {} frames in last 5s. Total written: {}. Available read: {}",
                                framesReadInChunk, totalFramesProcessed, ringBuffer->read_space());
#else
                 m_logger->debug("Consumer loop: Discarded {} frames in last 5s. Total discarded: {}. Available read: {}",
                                framesReadInChunk, totalFramesProcessed, ringBuffer->read_space());
#endif
                 lastLogTime = now;
                 // Log timestamp of last frame in chunk
                 if (framesReadInChunk > 0) {
                    uint64_t lastTs = frameBuffer[framesReadInChunk - 1].presentationNanos;
                    m_logger->debug("Consumer loop: Last processed frame timestamp: {}", lastTs);
                 }
            }

        } else {
            // Buffer is empty, wait a bit
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }

#if RECORD
    // --- Cleanup File ---
    if (outputFile.is_open()) {
        outputFile.close();
        m_logger->info("Consumer loop: Closed output file '{}'. Total frames written: {}", filename, totalFramesProcessed);
    } else if (m_consumerRunning == false) { // Only log if loop exited due to error, not normal stop
         m_logger->info("Consumer loop: Finished (file writing stopped due to error). Total frames written before error: {}", totalFramesProcessed);
    }
    // --- End Cleanup ---
#endif // RECORD

    m_logger->info("IsoStreamHandler: Consumer loop finished.");
}

// Need to implement pushTransmitData if TRANSMIT is enabled
#if TRANSMIT
bool IsoStreamHandler::pushTransmitData(const void* buffer, size_t bufferSizeInBytes) {
    if (m_outputStream) {
       // Delegate to AudioDeviceStream's implementation
       return m_outputStream->pushTransmitData(buffer, bufferSizeInBytes);
    }
    m_logger->warn("pushTransmitData called but output stream is not active/initialized.");
    return false;
}
#else
// Provide a stub if transmitter is disabled
bool IsoStreamHandler::pushTransmitData(const void* buffer, size_t bufferSizeInBytes) {
    // m_logger->trace("pushTransmitData called but transmitter is disabled.");
    return false; // Indicate failure as transmitter isn't running
}
#endif // TRANSMIT

} // namespace FWA
