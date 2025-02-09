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

using DeviceNotificationCallback = std::function<void(std::shared_ptr<AudioDevice>, bool /* connected */)>;

class IFireWireDeviceDiscovery {
public:
    virtual ~IFireWireDeviceDiscovery() = default;
    
    // Use std::expected instead of Result
    virtual std::expected<void, IOKitError> startDiscovery(DeviceNotificationCallback callback) = 0;
    virtual std::expected<void, IOKitError> stopDiscovery() = 0;
    virtual std::expected<std::vector<std::shared_ptr<AudioDevice>>, IOKitError> getConnectedDevices() = 0;
    virtual std::expected<std::shared_ptr<AudioDevice>, IOKitError> getDeviceByGuid(std::uint64_t guid) = 0;
};

} // namespace FWA
