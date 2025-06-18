#pragma once

#include <memory>
#include <vector>
#include <expected>
#include "Isoch/core/TransmitterTypes.hpp"
#include "FWA/Error.h"
#include <IOKit/firewire/IOFireWireLibIsoch.h>

namespace FWA {
namespace Isoch {

// Forward declaration for ITransmitBufferManager
class ITransmitBufferManager;

// Callback function for DCL completion events (Transmitter specific)
using TransmitDCLCompleteCallback = void(*)(uint32_t completedGroupIndex, void* refCon);
using TransmitDCLOverrunCallback = void(*)(void* refCon);

class ITransmitDCLManager {
public:
    virtual ~ITransmitDCLManager() = default;

    virtual std::expected<DCLCommand*, IOKitError> createDCLProgram(
        const TransmitterConfig& config, // Pass config for packet structure info
        IOFireWireLibNuDCLPoolRef nuDCLPool,
        const ITransmitBufferManager& bufferManager) = 0;

    virtual std::expected<void, IOKitError> fixupDCLJumpTargets(
        IOFireWireLibLocalIsochPortRef localPort) = 0;

    virtual void setDCLCompleteCallback(TransmitDCLCompleteCallback callback, void* refCon) = 0;
    virtual void setDCLOverrunCallback(TransmitDCLOverrunCallback callback, void* refCon) = 0;

    // Method to update a specific DCL command before it's sent
    virtual std::expected<void, IOKitError> updateDCLPacket(
        uint32_t groupIndex,
        uint32_t packetIndexInGroup,
        const IOVirtualRange ranges[], // Array of ranges (CIP Header, Audio Data)
        uint32_t numRanges           // Number of ranges to set (1 for NO_DATA, 2 for DATA)
    ) = 0;

    // Method to notify the hardware about updated DCLs in a group
    virtual std::expected<void, IOKitError> notifyGroupUpdate(
        IOFireWireLibLocalIsochPortRef localPort, 
        const std::vector<NuDCLRef>& groupDCLs) = 0;

    // Method to get DCL reference for batching (Apple's architecture)
    virtual NuDCLSendPacketRef getDCLRef(uint32_t groupIndex, uint32_t packetIndexInGroup) = 0;

    virtual DCLCommand* getProgramHandle() const = 0; // Get the handle needed by Local Port
    virtual void reset() = 0;
};

} // namespace Isoch
} // namespace FWA