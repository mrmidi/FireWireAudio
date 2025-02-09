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

class DeviceController {
public:
    DeviceController(std::unique_ptr<IFireWireDeviceDiscovery> discovery);
    ~DeviceController();

    // start() now takes the callback as a parameter:
    std::expected<void, IOKitError> start(DeviceNotificationCallback callback);
    std::expected<void, IOKitError> stop();
    std::expected<std::shared_ptr<AudioDevice>, IOKitError> getDeviceByGuid(std::uint64_t guid);

private:

    std::unique_ptr<IFireWireDeviceDiscovery> discovery_;
    std::vector<std::shared_ptr<AudioDevice>> devices_;
    std::mutex devicesMutex_;
    DeviceNotificationCallback notificationCallback_;  // Stored callback.
    std::atomic<bool> isRunning_{false};


    void addDevice(std::shared_ptr<AudioDevice> device);
    void removeDevice(std::uint64_t guid);
};

} // namespace FWA