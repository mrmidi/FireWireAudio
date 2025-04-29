#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <expected>
#include <functional>
#include <atomic>
#include <variant>
#include <IOKit/firewire/IOFireWireLib.h>
#include <spdlog/spdlog.h>

#include "FWA/Error.h"
#include "FWA/AudioDevice.h"
#include "FWA/Isoch/AmdtpTransmitter.hpp"
#include "FWA/Isoch/AmdtpHelpers.hpp"

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
class AudioDeviceStream {
public:
    /**
     * @brief Create an AudioDeviceStream as a factory method
     * 
     * @param audioDevice Shared pointer to the parent AudioDevice
     * @param streamType Type of stream to create
     * @param devicePlugNumber The device plug number to connect to
     * @param logger Logger for diagnostics
     * @param cyclesPerSegment Number of cycles per segment for stream buffer
     * @param numSegments Number of segments for stream buffer
     * @param bufferSize Size of buffer in bytes
     * @return std::expected<std::shared_ptr<AudioDeviceStream>, IOKitError> Shared pointer to created stream or error
     */
    static std::expected<std::shared_ptr<AudioDeviceStream>, IOKitError> create(
        std::shared_ptr<AudioDevice> audioDevice,
        StreamType streamType,
        uint8_t devicePlugNumber,
        std::shared_ptr<spdlog::logger> logger,
        unsigned int cyclesPerSegment = 8,
        unsigned int numSegments = 4,
        unsigned int bufferSize = 512);
    
    /**
     * @brief Destructor handles proper cleanup of resources
     */
    ~AudioDeviceStream();

    // Prevent copying
    AudioDeviceStream(const AudioDeviceStream&) = delete;
    AudioDeviceStream& operator=(const AudioDeviceStream&) = delete;
    
    // Allow moving
    AudioDeviceStream(AudioDeviceStream&&) noexcept = default;
    AudioDeviceStream& operator=(AudioDeviceStream&&) noexcept = default;

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
    uint32_t getIsochChannel() const { return isochChannel_; }
    
    /**
     * @brief Get the current isochronous speed
     * @return Current speed setting
     */
    IOFWSpeed getIsochSpeed() const { return isochSpeed_; }
    
    /**
     * @brief Set message callback for stream events
     * @param callback Function to call for stream events
     */
    void setMessageCallback(Isoch::MessageCallback callback);
    
    /**
     * @brief Set packet callback for received data
     * @param callback Function to call for received packets
     */
    void setPacketCallback(Isoch::PacketCallback callback);
    
    /**
     * @brief Get the type of this stream
     * @return The stream type
     */
    StreamType getStreamType() const { return streamType_; }
    
    /**
     * @brief Get the device plug number
     * @return The device plug number
     */
    uint8_t getDevicePlugNumber() const { return devicePlugNumber_; }
    
    /**
     * @brief Check if the stream is active
     * @return True if the stream is active
     */
    bool isActive() const { return isActive_; }
    
    /**
     * @brief Check if the plug is connected
     * @return True if the plug is connected
     */
    bool isPlugConnected() const { return isPlugConnected_; }

private:
    // Private constructor, use create() factory method instead
    AudioDeviceStream(
        std::shared_ptr<AudioDevice> audioDevice,
        StreamType streamType,
        uint8_t devicePlugNumber,
        std::shared_ptr<spdlog::logger> logger,
        unsigned int cyclesPerSegment,
        unsigned int numSegments,
        unsigned int bufferSize);

    // Core components
    std::shared_ptr<AudioDevice> audioDevice_;
    std::shared_ptr<spdlog::logger> logger_;
    
    // Stream configuration
    StreamType streamType_;
    uint8_t devicePlugNumber_;
    unsigned int cyclesPerSegment_;
    unsigned int numSegments_;
    unsigned int bufferSize_;
    
    // Isoch settings
    uint32_t isochChannel_ = 0xFFFFFFFF; // Any available channel
    IOFWSpeed isochSpeed_ = kFWSpeed100MBit;
    
    // State tracking
    std::atomic<bool> isActive_{false};
    std::atomic<bool> isPlugConnected_{false};
    
    // Stream implementation - using std::variant for type-safe polymorphism
    using StreamVariant = std::variant<
        std::shared_ptr<Isoch::AmdtpReceiver>,
        std::shared_ptr<Isoch::AmdtpTransmitter>
        // Add other stream types here when implementing them
    >;
    
    StreamVariant streamImpl_;
    
    // Methods for handling plug connections
    std::expected<void, IOKitError> connectPlug();
    std::expected<void, IOKitError> disconnectPlug();
    
    // Helpers for handling specific stream types
    template<typename T, typename... Args>
    static std::expected<std::shared_ptr<T>, IOKitError> createStreamImpl(Args&&... args);
    
    // Helper method to convert IOKit return codes to our error type
    static std::expected<void, IOKitError> checkIOReturn(IOReturn result);
};

} // namespace FWA