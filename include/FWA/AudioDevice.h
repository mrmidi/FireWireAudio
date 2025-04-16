#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <expected>
#include <IOKit/IOKitLib.h>           
#include <IOKit/firewire/IOFireWireLib.h>
#include "FWA/Error.h"
#include "FWA/DeviceInfo.hpp"
#include <IOKit/avc/IOFireWireAVCLib.h>

// Forward declarations
namespace FWA {
    class CommandInterface;
    class DeviceParser;
    class DeviceController; // Added forward declaration
}

namespace FWA {

/**
 * @brief Represents a FireWire audio device and manages its lifecycle
 *
 * AudioDevice class encapsulates a FireWire audio device, providing access to its
 * properties, controls, and interfaces. It manages the device's lifecycle and provides
 * methods to interact with the device's capabilities.
 */
class AudioDevice : public std::enable_shared_from_this<AudioDevice> {
public:
    /**
     * @brief Construct a new Audio Device object
     * @param guid Global Unique Identifier for the device
     * @param deviceName Name of the device
     * @param vendorName Name of the device vendor
     * @param avcUnit IOKit service representing the AVC unit
     */
    AudioDevice(std::uint64_t guid,
                const std::string& deviceName,
                const std::string& vendorName,
                io_service_t avcUnit,
                DeviceController *deviceController); // TODO: make shared pointer
    
    ~AudioDevice();

    /**
     * @brief Initialize the device after construction
     * @note Should be called after creation via std::make_shared
     * @return Success or error status
     */
    std::expected<void, IOKitError> init();

    /**
     * @brief Discover the capabilities of this device.
     *
     * This method triggers the DeviceParser to query the device and populate
     * the DeviceInfo structure. It should be called after init().
     * @return Success or error status
     */
    std::expected<void, IOKitError> discoverCapabilities();

    /**
     * @brief Get the device's GUID
     * @return uint64_t The device's Global Unique Identifier
     */
    std::uint64_t getGuid() const { return guid_; }

    /**
     * @brief Get the device name
     * @return const std::string& The device's name
     */
    const std::string& getDeviceName() const { return deviceName_; }

    /**
     * @brief Get the vendor name
     * @return const std::string& The device vendor's name
     */
    const std::string& getVendorName() const { return vendorName_; }

    /**
     * @brief Get the AVC device service
     * @return io_service_t IOKit service for AVC device
     */
    io_service_t getAVCDevice() const { return avcUnit_; }

    /**
     * @brief Get the FireWire unit service
     * @return io_service_t IOKit service for FireWire unit
     */
    io_service_t getFWUnit() const { return fwUnit_; }

    /**
     * @brief Get the FireWire device service
     * @return io_service_t IOKit service for FireWire device
     */
    io_service_t getFWDevice() const { return fwDevice_; }

    /**
     * @brief Get the bus controller service
     * @return io_service_t IOKit service for bus controller
     */
    io_service_t getBusController() const { return busController_; }

    /**
     * @brief Get the notification port
     * @return IONotificationPortRef The IOKit notification port
     */
    IONotificationPortRef getNotificationPort() const { return notificationPort_; }

    /**
     * @brief Get the command interface
     * @return std::shared_ptr<CommandInterface> Interface for sending commands to the device
     */
    std::shared_ptr<CommandInterface> getCommandInterface() const { return commandInterface_; }

    // Prevent copying
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    // Make DeviceParser a friend
    friend class DeviceParser;

    /**
     * @brief Get the number of isochronous input plugs
     * @return uint32_t Number of iso input plugs
     */
    uint32_t getNumIsoInPlugs() const { return numIsoInPlugs; }

    /**
     * @brief Get the number of isochronous output plugs
     * @return uint32_t Number of iso output plugs
     */
    uint32_t getNumIsoOutPlugs() const { return numIsoOutPlugs; }

    /**
     * @brief Get the number of external input plugs
     * @return uint32_t Number of external input plugs
     */
    uint32_t getNumExtInPlugs() const { return numExtInPlugs; }

    /**
     * @brief Get the number of external output plugs
     * @return uint32_t Number of external output plugs
     */
    uint32_t getNumExtOutPlugs() const { return numExtOutPlugs; }
    
    // TODO: Implement this (maybe)
//    IOReturn getCurrentNodeID(UInt16* nodeId);
    
    IOFireWireLibDeviceRef getDeviceInterface() const { return deviceInterface; }

    io_service_t avcUnit_ = 0;  

private:
    std::uint64_t guid_;
    std::string deviceName_;
    std::string vendorName_;

    // Discovered plug counts
    uint32_t numIsoInPlugs = 0;
    uint32_t numIsoOutPlugs = 0;
    uint32_t numExtInPlugs = 0;
    uint32_t numExtOutPlugs = 0;

    // Capability flags.
    bool hasMusicSubunit = false;
    bool hasAudioSubunit = false;

    // Related FireWire objects (retrieved in init()).
    io_service_t fwUnit_ = 0;
    io_service_t fwDevice_ = 0;
    io_service_t busController_ = 0;
    io_object_t interestNotification_ = 0;
    
    // Interface
    IOFireWireLibDeviceRef deviceInterface = nullptr;
    IOFireWireAVCLibUnitInterface **avcInterface_ = nullptr;
    DeviceController *deviceController_ = nullptr;

    // IOKit notification port.
    IONotificationPortRef notificationPort_ = nullptr;

    // Command interface.
    std::shared_ptr<CommandInterface> commandInterface_;

    // Internal helper.
    /**
     * @brief Read vendor and model information from the device
     * @return IOReturn Status of the read operation
     */
    IOReturn readVendorAndModelInfo();
    
    IOReturn createFWDeviceInterface();

    // Device capabilities container
    DeviceInfo info_;

    friend class DeviceParser;
};

} // namespace FWA
