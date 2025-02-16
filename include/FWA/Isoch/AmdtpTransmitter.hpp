#pragma once
#include <cstdint>
#include <memory>
#include <functional>
#include <expected>
#include <vector>
#include <array>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <IOKit/firewire/IOFireWireLib.h>
#include <spdlog/spdlog.h>

// IOKit includes
#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/firewire/IOFireWireLibIsoch.h>
#include <IOKit/firewire/IOFireWireFamilyCommon.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>


// Project includes
#include "FWA/Error.h"
#include "IsochBufferManager.hpp"
#include "CIPHeaderHandler.hpp"
#include "AmdtpHelpers.hpp"  // Now we use shared helper methods

// Forward declarations for FireWire interface creation helpers.
IOReturn GetFireWireLocalNodeInterface(IOFireWireLibNubRef *fireWireLocalNodeInterface);
IOReturn GetFireWireDeviceInterfaceFromExistingInterface(IOFireWireLibDeviceRef existingDeviceInterface, 
                                                           IOFireWireLibDeviceRef *newDeviceInterface);

namespace FWA {
namespace Isoch {

/**
 * @brief Message types for AMDTP transmission events
 */
enum class AmdtpMessageType {
    AllocateIsochPort,
    ReleaseIsochPort,
    TimeStampAdjust,
    DCLOverrunAutoRestartFailed,
    BadBufferRange,
    DataPull  // For client data requests
};

/**
 * @brief Callback types for AMDTP transmission
 */
using MessageCallback = std::function<void(AmdtpMessageType, uint32_t, uint32_t)>;
using FinalizeCallback = std::function<void()>;
using DataPullCallback = std::function<void(void* refcon)>;

/**
 * @brief Manages AMDTP (Audio & Music Data Transmission Protocol) over FireWire.
 */
class AmdtpTransmitter {
public:
    AmdtpTransmitter(std::shared_ptr<spdlog::logger> logger,
                     IOFireWireLibNubRef nubInterface,
                     uint32_t cyclesPerSegment,
                     uint32_t numSegments,
                     uint32_t clientBufferSize,
                     uint32_t sampleRate = 48000,
                     uint32_t numChannels = 2,
                     bool doIRMAlloc = true,
                     uint32_t irmPacketSize = 72,
                     uint32_t cycleMatchBits = 0);

    ~AmdtpTransmitter();

    // Prevent copying and moving
    AmdtpTransmitter(const AmdtpTransmitter&) = delete;
    AmdtpTransmitter& operator=(const AmdtpTransmitter&) = delete;
    AmdtpTransmitter(AmdtpTransmitter&&) = delete;
    AmdtpTransmitter& operator=(AmdtpTransmitter&&) = delete;

    // Core operations
    std::expected<void, FWA::IOKitError> setupTransmitter();
    std::expected<void, FWA::IOKitError> startTransmit();
    std::expected<void, FWA::IOKitError> stopTransmit();

    // Configuration
    std::expected<void, FWA::IOKitError> setTransmitSpeed(IOFWSpeed speed);
    std::expected<void, FWA::IOKitError> setTransmitChannel(unsigned int channel);
    
    // Callback registration
    void setMessageCallback(MessageCallback callback) noexcept {
        messageCallback_ = std::move(callback);
    }
    
    void setFinalizeCallback(FinalizeCallback callback) noexcept {
        finalizeCallback_ = std::move(callback);
    }
    
    void setDataPullCallback(DataPullCallback callback, void* refcon = nullptr) noexcept {
        dataPullCallback_ = std::move(callback);
        dataPullRefCon_ = refcon;
    }

    // Buffer access
    uint8_t* getClientBuffer() const noexcept;
    uint32_t getClientBufferSize() const noexcept;

private:
    // Core components
    std::shared_ptr<spdlog::logger> logger_;
    std::unique_ptr<IsochBufferManager> bufferManager_;
    std::unique_ptr<CIPHeaderHandler> cipHandler_;

    // FireWire interfaces
    IOFireWireLibNubRef nodeNubInterface_{nullptr};
    IOFireWireLibRemoteIsochPortRef remoteIsocPort_{nullptr};
    IOFireWireLibLocalIsochPortRef localIsocPort_{nullptr};
    IOFireWireLibNuDCLPoolRef nuDCLPool_{nullptr};
    IOFireWireLibIsochChannelRef isochChannel_{nullptr};
    CFRunLoopRef runLoopRef_{nullptr};

