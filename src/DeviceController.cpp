// src/FWA/DeviceController.cpp

#include "FWA/DeviceController.h"
#include "FWA/Error.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <algorithm>

namespace FWA {

DeviceController::DeviceController(std::unique_ptr<IFireWireDeviceDiscovery> discovery) :
    discovery_(std::move(discovery)) {
}

DeviceController::~DeviceController() {
    stop(); // Ensure proper cleanup when the DeviceController is destroyed.
}
// receives callback
std::expected<void, IOKitError> DeviceController::start(DeviceNotificationCallback callback) {
    spdlog::info("DeviceController::start() called");
    if (isRunning_) {
        return std::unexpected(IOKitError(kIOReturnExclusiveAccess));
    }

    // Store the provided callback.  NO LONGER create a lambda here.
    notificationCallback_ = callback;

    // Start device discovery.  This sets up IOKit notifications.
    // Pass the stored callback directly to startDiscovery.
    auto result = discovery_->startDiscovery(notificationCallback_);
    if (!result) {
        spdlog::error("Failed to start discovery: 0x{:x}", static_cast<int>(result.error()));
        return std::unexpected(result.error());
    }

    isRunning_ = true; // Set isRunning_ *after* successful startDiscovery.
    return {}; // Return success. No need start any thread!

    // NO THREAD CREATION HERE.  Let IOKit handle notifications.
}

std::expected<void, IOKitError> DeviceController::stop() {
    if (!isRunning_) {
        return {}; // Already stopped/never started
    }

    isRunning_ = false; // Signal that we're stopping.

    auto result = discovery_->stopDiscovery();
    if (!result) {
        spdlog::error("Failed to stop discovery: 0x{:x}", static_cast<int>(result.error()));
        return std::unexpected(result.error());
    }

    {
        std::lock_guard<std::mutex> lock(devicesMutex_);
        devices_.clear();  // Clear the device list.
    }
     // Clear Callback
    notificationCallback_ = nullptr;
    return {};
}


void DeviceController::addDevice(std::shared_ptr<AudioDevice> device) {
    std::lock_guard<std::mutex> lock(devicesMutex_);
    auto it = std::find_if(devices_.begin(), devices_.end(),
                           [&](const auto& d) { return d->getGuid() == device->getGuid(); });
    if (it == devices_.end())
    {
        devices_.push_back(device);
        spdlog::info("Device added: {}", device->getGuid());
    } else {
        spdlog::info("Device already exists, updating: {}", device->getGuid());
          // *it = device; // We don't need to update existing device! We already setup Interest Notification!
    }
}

void DeviceController::removeDevice(std::uint64_t guid)
{
    std::lock_guard<std::mutex> lock(devicesMutex_);
	// No need to call close() here! It's handled by deviceInterestCallback.
    devices_.erase(std::remove_if(devices_.begin(), devices_.end(),
                                     [&](const auto& device) { return device->getGuid() == guid; }),
                       devices_.end());
    spdlog::info("Device removed: {}", guid);
}

std::expected<std::shared_ptr<AudioDevice>, IOKitError> DeviceController::getDeviceByGuid(std::uint64_t guid)
{
    std::lock_guard<std::mutex> lock(devicesMutex_);
    auto it = std::find_if(devices_.begin(), devices_.end(),
                           [&](const auto& device) { return device->getGuid() == guid; });

    if (it != devices_.end()) {
        return *it;
    } else {
        return std::unexpected(IOKitError(kIOReturnNotFound));
    }
}
} // namespace FWA