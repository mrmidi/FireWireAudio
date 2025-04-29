#pragma once
#include <memory>
#include <functional>
#include <expected>
#include <IOKit/firewire/IOFireWireLibIsoch.h>
#include "FWA/Error.h"
#include "Isoch/core/Types.hpp"

namespace FWA {
namespace Isoch {

// Forward declarations
class AmdtpTransmitter;
struct TransmitterConfig;

// Interface for packet management
class ITransmitPacketManager {
public:
    virtual ~ITransmitPacketManager() = default;
    virtual std::expected<void, IOKitError> processPacket(
        uint32_t segment,
        uint32_t cycle,
        uint8_t* data,
        size_t length) = 0;
    virtual std::expected<void, IOKitError> handleOverrun() = 0;
    virtual void setPacketCallback(std::function<void(uint8_t* data, size_t size)> callback) = 0;
};

// Interface for buffer management
class ITransmitBufferManager {
public:
    virtual ~ITransmitBufferManager() = default;
    virtual std::expected<void, IOKitError> setupBuffers(
        uint32_t totalCycles,
        uint32_t cycleBufferSize) = 0;
    virtual std::expected<uint8_t*, IOKitError> getCycleBuffer(
        uint32_t segment,
        uint32_t cycle) = 0;
    virtual std::expected<uint8_t*, IOKitError> getOverrunBuffer() = 0;
    virtual size_t getTotalBufferSize() const noexcept = 0;
};

// Interface for DCL management
class ITransmitDCLManager {
public:
    virtual ~ITransmitDCLManager() = default;
    virtual std::expected<void, IOKitError> createProgram(
        uint32_t cyclesPerSegment,
        uint32_t numSegments,
        uint32_t cycleBufferSize) = 0;
    virtual std::expected<void, IOKitError> handleSegmentComplete(uint32_t segment) = 0;
    virtual DCLCommandPtr getProgram() const = 0;
};

// Component factory interface
class ITransmitterComponentFactory {
public:
    virtual ~ITransmitterComponentFactory() = default;
    
    virtual std::shared_ptr<ITransmitBufferManager> createBufferManager(
        const TransmitterConfig& config) = 0;
        
    virtual std::shared_ptr<ITransmitPacketManager> createPacketManager(
        const TransmitterConfig& config,
        std::weak_ptr<AmdtpTransmitter> transmitter) = 0;
        
    virtual std::shared_ptr<ITransmitDCLManager> createDCLManager(
        const TransmitterConfig& config,
        std::weak_ptr<AmdtpTransmitter> transmitter) = 0;
};

} // namespace Isoch
} // namespace FWA