#pragma once

#include "IFireWireDeviceDiscovery.h"
#include "FWA/Error.h"
#include <IOKit/IOKitLib.h>
#include <IOKit/firewire/IOFireWireLib.h>
#include <IOKit/avc/IOFireWireAVCLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <expected>
#include <vector>
#include "AudioDevice.h"
#include <thread>
#include <mutex>

namespace FWA {

/**
 * @brief IOKit implementation of FireWire device discovery
 *
 * This class implements the IFireWireDeviceDiscovery interface using IOKit
 * to discover and monitor FireWire audio devices.
 */
class IOKitFireWireDeviceDiscovery : public IFireWireDeviceDiscovery {
public:
    /**
     * @brief Construct a new IOKit FireWire Device Discovery object
     */
    IOKitFireWireDeviceDiscovery(std::shared_ptr<DeviceController> deviceController);
    
    /**
     * @brief Destroy the IOKit FireWire Device Discovery object
     */
    ~IOKitFireWireDeviceDiscovery() override;
    
    /**
     * @brief Start device discovery with callback notifications
     * @param callback Function to be called when devices are connected/disconnected
     * @return Success or error status
     */
    std::expected<void, IOKitError> startDiscovery(DeviceNotificationCallback callback) override;

    /**
     * @brief Stop device discovery and clean up resources
     * @return Success or error status
     */
    std::expected<void, IOKitError> stopDiscovery() override;

    /**
     * @brief Get a list of all currently connected devices
     * @return Vector of shared pointers to connected AudioDevice objects or error
     */
    std::expected<std::vector<std::shared_ptr<AudioDevice>>, IOKitError> getConnectedDevices() override;

    /**
     * @brief Find a specific device by its GUID
     * @param guid The Global Unique Identifier of the device to find
     * @return Shared pointer to the AudioDevice if found, or error if not found
     */
    std::expected<std::shared_ptr<AudioDevice>, IOKitError> getDeviceByGuid(std::uint64_t guid) override;
    
    /**
     * @brief Check if master port is valid
     * @return bool True if master port is valid
     */
    bool isMasterPortValid() const { return masterPort_ != MACH_PORT_NULL; }

    /**
     * @brief Check if notification port is valid
     * @return bool True if notification port is valid
     */
    bool isNotificationPortValid() const { return notifyPort_ != nullptr; }
    
    /**
     * @brief Set a test callback for unit testing
     * @param callback Test callback function
     */
    void setTestCallback(DeviceNotificationCallback callback);
    
private:
    mach_port_t                 masterPort_;          ///< IOKit master port
    IONotificationPortRef       notifyPort_;          ///< IOKit notification port
    CFRunLoopSourceRef         runLoopSource_;       ///< RunLoop source for notifications
    io_iterator_t              deviceIterator_;      ///< Iterator for device enumeration
    DeviceNotificationCallback callback_;            ///< Device notification callback
    
    /**
     * @brief Static callback for device addition
     * @param refCon Reference constant passed during registration
     * @param iterator Iterator containing new devices
     */
    static void deviceAdded(void* refCon, io_iterator_t iterator);
    
    std::vector<std::shared_ptr<AudioDevice>> devices_;    ///< List of discovered devices
    std::mutex devicesMutex_;                              ///< Mutex for thread-safe device list access
    
    /**
     * @brief Create an AudioDevice object from IOKit device
     * @param device IOKit device object
     * @return AudioDevice object or error
     */
    std::expected<std::shared_ptr<AudioDevice>, IOKitError> createAudioDevice(io_object_t device);

    /**
     * @brief Find a device in the list by GUID
     * @param guid Device GUID to search for
     * @return Shared pointer to found device or nullptr
     */
    std::shared_ptr<AudioDevice> findDeviceByGuid(UInt64 guid);
    
    std::thread discoveryThread_;                          ///< Thread for device discovery
    std::atomic<bool> discoveryThreadRunning_{false};     ///< Thread running state
    
    /**
     * @brief Main discovery thread function
     */
    void discoveryThreadFunction();
    
    CFRunLoopRef discoveryRunLoop_ = nullptr;             ///< RunLoop for discovery thread

    /**
     * @brief Get device GUID from IOKit device object
     * @param device IOKit device object
     * @return Device GUID or error
     */
    std::expected<UInt64, IOKitError> getDeviceGuid(io_object_t device);
    
    /**
     * @brief Static callback for device removal and other events
     * @param refCon Reference constant passed during registration
     * @param service IOKit service that generated the event
     * @param messageType Type of message received
     * @param messageArgument Additional message data
     */
    static void deviceInterestCallback(void* refCon, io_service_t service, natural_t messageType, void* messageArgument);
    
    std::shared_ptr<DeviceController> deviceController_;  ///< Device controller for device operations
    
    friend class IOKitFireWireDeviceDiscoveryTests;
};

} // namespace FWA
