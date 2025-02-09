#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <expected>
#include <IOKit/IOKitLib.h>           // For IOKit types.
#include <IOKit/firewire/IOFireWireLib.h>
#include "FWA/Error.h"
#include "FWA/DeviceInfo.hpp"

// Forward declarations
namespace FWA {
    class CommandInterface;
    class DeviceParser;
}

namespace FWA {

class AudioDevice : public std::enable_shared_from_this<AudioDevice> {
public:
    // Constructor: takes basic info and the AVC Unit.
    AudioDevice(std::uint64_t guid,
                const std::string& deviceName,
                const std::string& vendorName,
                io_service_t avcUnit);
    ~AudioDevice();

    // Initialization (should be called after creation via std::make_shared).
    std::expected<void, IOKitError> init();

    // Getters.
    std::uint64_t getGuid() const { return guid_; }
    const std::string& getDeviceName() const { return deviceName_; }
    const std::string& getVendorName() const { return vendorName_; }
    io_service_t getAVCDevice() const { return avcUnit_; }
    io_service_t getFWUnit() const { return fwUnit_; }
    io_service_t getFWDevice() const { return fwDevice_; }
    io_service_t getBusController() const { return busController_; }

    // Provide the IOKit notification port.
    IONotificationPortRef getNotificationPort() const { return notificationPort_; }

    std::shared_ptr<CommandInterface> getCommandInterface() const { return commandInterface_; }

    // Prevent copying.
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    // Make DeviceParser a friend
    friend class DeviceParser;

    // Expose discovered plug counts (for convenience)
    uint32_t getNumIsoInPlugs() const { return numIsoInPlugs; }
    uint32_t getNumIsoOutPlugs() const { return numIsoOutPlugs; }
    uint32_t getNumExtInPlugs() const { return numExtInPlugs; }
    uint32_t getNumExtOutPlugs() const { return numExtOutPlugs; }

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

    // IOKit notification port.
    IONotificationPortRef notificationPort_ = nullptr;

    // Command interface.
    std::shared_ptr<CommandInterface> commandInterface_;

    // Internal helper.
    IOReturn readVendorAndModelInfo();

    // Device capabilities container
    DeviceInfo info_;

    friend class DeviceParser;
};

} // namespace FWA