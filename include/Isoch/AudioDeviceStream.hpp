#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <expected>
#include <functional>
#include <atomic>
#include <variant>
#include <thread>
#include <IOKit/firewire/IOFireWireLib.h>
#include <spdlog/spdlog.h>

#include "FWA/Error.h"
#include "FWA/AudioDevice.h"
#include "Isoch/core/AmdtpReceiver.hpp"
#include "Isoch/core/AmdtpTransmitter.hpp" // Include the transmitter
#include "Isoch/core/ReceiverFactory.hpp"
#include "Isoch/core/ReceiverTypes.hpp"
#include "Isoch/utils/RingBuffer.hpp" // Include RingBuffer header
#include "Isoch/interfaces/ITransmitPacketProvider.hpp" // Include interface

namespace FWA {

/**
 * @brief Enumeration of stream types supported by AudioDeviceStream
 */
enum class StreamType {
    AmdtpReceiver,           ///< AMDTP receiver stream for audio input
    AmdtpTransmitter,        ///< AMDTP transmitter stream for audio output
    UniversalReceiver,       ///< Legacy universal receiver type
    UniversalTransmitter     ///< Legacy universal transmitter type
};

/**
 * @brief AudioDeviceStream encapsulates an audio stream connected to a FireWire device
 *
 * This class provides a modern C++23 implementation for managing audio streams
 * over FireWire, using AMDTP (Audio & Music Data Transmission Protocol). It replaces
 * the legacy AVCDeviceStream structure with a robust, type-safe, and efficient design.
 */
class AudioDeviceStream : public std::enable_shared_from_this<AudioDeviceStream> {
public:
    /**
     * @brief Create an AudioDeviceStream as a factory method
     *
     * @param audioDevice Shared pointer to the parent AudioDevice
     * @param streamType Type of stream to create
     * @param devicePlugNumber The device plug number to connect to
     * @param logger Logger for diagnostics
     * @param dataPushCallback Callback for received data packets (for receiver streams)
     * @param dataPushRefCon Reference context for data push callback
     * @param messageCallback Callback for stream events
     * @param messageRefCon Reference context for message callback
     * @param cyclesPerSegment Number of cycles per segment for stream buffer
     * @param numSegments Number of segments for stream buffer
     * @param bufferSize Size of buffer in bytes
     * @param speed Initial speed setting
     * @param interface FireWire device interface
     * @return std::expected<std::shared_ptr<AudioDeviceStream>, IOKitError> Shared pointer to created stream or error
     */
    static std::expected<std::shared_ptr<AudioDeviceStream>, IOKitError> create(
                                                                                std::shared_ptr<AudioDevice> audioDevice,
                                                                                StreamType streamType,
                                                                                uint8_t devicePlugNumber,
                                                                                std::shared_ptr<spdlog::logger> logger,
                                                                                Isoch::PacketCallback dataPushCallback = nullptr,
                                                                                void* dataPushRefCon = nullptr,
                                                                                Isoch::MessageCallback messageCallback = nullptr,
                                                                                void* messageRefCon = nullptr,
                                                                                unsigned int cyclesPerSegment = 8,
                                                                                unsigned int numSegments = 4,
                                                                                unsigned int bufferSize = 512,
                                                                                IOFWSpeed speed = kFWSpeed100MBit,
                                                                                IOFireWireLibDeviceRef interface = nullptr);
    
    /**
     * @brief Create a receiver stream for a device output plug
     *
     * @param audioDevice Parent audio device
     * @param devicePlugNumber Device output plug number
     * @param dataPushCallback Callback for received data
     * @param dataPushRefCon Context for data callback
     * @param messageCallback Callback for stream events
     * @param messageRefCon Context for message callback
     * @param logger Logger for diagnostics
     * @param cyclesPerSegment Number of FireWire cycles per segment
     * @param numSegments Number of segments in the cycle buffer
     * @param cycleBufferSize Size of cycle buffer in bytes
     * @param interface FireWire device interface
     * @return std::expected<std::shared_ptr<AudioDeviceStream>, IOKitError> Created stream or error
     */
    static std::expected<std::shared_ptr<AudioDeviceStream>, IOKitError> createReceiverForDevicePlug(
                                                                                                     std::shared_ptr<AudioDevice> audioDevice,
                                                                                                     uint8_t devicePlugNumber,
                                                                                                     Isoch::PacketCallback dataPushCallback,
                                                                                                     void* dataPushRefCon,
                                                                                                     Isoch::MessageCallback messageCallback,
                                                                                                     void* messageRefCon,
                                                                                                     std::shared_ptr<spdlog::logger> logger,
                                                                                                     unsigned int cyclesPerSegment = 8,
                                                                                                     unsigned int numSegments = 4,
                                                                                                     unsigned int cycleBufferSize = 512,
                                                                                                     IOFireWireLibDeviceRef interface = nullptr);
    
