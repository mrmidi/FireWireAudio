// AmdtpTransmitter.hpp
#pragma once

#include <memory>
#include <expected>
#include <atomic>
#include <mutex>
#include <vector>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/firewire/IOFireWireLibIsoch.h>
#include <spdlog/logger.h>
#include <iomanip> // For std::setw, std::setfill

#include "FWA/Error.h"
#include "Isoch/core/CIPHeader.hpp"
#include "Isoch/core/TransmitterTypes.hpp"
#include "Isoch/interfaces/ITransmitBufferManager.hpp"
#include "Isoch/interfaces/ITransmitDCLManager.hpp"
#include "Isoch/interfaces/ITransmitPacketProvider.hpp"
#include "Isoch/core/AppleSyTGenerator.hpp"

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
     void generateCIPHeaderContent(FWA::Isoch::CIPHeader* outHeader,
                                   uint8_t current_dbc_state,
                                   bool previous_wasNoData_state,
                                   bool first_dcl_callback_occurred_state,
                                   uint8_t& next_dbc_for_state,
                                   bool& next_wasNoData_for_state);

     // --- New refactored CIP header generation helper ---
     void generateCIPHeaderContent_from_decision(FWA::Isoch::CIPHeader* outHeader,
                                                 bool isNoDataPacket,
                                                 uint16_t sytValueForDataPacket,
                                                 uint8_t current_dbc_state,
                                                 uint8_t& next_dbc_for_state,
                                                 bool& next_wasNoData_for_state);


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
     std::atomic<bool> firstDCLCallbackOccurred_{false};
     uint32_t expectedTimeStampCycle_{0}; // For timestamp checking

    // --- Apple SYT Generator for "Blocking" (Apple-style) SYT ---
    std::unique_ptr<AppleSyTGenerator> m_appleSyTGenerator;

    // Client Callbacks
    MessageCallback messageCallback_{nullptr};
    void* messageCallbackRefCon_{nullptr};

    // Interface
    IOFireWireLibNubRef interface_{nullptr}; // The FireWire nub interface

    // --- Packet Logging Helper ---
    // Counter to decide when to log a packet
    std::atomic<uint64_t> packetLogCounter_{0};
    // How often to log a full packet dump (e.g., every 10000th generated packet)
    static constexpr uint64_t PACKET_LOG_INTERVAL = 10000;

    // Helper function to log packet details
    void logPacketDetails(
        uint32_t groupIndex,
        uint32_t packetIndexInGroup,
        const FWA::Isoch::CIPHeader* cipHeader,
        const uint8_t* audioPayload,
        size_t audioPayloadSize,
        const TransmitPacketInfo& packetInfo // For context
    );

    // Helper to log packet patterns for verification against Apple Duet capture
    void logPacketPattern(const FWA::Isoch::CIPHeader* cipHeader);

};




} // namespace Isoch
} // namespace FWA
