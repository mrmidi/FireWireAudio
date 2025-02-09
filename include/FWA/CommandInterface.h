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

// Forward declaration.
class AudioDevice;

// Callback type for notifications.
using DeviceStatusCallback = std::function<void(std::shared_ptr<AudioDevice>, natural_t messageType, void* messageArgument)>;

class CommandInterface {
public:
    // Constructor now takes a shared_ptr to AudioDevice by value.
    explicit CommandInterface(std::shared_ptr<AudioDevice> pAudioDevice);
    ~CommandInterface();

    // Delete copying.
    CommandInterface(const CommandInterface&) = delete;
    CommandInterface& operator=(const CommandInterface&) = delete;

    // Allow moving.
    CommandInterface(CommandInterface&&) noexcept;
    CommandInterface& operator=(CommandInterface&&) noexcept;

    std::expected<void, IOKitError> setNotificationCallback(DeviceStatusCallback callback, void* refCon);
    void clearNotificationCallback();

    std::expected<void, IOKitError> activate();
    std::expected<void, IOKitError> deactivate();

    std::expected<std::vector<uint8_t>, IOKitError> sendCommand(const std::vector<uint8_t>& command);

    IOFireWireAVCLibUnitInterface** getAvcInterface() const { return avcInterface_; }
    io_service_t getAVCUnit() const { return avcUnit_; }

private:
    std::expected<void, IOKitError> createAVCUnitInterface();
    std::expected<void, IOKitError> releaseAVCUnitInterface();

    static void deviceInterestCallback(void* refCon, io_service_t service, natural_t messageType, void* messageArgument);

private:
    std::shared_ptr<AudioDevice> pAudioDevice_;
    io_service_t avcUnit_ = 0;
    IOFireWireAVCLibUnitInterface** avcInterface_ = nullptr;
    io_object_t interestNotification_ = 0;
    DeviceStatusCallback notificationCallback_;
    void* refCon_ = nullptr;
};

} // namespace FWA