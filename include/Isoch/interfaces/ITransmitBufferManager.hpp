#pragma once

#include <memory>
#include <expected>
#include "Isoch/core/TransmitterTypes.hpp"
#include "FWA/Error.h"
#include <IOKit/firewire/IOFireWireLibIsoch.h> 

namespace FWA {
namespace Isoch {

class ITransmitBufferManager {
public:
    virtual ~ITransmitBufferManager() = default;

    virtual std::expected<void, IOKitError> setupBuffers(const TransmitterConfig& config) = 0;
    virtual void cleanup() noexcept = 0;

    // Getters for pre-allocated header/timestamp areas
    virtual std::expected<uint8_t*, IOKitError> getPacketIsochHeaderPtr(uint32_t groupIndex, uint32_t packetIndexInGroup) const = 0;
    virtual std::expected<uint8_t*, IOKitError> getPacketCIPHeaderPtr(uint32_t groupIndex, uint32_t packetIndexInGroup) const = 0;
    virtual std::expected<uint32_t*, IOKitError> getGroupTimestampPtr(uint32_t groupIndex) const = 0;

    // Getters for the client audio data area
    virtual uint8_t* getClientAudioBufferPtr() const = 0;
    virtual size_t getClientAudioBufferSize() const = 0;
    virtual size_t getAudioPayloadSizePerPacket() const = 0; // Calculated size based on config

    // Get overall range for port creation
    virtual const IOVirtualRange& getBufferRange() const = 0;
    virtual size_t getTotalBufferSize() const = 0;
};

} // namespace Isoch
} // namespace FWA