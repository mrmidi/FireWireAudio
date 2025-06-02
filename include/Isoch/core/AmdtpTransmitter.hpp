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
class IsochPortChannelManager; 
class IsochTransportManager; 
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

    inline PreparedPacketData safeFillAudio(uint8_t* dst, size_t len,
                                                         const TransmitPacketInfo& inf);


private:

        // --- LOGGING/DIAGNOSTICS ---
    std::atomic<uint64_t> dataPacketsSent_{0};
    std::atomic<uint64_t> noDataPacketsSent_{0};

    // We’ll also keep a timestamp so we only log once per second:
    std::chrono::steady_clock::time_point lastPacketLogTime_{ std::chrono::steady_clock::now() };

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
     void generateCIPHeaderContent(CIPHeader* outHeader,
                                   uint8_t current_dbc_state,
                                   bool previous_wasNoData_state,
                                   bool first_dcl_callback_occurred_state,
                                   uint8_t& next_dbc_for_state,
                                   bool& next_wasNoData_for_state);

     // --- Helper for the "Blocking" (UniversalTransmitter-style) SYT logic ---
     struct BlockingSytParams {
         bool isNoData;
         uint16_t syt_value;
     };
     BlockingSytParams calculateBlockingSyt();

     // --- Helper for the "NonBlocking" (current AmdtpTransmitter) SYT logic ---
     struct NonBlockingSytParams {
         bool isNoData;
         uint16_t syt_value;
         // Potentially other outputs specific to this strategy's state updates
     };
     NonBlockingSytParams calculateNonBlockingSyt(uint8_t current_dbc_state, bool previous_wasNoData_state);

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
     std::atomic<bool> firstDCLCallbackOccurred_{false};
     uint32_t expectedTimeStampCycle_{0}; // For timestamp checking

    // --- NEW: State variables specifically for "Blocking" (UniversalTransmitter-style) SYT ---
    uint16_t sytOffset_blocking_{TICKS_PER_CYCLE};
    uint32_t sytPhase_blocking_{0};

    // --- Constants for "Blocking" SYT (distinct from NonBlocking) ---
    static constexpr uint32_t SYT_PHASE_MOD_BLOCKING = 147;
    static constexpr uint32_t SYT_PHASE_RESET_BLOCKING = 147;
    // static constexpr uint32_t BASE_TICKS_BLOCKING = 1386;
    static constexpr uint32_t BASE_TICKS_BLOCKING = 565;
    // TICKS_PER_CYCLE is already defined

    // Client Callbacks
    MessageCallback messageCallback_{nullptr};
    void* messageCallbackRefCon_{nullptr};

    // Interface
    IOFireWireLibNubRef interface_{nullptr}; // The FireWire nub interface

    // Static constants for SYT calc (44.1kHz)
    static constexpr uint32_t SYT_PHASE_MOD = 147;
    static constexpr uint32_t SYT_PHASE_RESET = 1470;
    static constexpr uint32_t BASE_TICKS = 1386; // ~1/8 of TICKS_PER_CYCLE
    static constexpr uint32_t TICKS_PER_CYCLE = 3072;
};

} // namespace Isoch
} // namespace FWA
