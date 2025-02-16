#include "FWA/CommandInterface.h"
#include "FWA/AudioDevice.h" 
#include "FWA/Error.h"
#include <spdlog/spdlog.h>
#include <sstream>
#include <iomanip>
#include <IOKit/IOMessage.h>

namespace FWA {

CommandInterface::CommandInterface(std::shared_ptr<AudioDevice> pAudioDevice)
    : pAudioDevice_(pAudioDevice)
    , avcUnit_(pAudioDevice->getAVCDevice())
    , avcInterface_(nullptr)
    , interestNotification_(0)
    , notificationCallback_(nullptr)
    , refCon_(nullptr)
{
}

CommandInterface::~CommandInterface()
{
    auto result = deactivate();
    if (!result) {
        spdlog::error("~CommandInterface, deactivate failed");
    }
}

CommandInterface::CommandInterface(CommandInterface&& other) noexcept
    : pAudioDevice_(std::move(other.pAudioDevice_))
    , avcUnit_(other.avcUnit_)
    , avcInterface_(other.avcInterface_)
    , interestNotification_(other.interestNotification_)
    , notificationCallback_(other.notificationCallback_)
    , refCon_(other.refCon_)
{
    other.pAudioDevice_ = nullptr;
    other.avcUnit_ = 0;
    other.avcInterface_ = nullptr;
    other.interestNotification_ = 0;
    other.notificationCallback_ = nullptr;
    other.refCon_ = nullptr;
}

CommandInterface& CommandInterface::operator=(CommandInterface&& other) noexcept
{
    if (this != &other) {
        deactivate();

        pAudioDevice_ = std::move(other.pAudioDevice_);
        avcUnit_ = other.avcUnit_;
        avcInterface_ = other.avcInterface_;
        interestNotification_ = other.interestNotification_;
        notificationCallback_ = other.notificationCallback_;
        refCon_ = other.refCon_;

        other.pAudioDevice_ = nullptr;
        other.avcUnit_ = 0;
        other.avcInterface_ = nullptr;
        other.interestNotification_ = 0;
        other.notificationCallback_ = nullptr;
        other.refCon_ = nullptr;
    }
    return *this;
}

std::expected<void, IOKitError> CommandInterface::activate() {
    if (avcInterface_ != nullptr) {
        return std::unexpected(static_cast<IOKitError>(kIOReturnStillOpen)); // Already active.
    }

    auto result = createAVCUnitInterface();
    if (!result) {
        return std::unexpected(result.error());
    }
    // Register the notification callback.
    return setNotificationCallback(notificationCallback_, refCon_);
}

std::expected<void, IOKitError> CommandInterface::deactivate()
{
    clearNotificationCallback();
    if (!avcInterface_) {
        return {}; // Already deactivated.
    }
    return releaseAVCUnitInterface();
}

std::expected<void, IOKitError> CommandInterface::createAVCUnitInterface()
{
    IOCFPlugInInterface **plugInInterface = nullptr;
    SInt32 score = 0;


    IOReturn result = IOCreatePlugInInterfaceForService(avcUnit_,
                                                        kIOFireWireAVCLibUnitTypeID,
                                                        kIOCFPlugInInterfaceID,
                                                        &plugInInterface,
                                                        &score);

    if (result != kIOReturnSuccess) {
        spdlog::error("Failed to create the CFPlugin interface: 0x{:x}", result);
        return std::unexpected(static_cast<IOKitError>(result));
    }

    HRESULT comResult = (*plugInInterface)->QueryInterface(plugInInterface,
                               CFUUIDGetUUIDBytes(kIOFireWireAVCLibUnitInterfaceID_v2),
                               (void**)&avcInterface_);

    (*plugInInterface)->Release(plugInInterface);

    if (comResult != S_OK || avcInterface_ == nullptr) {
        spdlog::error("Failed to get IOFireWireAVCLibUnitInterface: 0x{:x}", static_cast<int>(comResult));
        return std::unexpected(static_cast<IOKitError>(comResult));
    }

    return {};
}

std::expected<void, IOKitError> CommandInterface::releaseAVCUnitInterface()
{
    if (avcInterface_) {
        (*avcInterface_)->close(avcInterface_);
        (*avcInterface_)->Release(avcInterface_);
        avcInterface_ = nullptr;
    }
    return {};
}

std::expected<std::vector<uint8_t>, IOKitError> CommandInterface::sendCommand(
    const std::vector<uint8_t>& command)
{
    if (!avcInterface_) {
        return std::unexpected(static_cast<IOKitError>(kIOReturnNotOpen));
    }

    std::stringstream ss;
    ss << "Sending command: ";
    for (const auto& byte : command) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
    }
    spdlog::info("{}", ss.str());

    UInt32 cmdLen = static_cast<UInt32>(command.size());
    UInt32 respCapacity = 512;
    std::vector<uint8_t> response(respCapacity, 0);

    IOReturn result = (*avcInterface_)->AVCCommand(avcInterface_,
                                                    command.data(),
                                                    cmdLen,
                                                    response.data(),
                                                    &respCapacity);

    if (result != kIOReturnSuccess) {
        spdlog::error("Error sending command: 0x{:x}", result);
        return std::unexpected(static_cast<IOKitError>(result));
    }

    response.resize(respCapacity);

    ss.str("");
    ss.clear();
    ss << "Response: ";
    for (const auto& byte : response) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
    }
    spdlog::info("{}", ss.str());
    return response;
}

std::expected<void, IOKitError> CommandInterface::setNotificationCallback(DeviceStatusCallback callback, void* refCon)
{
    if (notificationCallback_) {
        return std::unexpected(static_cast<IOKitError>(kIOReturnExclusiveAccess));
    }
    notificationCallback_ = callback;
    refCon_ = refCon;

    io_object_t interestNotification = 0;
    auto result = IOServiceAddInterestNotification(
                           pAudioDevice_->getNotificationPort(),
                           avcUnit_,
                           kIOGeneralInterest,
                           deviceInterestCallback,
                           this,
                           &interestNotification
                           );

    if (result != kIOReturnSuccess) {
        spdlog::error("Failed to add interest notification: {}", result);
        return std::unexpected(static_cast<IOKitError>(result));
    }
    interestNotification_ = interestNotification;
    return {};
}

void CommandInterface::clearNotificationCallback()
{
    if (interestNotification_) {
        IOObjectRelease(interestNotification_);
        interestNotification_ = 0;
    }
    notificationCallback_ = nullptr;
    refCon_ = nullptr;
}

void CommandInterface::deviceInterestCallback(void* refCon, io_service_t service,
                                              natural_t messageType, void* messageArgument)
{
    CommandInterface* self = static_cast<CommandInterface*>(refCon);
    if (!self) return;

    if (messageType == kIOMessageServiceIsTerminated) {
        spdlog::info("Device terminated!");
        if (self->notificationCallback_) {
            self->notificationCallback_(self->pAudioDevice_, messageType, messageArgument);
        }
    }
}

} // namespace FWA
