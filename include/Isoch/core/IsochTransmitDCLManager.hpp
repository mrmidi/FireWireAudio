#pragma once

#include "Isoch/interfaces/ITransmitDCLManager.hpp"
#include "Isoch/interfaces/ITransmitBufferManager.hpp"
#include "Isoch/core/TransmitterTypes.hpp"
#include <vector>
#include <mutex>
#include <atomic>
#include <spdlog/spdlog.h> // Using main spdlog header instead of just logger.hpp
#include <CoreFoundation/CFSet.h> // For CFMutableSetRef

namespace FWA {
namespace Isoch {

class IsochTransmitDCLManager : public ITransmitDCLManager {
public:
    explicit IsochTransmitDCLManager(std::shared_ptr<spdlog::logger> logger);
    ~IsochTransmitDCLManager() override;

    // Prevent Copy
    IsochTransmitDCLManager(const IsochTransmitDCLManager&) = delete;
    IsochTransmitDCLManager& operator=(const IsochTransmitDCLManager&) = delete;


    std::expected<DCLCommand*, IOKitError> createDCLProgram(
        const TransmitterConfig& config,
        IOFireWireLibNuDCLPoolRef nuDCLPool,
        const ITransmitBufferManager& bufferManager) override;

    std::expected<void, IOKitError> fixupDCLJumpTargets(
        IOFireWireLibLocalIsochPortRef localPort) override;

    void setDCLCompleteCallback(TransmitDCLCompleteCallback callback, void* refCon) override;
    void setDCLOverrunCallback(TransmitDCLOverrunCallback callback, void* refCon) override;

    std::expected<void, IOKitError> updateDCLPacket(
        uint32_t groupIndex,
        uint32_t packetIndexInGroup,
        const IOVirtualRange ranges[],
        uint32_t numRanges
    ) override;

     std::expected<void, IOKitError> notifySegmentUpdate(
         IOFireWireLibLocalIsochPortRef localPort,
         uint32_t groupIndexToNotify) override;

    DCLCommand* getProgramHandle() const override;
    void reset() override;

private:
    // Structure to hold info passed to static callbacks
     struct DCLCallbackInfo {
         IsochTransmitDCLManager* manager = nullptr;
         uint32_t groupIndex = 0;
     };

    // Static callbacks
    static void DCLComplete_Helper(void* refcon, NuDCLRef dcl);
    static void DCLOverrun_Helper(void* refcon, NuDCLRef dcl);

    // Instance handlers
    void handleDCLComplete(uint32_t groupIndex, NuDCLRef dcl);
    void handleDCLOverrun(NuDCLRef dcl);

    // Internal helpers
    NuDCLSendPacketRef getDCLRef(uint32_t groupIndex, uint32_t packetIndexInGroup); // Removed const qualifier
    IOReturn notifyDCLUpdates(IOFireWireLibLocalIsochPortRef localPort, NuDCLRef dcls[], uint32_t count);
    IOReturn notifyJumpUpdate(IOFireWireLibLocalIsochPortRef localPort, NuDCLRef* dclRefPtr);


    std::shared_ptr<spdlog::logger> logger_;
    TransmitterConfig config_; // Store config
    IOFireWireLibNuDCLPoolRef nuDCLPool_{nullptr}; // Non-owning

    // DCL Program Structure
    std::vector<NuDCLSendPacketRef> dclProgramRefs_; // Stores all DCLs
    NuDCLSendPacketRef firstDCLRef_{nullptr};
    NuDCLSendPacketRef lastDCLRef_{nullptr};
    NuDCLSendPacketRef overrunDCL_{nullptr};
    std::vector<DCLCallbackInfo> callbackInfos_; // Store refcon data for each group
     std::vector<CFMutableSetRef> updateBags_; // Update bags per segment completion DCL

    // State
    bool dclProgramCreated_{false};
    std::atomic<uint32_t> currentSegment_{0}; // Maybe needed for jump target logic

    // Callbacks
    TransmitDCLCompleteCallback dclCompleteCallback_{nullptr};
    void* dclCompleteRefCon_{nullptr};
    TransmitDCLOverrunCallback dclOverrunCallback_{nullptr};
    void* dclOverrunRefCon_{nullptr};

    std::mutex mutex_;
};

} // namespace Isoch
} // namespace FWA