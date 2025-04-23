// include/FWA/DeviceController.h
#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include <expected>
#include "FWA/IFireWireDeviceDiscovery.h"
#include "FWA/AudioDevice.h"
#include "FWA/Error.h"

namespace FWA {

/**
 * @brief Controls and manages FireWire audio devices
 * 
 * This class is responsible for managing the lifecycle of FireWire audio devices,
 * handling device discovery, and maintaining the list of active devices.
 */
class DeviceController {
public:
    /**
     * @brief Construct a new Device Controller
     * @param discovery Unique pointer to device discovery implementation
     */
    DeviceController(std::unique_ptr<IFireWireDeviceDiscovery> discovery);
    ~DeviceController();

    /**
     * @brief Start device monitoring
     * @param callback Function to call when device status changes
     * @return Success or error status
     */
    std::expected<void, IOKitError> start(DeviceNotificationCallback callback);

    /**
     * @brief Stop device monitoring
     * @return Success or error status
     */
    std::expected<void, IOKitError> stop();

    /**
     * @brief Find device by GUID
     * @param guid Global Unique Identifier of the device to find
     * @return Shared pointer to device or error if not found
     */
    std::expected<std::shared_ptr<AudioDevice>, IOKitError> getDeviceByGuid(std::uint64_t guid);
    
    CFRunLoopRef getRunLoopRef() { return runLoopRef_; }    

    /**
     * @brief Set the discovery implementation
     * @param discovery Unique pointer to device discovery implementation
     */
    void setDiscovery(std::unique_ptr<IFireWireDeviceDiscovery> discovery) {
        discovery_ = std::move(discovery);
    }

    /**
     * @brief Get a raw pointer to the discovery implementation (for C API interop)
     */
    IFireWireDeviceDiscovery* getDiscoveryRaw() const { return discovery_.get(); }

    /**
     * @brief Add a new device to the managed list
     * @param device Device to add
     */
    void addDevice(std::shared_ptr<AudioDevice> device);


private:
    std::unique_ptr<IFireWireDeviceDiscovery> discovery_;      ///< Device discovery implementation
    std::vector<std::shared_ptr<AudioDevice>> devices_;        ///< List of active devices
    std::mutex devicesMutex_;                                  ///< Mutex for device list access
    DeviceNotificationCallback notificationCallback_;           ///< Stored callback
    std::atomic<bool> isRunning_{false};                      ///< Running state flag
    
    // runloop ref
    CFRunLoopRef runLoopRef_;


    /**
     * @brief Remove a device from the managed list
     * @param guid GUID of the device to remove
     */
    void removeDevice(std::uint64_t guid);
};

} // namespace FWA
