#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <memory>
#include <expected> // Include expected
#include "Isoch/interfaces/ITransmitPacketProvider.hpp" // Include interface

#include "FWA/CommandInterface.h"
#include "FWA/AudioDevice.h"
#include "FWA/Error.h"
#include <spdlog/spdlog.h>
#include "Isoch/core/AmdtpReceiver.hpp"
#include "Isoch/core/ReceiverFactory.hpp"
#include "Isoch/AudioDeviceStream.hpp"
#include <IOKit/firewire/IOFireWireLib.h>
#include "Isoch/utils/RingBuffer.hpp" // Include RingBuffer header

#ifdef __OBJC__
// #import "FWA/XPC/XPCReceiverClient.hpp"
#else
class XPCReceiverClient; // Forward declaration when compiling as pure C++
#endif

namespace FWA {

/**
 * @brief Handles isochronous audio streaming for FireWire devices
 *
 * This class manages AMDTP (Audio & Music Data Transmission Protocol) streams
 * for FireWire audio devices, including packet processing, callbacks,
 * and properly configuring streams with RAII principles.
 */
class IsoStreamHandler {
public:
    /**
     * @brief Construct a new IsoStreamHandler
     *
     * @param device Shared pointer to the audio device to manage streams for
     * @param logger Logger for diagnostics
     * @param commandInterface Shared pointer to the command interface
     * @param interface FireWire device interface to use for stream creation
     */
    explicit IsoStreamHandler(std::shared_ptr<AudioDevice> device,
                              std::shared_ptr<spdlog::logger> logger,
                              std::shared_ptr<CommandInterface> commandInterface,
                              IOFireWireLibDeviceRef interface);

    /**
     * @brief Destroy the IsoStreamHandler and cleanup resources
     */
    ~IsoStreamHandler();

    /**
     * @brief Start all configured audio streams
     *
     * @return std::expected<void, IOKitError> Success or error
     */
    std::expected<void, IOKitError> start();

    /**
     * @brief Stop all active audio streams
     */
    void stop();

    /**
     * @brief Push audio data into the transmit stream.
     *
     * This method is intended to be called by the component responsible for
     * providing audio data (e.g., an XPC bridge).
     *
     * @param buffer Pointer to the audio data (e.g., interleaved float samples).
     * @param bufferSizeInBytes Size of the data in bytes.
     * @return True if the data was successfully pushed into the transmitter's buffer,
     *         false otherwise (e.g., stream not started, buffer full).
     */
    bool pushTransmitData(const void* buffer, size_t bufferSizeInBytes);

    Isoch::ITransmitPacketProvider* getTransmitPacketProvider();


private:
    // Callback handlers with proper refcon
    static void handleDataPush(const uint8_t* pPayload, size_t payloadLength, void* refCon);
    static void handleMessage(uint32_t msg, uint32_t param1, uint32_t param2, void* refCon);
    static void handleNoData(uint32_t lastCycle, void* refCon);
    static void handleStructuredData(const Isoch::ReceivedCycleData& data, void* refCon);

    // Instance methods that static callbacks forward to
    void handleDataPushImpl(const uint8_t* pPayload, size_t payloadLength);
    void handleMessageImpl(uint32_t msg, uint32_t param1, uint32_t param2);
    void handleNoDataImpl(uint32_t lastCycle);
    void handleStructuredDataImpl(const Isoch::ReceivedCycleData& data);

    // Background processing
    void processData();
    void makeThreadRealtime(std::thread& th);

    // Ring Buffer Consumer Thread
    std::thread m_consumerThread;
    std::atomic<bool> m_consumerRunning{false};
    void consumerLoop(); // The function the consumer thread will run

    // Helper to get the ring buffer pointer safely
    raul::RingBuffer* getInputStreamRingBuffer();

    // Statistics tracking
    std::chrono::steady_clock::time_point m_lastTimestamp;
    std::chrono::steady_clock::time_point m_lastTransmitterTimestamp;
    uint32_t m_packetCounter = 0;
    uint32_t m_packetCounterNoData = 0;
    uint32_t m_transmitterPacketCounter = 0;
    UInt16 m_prevSYT = 0;
    UInt32 m_nodataPacketsSkipped = 0;

    // Core components
    std::shared_ptr<spdlog::logger> m_logger;
    std::shared_ptr<AudioDevice> m_audioDevice;
    std::mutex m_streamMutex;
    std::shared_ptr<CommandInterface> m_commandInterface;

    // Processing thread
    std::thread m_processingThread;
    std::atomic<bool> m_processingRunning{false};


    // Streams (Input and Output)
    std::shared_ptr<AudioDeviceStream> m_inputStream;
    std::shared_ptr<AudioDeviceStream> m_outputStream; // Member for output stream

    // Direct stream implementations for low-level access
    std::shared_ptr<Isoch::AmdtpReceiver> m_directReceiver;

    // Device info
    UInt16 m_nodeId = 0;
    IOFWSpeed m_speed = kFWSpeed100MBit; // default to minimum speed

    // Interface
    IOFireWireLibDeviceRef m_interface = nullptr;

// #ifdef __OBJC__
//     XPCReceiverClient* m_xpcClient = nil; // XPC client for audio processing
// #endif
};

} // namespace FWA