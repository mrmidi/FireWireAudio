#pragma once

#include <cstdint>
#include <expected>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <IOKit/avc/IOFireWireAVCLib.h>
#include "FWA/Error.h"

namespace FWA {

// Forward declaration
class AudioDevice;

/**
 * @brief Callback type for device status notifications
 * @param device Pointer to the audio device that generated the notification
 * @param messageType Type of message received from the device
 * @param messageArgument Additional message data
 */
using DeviceStatusCallback = std::function<void(std::shared_ptr<AudioDevice>, natural_t messageType, void* messageArgument)>;

/**
 * @brief Interface for sending commands to and receiving responses from a FireWire audio device
 * 
 * This class manages the AVC command interface for a FireWire audio device,
 * handling command transmission and response reception.
 */
class CommandInterface {
public:
    /**
     * @brief Construct a new Command Interface object
     * @param pAudioDevice Shared pointer to the associated AudioDevice
     */
    explicit CommandInterface(std::shared_ptr<AudioDevice> pAudioDevice);
    
    ~CommandInterface();

    // Delete copying
    CommandInterface(const CommandInterface&) = delete;
    CommandInterface& operator=(const CommandInterface&) = delete;

    /**
     * @brief Move constructor
     * @param other CommandInterface to move from
     */
    CommandInterface(CommandInterface&&) noexcept;

    /**
     * @brief Move assignment operator
     * @param other CommandInterface to move from
     * @return Reference to this object
     */
    CommandInterface& operator=(CommandInterface&&) noexcept;

    /**
     * @brief Set callback for device status notifications
     * @param callback Function to be called when device status changes
     * @param refCon Reference constant to be passed to callback
     * @return Success or error status
     */
    std::expected<void, IOKitError> setNotificationCallback(DeviceStatusCallback callback, void* refCon);

    /**
     * @brief Clear the notification callback
     */
    void clearNotificationCallback();

    /**
     * @brief Activate the command interface
     * @return Success or error status
     */
    std::expected<void, IOKitError> activate();

    /**
     * @brief Deactivate the command interface
     * @return Success or error status
     */
    std::expected<void, IOKitError> deactivate();

    /**
     * @brief Send a command to the device
     * @param command Vector of bytes containing the command
     * @return Response from the device or error status
     */
    std::expected<std::vector<uint8_t>, IOKitError> sendCommand(const std::vector<uint8_t>& command);

    /**
     * @brief Get the AVC interface
     * @return Pointer to the AVC unit interface
     */
    IOFireWireAVCLibUnitInterface** getAvcInterface() const { return avcInterface_; }

    /**
     * @brief Get the AVC interface
     * @return IOFireWireAVCLibUnitInterface** The AVC interface
     */
    IOFireWireAVCLibUnitInterface** getAVCInterface() const { return avcInterface_; }

    /**
     * @brief Get the AVC unit
     * @return IOKit service for the AVC unit
     */
    io_service_t getAVCUnit() const { return avcUnit_; }
    
    bool isActive() const { return avcInterface_ != 0; }

private:
    /**
     * @brief Create the AVC unit interface
     * @return Success or error status
     */
    std::expected<void, IOKitError> createAVCUnitInterface();

    /**
     * @brief Release the AVC unit interface
     * @return Success or error status
     */
    std::expected<void, IOKitError> releaseAVCUnitInterface();

    /**
     * @brief Static callback for device interest notifications
     * @param refCon Reference constant passed during callback registration
     * @param service IOKit service that generated the notification
     * @param messageType Type of notification message
     * @param messageArgument Additional message data
     */
    static void deviceInterestCallback(void* refCon, io_service_t service, natural_t messageType, void* messageArgument);

private:
    std::shared_ptr<AudioDevice> pAudioDevice_;
    io_service_t avcUnit_ = 0;
    IOFireWireAVCLibUnitInterface** avcInterface_ = nullptr;
    io_object_t interestNotification_ = 0;
    DeviceStatusCallback notificationCallback_;
    void* refCon_ = nullptr;
//    std::atomic<bool> active_ = false;
};

} // namespace FWA