    /**
     * @brief Create a transmitter stream for a device input plug
     *
     * @param audioDevice Parent audio device
     * @param devicePlugNumber Device input plug number
     * @param dataPullCallback Callback to provide data for transmission
     * @param dataPullRefCon Context for data callback
     * @param messageCallback Callback for stream events
     * @param messageRefCon Context for message callback
     * @param logger Logger for diagnostics
     * @param cyclesPerSegment Number of FireWire cycles per segment
     * @param numSegments Number of segments in the cycle buffer
     * @param transmitBufferSize Size of transmission buffer in bytes
     * @param interface FireWire device interface
     * @return std::expected<std::shared_ptr<AudioDeviceStream>, IOKitError> Created stream or error
     */
    static std::expected<std::shared_ptr<AudioDeviceStream>, IOKitError> createTransmitterForDevicePlug(
                                                                                                        std::shared_ptr<AudioDevice> audioDevice,
                                                                                                        uint8_t devicePlugNumber,
                                                                                                        Isoch::PacketCallback dataPullCallback,
                                                                                                        void* dataPullRefCon,
                                                                                                        Isoch::MessageCallback messageCallback,
                                                                                                        void* messageRefCon,
                                                                                                        std::shared_ptr<spdlog::logger> logger,
                                                                                                        unsigned int cyclesPerSegment = 8,
                                                                                                        unsigned int numSegments = 4,
                                                                                                        unsigned int transmitBufferSize = 512,
                                                                                                        IOFireWireLibDeviceRef interface = nullptr);
    
    /**
     * @brief Destructor handles proper cleanup of resources
     */
    ~AudioDeviceStream();
    
    // Prevent copying
    AudioDeviceStream(const AudioDeviceStream&) = delete;
    AudioDeviceStream& operator=(const AudioDeviceStream&) = delete;
    
    // Allow moving
    AudioDeviceStream(AudioDeviceStream&&) noexcept = delete;
    AudioDeviceStream& operator=(AudioDeviceStream&&) noexcept = delete;

    Isoch::ITransmitPacketProvider* getTransmitPacketProvider() const;
    
    /**
     * @brief Start the audio stream
     * @return Success or error status
     */
    std::expected<void, IOKitError> start();
    
    /**
     * @brief Stop the audio stream
     * @return Success or error status
     */
    std::expected<void, IOKitError> stop();
    
    /**
     * @brief Set the isochronous channel for the stream
     * @param channel Channel number
     * @return Success or error status
     */
    std::expected<void, IOKitError> setIsochChannel(uint32_t channel);
    
    /**
     * @brief Set the isochronous speed for the stream
     * @param speed Speed setting
     * @return Success or error status
     */
    std::expected<void, IOKitError> setIsochSpeed(IOFWSpeed speed);
    
    /**
     * @brief Get the current isochronous channel
     * @return Current channel number
     */
    uint32_t getIsochChannel() const { return m_isochChannel; }
    
    /**
     * @brief Get the current isochronous speed
     * @return Current speed setting
     */
    IOFWSpeed getIsochSpeed() const { return m_isochSpeed; }
    
    /**
     * @brief Set message callback for stream events
     * @param callback Function to call for stream events
     * @param refCon Context pointer for callback
     */
    void setMessageCallback(Isoch::MessageCallback callback, void* refCon = nullptr);
    
    /**
     * @brief Set packet callback for received data
     * @param callback Function to call for received packets
     * @param refCon Context pointer for callback
     */
    void setPacketCallback(Isoch::PacketCallback callback, void* refCon = nullptr);
    
    /**
     * @brief Set packet pull callback for transmitter data
     * @param callback Function to call to get data for transmission
     * @param refCon Context pointer for callback
     */
    void setPacketPullCallback(Isoch::PacketCallback callback, void* refCon = nullptr);
    
    /**
     * @brief Get the type of this stream
     * @return The stream type
     */
    StreamType getStreamType() const { return m_streamType; }
    
    /**
     * @brief Get the device plug number
     * @return The device plug number
     */
    uint8_t getDevicePlugNumber() const { return m_devicePlugNumber; }
    
    /**
     * @brief Check if the stream is active
     * @return True if the stream is active
     */
    bool isActive() const { return m_isActive; }
    
    /**
     * @brief Check if the plug is connected
     * @return True if the plug is connected
     */
    bool isPlugConnected() const { return m_isPlugConnected; }
    
