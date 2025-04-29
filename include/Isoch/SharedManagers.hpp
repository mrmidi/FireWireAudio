// SharedManagers.hpp
#pragma once

#include <memory>
#include <expected>
#include "FWA/Error.h"
#include <spdlog/logger.h>
#include <IOKit/firewire/IOFireWireLibIsoch.h>
#include <functional>
//#include "Isoch/core/AmdtpDCLManager.hpp"

namespace FWA {
namespace Isoch {

// Forward declarations
class AmdtpPortManager;
class AmdtpTransportManager;

class AmdtpPortManager {
public:
    explicit AmdtpPortManager(std::shared_ptr<spdlog::logger> logger) : logger_(std::move(logger)) {}
    virtual ~AmdtpPortManager() = default;

    virtual std::expected<void, IOKitError> initialize(
        IOFireWireLibNubRef nubInterface,
        bool isTalker,
        DCLCommandPtr program,
        IOVirtualRange* bufferRange = nullptr,
        uint32_t numRanges = 0) = 0;

    virtual std::expected<void, IOKitError> configure(
        IOFWSpeed speed,
        uint32_t channel) = 0;

    virtual void resetPorts() = 0;

    // Get active channel configuration
    virtual std::expected<uint32_t, IOKitError> getActiveChannel() const = 0;

    // Get isoch channel interface - needed by transport manager
    virtual IOFireWireLibIsochChannelRef getIsochChannel() const = 0;

protected:
    virtual std::expected<void, IOKitError> createRemotePort() = 0;
    virtual std::expected<void, IOKitError> createLocalPort(DCLCommandPtr program) = 0;
    virtual std::expected<void, IOKitError> createIsochChannel() = 0;
    std::shared_ptr<spdlog::logger> logger_;
};

class AmdtpTransportManager {
public:
    enum class State {
        Stopped,
        Starting,
        Running,
        Stopping
    };

    using FinalizeCallback = std::function<void()>;

    explicit AmdtpTransportManager(std::shared_ptr<spdlog::logger> logger) 
        : logger_(std::move(logger)) {}
    virtual ~AmdtpTransportManager() = default;

    virtual std::expected<void, IOKitError> start(IOFireWireLibIsochChannelRef channel) = 0;
    virtual std::expected<void, IOKitError> stop(IOFireWireLibIsochChannelRef channel) = 0;
    virtual State getState() const noexcept = 0;
    virtual void handleFinalize() = 0;

protected:
    virtual std::expected<void, IOKitError> prepareStart() = 0;
    virtual std::expected<void, IOKitError> finishStop() = 0;
    std::shared_ptr<spdlog::logger> logger_;
};

} // namespace Isoch
} // namespace FWA
