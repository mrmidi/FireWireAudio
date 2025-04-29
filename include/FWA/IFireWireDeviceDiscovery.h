// include/FWA/IFireWireDeviceDiscovery.h
#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <functional>
#include <system_error>
#include <expected>
#include "FWA/Error.h"

namespace FWA {

class AudioDevice; // Forward declaration

/**
 * @brief Callback type for device connection/disconnection notifications
 * @param device Shared pointer to the AudioDevice that was connected/disconnected
 * @param connected True if device was connected, false if disconnected
 */
using DeviceNotificationCallback = std::function<void(std::shared_ptr<AudioDevice>, bool /* connected */)>;

/**
 * @brief Interface for FireWire device discovery and management
 *
 * This interface defines the contract for discovering and managing FireWire audio devices
 * in the system. Implementations should handle device enumeration, monitoring, and status updates.
 */
class IFireWireDeviceDiscovery {
public:
    virtual ~IFireWireDeviceDiscovery() = default;
    
    /**
     * @brief Start device discovery with callback notifications
     * @param callback Function to be called when devices are connected/disconnected
     * @return Success or error status
     */
    virtual std::expected<void, IOKitError> startDiscovery(DeviceNotificationCallback callback) = 0;

    /**
     * @brief Stop device discovery and clean up resources
     * @return Success or error status
     */
    virtual std::expected<void, IOKitError> stopDiscovery() = 0;

    /**
     * @brief Get a list of all currently connected devices
     * @return Vector of shared pointers to connected AudioDevice objects or error
     */
    virtual std::expected<std::vector<std::shared_ptr<AudioDevice>>, IOKitError> getConnectedDevices() = 0;

    /**
     * @brief Find a specific device by its GUID
     * @param guid The Global Unique Identifier of the device to find
     * @return Shared pointer to the AudioDevice if found, or error if not found
     */
    virtual std::expected<std::shared_ptr<AudioDevice>, IOKitError> getDeviceByGuid(std::uint64_t guid) = 0;
};

} // namespace FWA
