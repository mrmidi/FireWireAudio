#pragma once

#include <memory>
#include <expected>
#include <atomic>
#include <mutex>
#include <vector>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/firewire/IOFireWireLibIsoch.h>
#include <spdlog/logger.h>

#include "FWA/Error.h"
#include "Isoch/core/TransmitterTypes.hpp"
#include "Isoch/interfaces/ITransmitBufferManager.hpp"
#include "Isoch/interfaces/ITransmitDCLManager.hpp"
#include "Isoch/interfaces/ITransmitPacketProvider.hpp"

// Forward declarations
namespace FWA {
namespace Isoch {
class IsochPortChannelManager; // Assume reusable
class IsochTransportManager; // Assume reusable
} }

namespace FWA {
namespace Isoch {

class AmdtpTransmitter : public std::enable_shared_from_this<AmdtpTransmitter> {
public:
    // Factory method
    static std::shared_ptr<AmdtpTransmitter> create(const TransmitterConfig& config);

    ~AmdtpTransmitter();

    // Prevent copy
    AmdtpTransmitter(const AmdtpTransmitter&) = delete;
    AmdtpTransmitter& operator=(const AmdtpTransmitter&) = delete;

    // Core lifecycle methods
    std::expected<void, IOKitError> initialize(IOFireWireLibNubRef interface);
    std::expected<void, IOKitError> configure(IOFWSpeed speed, uint32_t channel);
    std::expected<void, IOKitError> startTransmit();
    std::expected<void, IOKitError> stopTransmit();

    // Method for client to push data into the transmitter's provider
    bool pushAudioData(const void* buffer, size_t bufferSizeInBytes);

    // Set message callback
    void setMessageCallback(MessageCallback callback, void* refCon);

    // Get underlying runloop
    CFRunLoopRef getRunLoopRef() const { return runLoopRef_; }

    ITransmitPacketProvider* getPacketProvider() const;


private:
    // Private constructor for factory
    explicit AmdtpTransmitter(const TransmitterConfig& config);

    // Setup and cleanup
    std::expected<void, IOKitError> setupComponents(IOFireWireLibNubRef interface);
    void cleanup() noexcept;

    // Internal DCL callback handlers (instance methods)
    void handleDCLComplete(uint32_t completedGroupIndex);
    void handleDCLOverrun();

    // Static callback helpers (forward to instance methods)
    static void DCLCompleteCallback_Helper(uint32_t completedGroupIndex, void* refCon);
    static void DCLOverrunCallback_Helper(void* refCon);
    static void TransportFinalize_Helper(void* refCon); // If needed

     // CIP Header/Timing generation logic
     void initializeCIPState();
     void prepareCIPHeader(CIPHeader* outHeader); // Fills header based on state
     
     // Helper to send messages to the client
     void notifyMessage(TransmitterMessage msg, uint32_t p1 = 0, uint32_t p2 = 0);

    // Configuration & Logger
    TransmitterConfig config_;
    std::shared_ptr<spdlog::logger> logger_;

    // Manager Components
    std::unique_ptr<ITransmitBufferManager> bufferManager_;
    std::unique_ptr<IsochPortChannelManager> portChannelManager_; // Reusable
    std::unique_ptr<ITransmitDCLManager> dclManager_;
    std::unique_ptr<IsochTransportManager> transportManager_;     // Reusable
    std::unique_ptr<ITransmitPacketProvider> packetProvider_;

    // RunLoop
    CFRunLoopRef runLoopRef_{nullptr};

    // State
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    std::mutex stateMutex_;

     // CIP Header State
     uint8_t dbc_count_{0};
     bool wasNoData_{true}; // Start assuming previous was NoData
     uint16_t sytOffset_{0};
     uint32_t sytPhase_{0}; // For 44.1kHz calculation
     bool firstDCLCallbackOccurred_{false};
     uint32_t expectedTimeStampCycle_{0}; // For timestamp checking

    // Client Callbacks
    MessageCallback messageCallback_{nullptr};
    void* messageCallbackRefCon_{nullptr};

    // Static constants for SYT calc (example for 44.1)
    static constexpr uint32_t SYT_PHASE_MOD = 147;
    static constexpr uint32_t SYT_PHASE_RESET = 1470;
    static constexpr uint32_t BASE_TICKS = 1386; // ~1/8 of TICKS_PER_CYCLE
    static constexpr uint32_t TICKS_PER_CYCLE = 3072;
};

} // namespace Isoch
} // namespace FWA