    /**
     * @brief Get the associated RunLoop for this stream
     * @return CFRunLoopRef for this stream's callbacks
     */
    CFRunLoopRef getRunLoop() const { return m_runLoop; }

    /**
     * @brief Get the underlying Ring Buffer for receiver streams.
     * @return Pointer to the raul::RingBuffer, or nullptr if not a receiver or not initialized.
     */
    raul::RingBuffer* getReceiverRingBuffer() const; // Declaration added
    
    /**
     * @brief Push audio data to the transmitter for sending
     * @param buffer Pointer to the audio data buffer
     * @param bufferSizeInBytes Size of the audio data buffer in bytes
     * @return True if the data was successfully pushed, false otherwise
     */
    bool pushTransmitData(const void* buffer, size_t bufferSizeInBytes);
    
private:
    // Private constructor, use create() factory method instead
    AudioDeviceStream(
                      std::shared_ptr<AudioDevice> audioDevice,
                      StreamType streamType,
                      uint8_t devicePlugNumber,
                      std::shared_ptr<spdlog::logger> logger,
                      Isoch::PacketCallback dataPushCallback,
                      void* dataPushRefCon,
                      Isoch::MessageCallback messageCallback,
                      void* messageRefCon,
                      unsigned int cyclesPerSegment,
                      unsigned int numSegments,
                      unsigned int bufferSize,
                      IOFWSpeed speed = kFWSpeed100MBit,
                      IOFireWireLibDeviceRef interface = nullptr);
    
    // Core components
    std::shared_ptr<AudioDevice> m_audioDevice;
    std::shared_ptr<spdlog::logger> m_logger;
    IOFireWireLibDeviceRef m_interface;
    
    // Stream configuration
    StreamType m_streamType;
    uint8_t m_devicePlugNumber;
    unsigned int m_cyclesPerSegment;
    unsigned int m_numSegments;
    unsigned int m_bufferSize;
    
    // Isoch settings
    uint32_t m_isochChannel = 0xFFFFFFFF; // Any available channel
    IOFWSpeed m_isochSpeed = kFWSpeed100MBit;
    
    // State tracking
    std::atomic<bool> m_isActive{false};
    std::atomic<bool> m_isPlugConnected{false};
    
    // RunLoop management
    CFRunLoopRef m_runLoop = nullptr;
    std::thread m_runLoopThread;
    std::atomic<bool> m_runLoopActive{false};
    
    // Callback support with RefCons
    Isoch::PacketCallback m_packetCallback = nullptr;
    void* m_packetCallbackRefCon = nullptr;
    
    Isoch::PacketCallback m_packetPullCallback = nullptr;
    void* m_packetPullCallbackRefCon = nullptr;
    
    Isoch::MessageCallback m_messageCallback = nullptr;
    void* m_messageCallbackRefCon = nullptr;
    
    // Stream implementation - using std::variant for type-safe polymorphism
    using StreamVariant = std::variant<
        std::shared_ptr<Isoch::AmdtpReceiver>,
        std::shared_ptr<Isoch::AmdtpTransmitter> // Add AmdtpTransmitter to the variant
    >;
    
    StreamVariant m_streamImpl;
    
    // Methods for handling plug connections
    std::expected<void, IOKitError> connectPlug();
    std::expected<void, IOKitError> disconnectPlug();
    
    // Helper method to initialize and start the RunLoop thread
    std::expected<void, IOKitError> initializeRunLoop();
    
    // RunLoop thread function
    void runLoopThreadFunc();
    
    // Helper method to make the RunLoop thread time-constrained (real-time)
    void makeRunLoopThreadRealTime();
    
    // Internal callback methods with proper refcon handling
    static void handlePacketReceived(const uint8_t* data, size_t length, void* refCon);
    static void handleMessageReceived(uint32_t message, uint32_t param1, uint32_t param2, void* refCon);
    
    // New callback methods for processed data
    static void handleProcessedDataStatic(const std::vector<Isoch::ProcessedSample>& samples, 
                                         const Isoch::PacketTimingInfo& timing, 
                                         void* refCon);
    void handleProcessedDataImpl(const std::vector<Isoch::ProcessedSample>& samples,
                               const Isoch::PacketTimingInfo& timing);
    
    // Helper method to set up receiver callbacks with proper refcon handling
    void setupReceiverCallbacks(std::shared_ptr<Isoch::AmdtpReceiver> receiver);
    
    // Helper method to convert IOKit return codes to our error type
    static std::expected<void, IOKitError> checkIOReturn(IOReturn result);
};

} // namespace FWA