    // Configuration
    uint32_t cyclesPerSegment_;
    uint32_t numSegments_;
    uint32_t clientBufferSize_;
    uint32_t sampleRate_;
    uint32_t numChannels_;
    bool doIRM_;
    uint32_t irmPacketSize_;
    uint32_t cycleMatchBits_;
    uint32_t startupCycleMatchMask_{0};

    // Speed/Channel settings
    IOFWSpeed transmitSpeed_{kFWSpeed100MBit};
    unsigned int transmitChannel_{0};

    // Thread synchronization
    std::mutex transportMutex_;
    std::atomic<bool> transportPlaying_{false};
    std::atomic<bool> finalizeCallbackCalled_{false};

    // Callbacks
    MessageCallback messageCallback_;
    FinalizeCallback finalizeCallback_;
    DataPullCallback dataPullCallback_;
    void* dataPullRefCon_{nullptr};

    // DCL program management
    struct DCLProgram {
        std::vector<NuDCLSendPacketRef> programDCLs;
        std::vector<CFMutableSetRef> segmentUpdateBags; // Not used in new logic
        NuDCLSendPacketRef overrunDCL{nullptr};
    };
    std::unique_ptr<DCLProgram> dclProgram_;

    // Cycle management
    struct CycleInfo {
        std::array<IOVirtualRange, 3> ranges;  // CIP header + stereo audio
        uint32_t numRanges{1};
        uint32_t index{0};
        uint8_t sy{0};
        uint8_t tag{0};
        uint16_t nodeID{0};
        uint64_t expectedTransmitCycleTime{0};
        uint64_t transmitTimeInNanoSeconds{0};
    };
    std::vector<CycleInfo> cycleInfos_;

    // State tracking
    uint32_t currentSegment_{0};
    std::atomic<bool> firstDCLCallbackOccurred_{false};
    uint32_t expectedTimeStampCycle_{0};
    uint64_t currentFireWireCycleTime_{0};
    uint64_t currentUpTimeInNanoSecondsU64_{0};

    // Setup helpers
    std::expected<void, FWA::IOKitError> initializeFireWireInterface();
    std::expected<void, FWA::IOKitError> createNuDCLPool();
    std::expected<void, FWA::IOKitError> createRemoteIsochPort();
    std::expected<void, FWA::IOKitError> createLocalIsochPort();
    std::expected<void, FWA::IOKitError> createIsochChannel();
    std::expected<void, FWA::IOKitError> setupChannelConnections();
    std::expected<void, FWA::IOKitError> createDCLProgram();

    // DCL and cycle management
    std::expected<void, FWA::IOKitError> fillCycleBuffer(NuDCLSendPacketRef dcl, 
                                                    uint16_t nodeID,
                                                    uint32_t segment, 
                                                    uint32_t cycle);

    // Callback handlers
    void handleDCLCallback();
    void handleOverrunCallback();
    void handleFinalize();
    void notifyCallback(AmdtpMessageType type, uint32_t param1 = 0, uint32_t param2 = 0) const;

    // Static C-style callback wrappers
    static IOReturn finalizeCallbackWrapper(void* refcon);
    static void dclCallbackWrapper(void* refcon, NuDCLRef dcl);
    static void dclOverrunCallbackWrapper(void* refcon, NuDCLRef dcl);

    // --- Remote Port Helpers as static member functions ---
    static IOReturn remotePortGetSupportedHelper(IOFireWireLibIsochPortRef interface,
                                                   IOFWSpeed* outMaxSpeed,
                                                   UInt64* outChanSupported);
    static IOReturn remotePortAllocatePortHelper(IOFireWireLibIsochPortRef interface,
                                                   IOFWSpeed maxSpeed,
                                                   UInt32 channel);
    static IOReturn remotePortReleasePortHelper(IOFireWireLibIsochPortRef interface);
    static IOReturn remotePortStartHelper(IOFireWireLibIsochPortRef interface);
    static IOReturn remotePortStopHelper(IOFireWireLibIsochPortRef interface);

    // --- Remote Port Handlers (instance methods) ---
    IOReturn remotePortGetSupported(IOFireWireLibIsochPortRef interface,
                                    IOFWSpeed* outMaxSpeed,
                                    uint64_t* outChanSupported);
    IOReturn remotePortAllocatePort(IOFireWireLibIsochPortRef interface,
                                    IOFWSpeed maxSpeed,
                                    uint32_t channel);
    IOReturn remotePortReleasePort(IOFireWireLibIsochPortRef interface);
    IOReturn remotePortStart(IOFireWireLibIsochPortRef interface);
    IOReturn remotePortStop(IOFireWireLibIsochPortRef interface);

    // --- Resource cleanup ---
    void cleanup() noexcept;
};

} // namespace Isoch
} // namespace FWA
