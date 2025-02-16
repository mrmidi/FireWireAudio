#pragma once

#include <memory>
#include <expected>
#include "Isoch/core/TransmitterTypes.hpp"
#include "FWA/Error.h"

namespace FWA {
namespace Isoch {

class ITransmitPacketProvider {
public:
    virtual ~ITransmitPacketProvider() = default;

    // Method called by client (e.g., XPC bridge) to push audio data INTO the provider
    virtual bool pushAudioData(const void* buffer, size_t bufferSizeInBytes) = 0;

    // Method called by AmdtpTransmitter to get data FOR a packet
    // It should read from its internal buffer (e.g., ring buffer) and write
    // formatted audio data directly into the provided targetBuffer.
    virtual PreparedPacketData fillPacketData(
        uint8_t* targetBuffer,          // Pointer to the DCL's client data area slot
        size_t targetBufferSize,        // Expected size based on config (e.g., 64 bytes)
        const TransmitPacketInfo& info  // Context about the packet being prepared
        ) = 0;

    // Optional: Check if provider has enough data buffered for smooth streaming
    virtual bool isReadyForStreaming() const = 0;

    // Optional: Reset internal buffer state
    virtual void reset() = 0;
};

} // namespace Isoch
} // namespace FWA