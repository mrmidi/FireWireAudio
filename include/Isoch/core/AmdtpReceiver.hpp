#pragma once

#include <memory>
#include <expected>
#include <atomic>
#include <vector>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/firewire/IOFireWireLibIsoch.h>
#include <spdlog/logger.h>
#include "FWA/Error.h"
#include "Isoch/core/ReceiverTypes.hpp"
#include "Isoch/utils/RingBuffer.hpp"

namespace FWA {
namespace Isoch {

// Forward declarations
class IsochDCLManager;
class IsochPortChannelManager;
class IsochBufferManager;
class IsochTransportManager;
class IsochPacketProcessor;
class IsochMonitoringManager;
class AudioClockPLL;

// namespace raul { class RingBuffer; }

/**
 * @brief AMDTP receiver for FireWire isochronous data reception
 */
class AmdtpReceiver : public std::enable_shared_from_this<AmdtpReceiver> {
public:
    /**
     * @brief Factory method to create an AmdtpReceiver instance
     * 
     * @param config Configuration parameters
     * @return std::shared_ptr<AmdtpReceiver> New receiver instance
     */
    static std::shared_ptr<AmdtpReceiver> create(const ReceiverConfig& config);
    
    /**
     * @brief Destructor - ensures proper resource cleanup
     */
    ~AmdtpReceiver();
    
    // Prevent copying
    AmdtpReceiver(const AmdtpReceiver&) = delete;
    AmdtpReceiver& operator=(const AmdtpReceiver&) = delete;
    
    /**
     * @brief Initialize the receiver with a FireWire interface
     * 
     * @param interface FireWire interface to use
     * @return std::expected<void, IOKitError> Success or error code
     */
    std::expected<void, IOKitError> initialize(IOFireWireLibNubRef interface);
    
    /**
     * @brief Configure the receiver with speed and channel
     * 
     * @param speed FireWire speed to use
     * @param channel Channel number to use
     * @return std::expected<void, IOKitError> Success or error code
     */
    std::expected<void, IOKitError> configure(IOFWSpeed speed, uint32_t channel);
    
    /**
     * @brief Start receiving data
     * 
     * @return std::expected<void, IOKitError> Success or error code
     */
    std::expected<void, IOKitError> startReceive();
    
    /**
     * @brief Stop receiving data
     * 
     * @return std::expected<void, IOKitError> Success or error code
     */
    std::expected<void, IOKitError> stopReceive();
    
    /**
     * @brief Set processed data callback for received samples
     * 
     * @param callback Function to call with processed sample data
     * @param refCon Context pointer to pass to the callback
     */
    void setProcessedDataCallback(ProcessedDataCallback callback, void* refCon);
    
    /**
     * @brief Set structured data callback for cycle data
     * 
     * @param callback Function to call with structured data
     * @param refCon Context pointer to pass to the callback
     */
    void setStructuredCallback(StructuredDataCallback callback, void* refCon);
    
    /**
     * @brief Set no-data callback for timeout conditions
     * 
     * @param callback Function to call on timeout
     * @param refCon Context pointer to pass to the callback
     * @param timeoutMs Timeout in milliseconds
     * @param cipOnlyMode True to consider CIP-only packets as no data
     */
    void setNoDataCallback(NoDataCallback callback, void* refCon, uint32_t timeoutMs, bool cipOnlyMode = true);
    
    /**
     * @brief Set message callback for status messages
     * 
     * @param callback Function to call with messages
     * @param refCon Context pointer to pass to the callback
     */
    void setMessageCallback(MessageCallback callback, void* refCon);
    
    /**
     * @brief Set group completion callback
     * 
     * @param callback Function to call when a group completes
     * @param refCon Context pointer to pass to the callback
     */
    void setGroupCompletionCallback(GroupCompletionCallback callback, void* refCon);
    
    /**
     * @brief Handle buffer group completion
     * 
     * @param groupIndex Group index that completed
     */
    void handleBufferGroupComplete(uint32_t groupIndex);
    
    /**
     * @brief Handle buffer overrun
     */
    void handleOverrun();
    
    /**
     * @brief Get the RunLoop used by the receiver
     * 
     * @return CFRunLoopRef The RunLoop
     */
    CFRunLoopRef getRunLoopRef() const { return runLoopRef_; }

    /**
     * @brief Get a pointer to the application ring buffer.
     * @return Pointer to the raul::RingBuffer, or nullptr if not initialized.
     */
    raul::RingBuffer* getAppRingBuffer() const; // <<< Declaration Added Here
    
private:
    /**
     * @brief Constructor - use create() factory method instead
     * 
     * @param config Configuration parameters
     */
    explicit AmdtpReceiver(const ReceiverConfig& config);
    
    /**
     * @brief Set up components
     * 
     * @param interface FireWire interface to use
     * @return std::expected<void, IOKitError> Success or error code
     */
    std::expected<void, IOKitError> setupComponents(IOFireWireLibNubRef interface);
    
    /**
     * @brief Clean up resources
     */
    void cleanup() noexcept;
    
    /**
     * @brief Synchronize host and FireWire clocks and initialize the PLL
     * 
     * Uses the FireWire device's GetCycleTimeAndUpTime function to get a 
     * precise correlation between host time and FireWire cycle time
     * 
     * @return std::expected<void, IOKitError> Success or error code
     */
    std::expected<void, IOKitError> synchronizeAndInitializePLL();
    
    /**
     * @brief Send message to client
     * 
     * @param msg Message code
     * @param param1 First parameter
     * @param param2 Second parameter
     */
    void notifyMessage(uint32_t msg, uint32_t param1 = 0, uint32_t param2 = 0);
    
    /**
     * @brief Handle overrun recovery
     * 
     * Stops the channel, fixes DCL targets, and restarts to recover from overrun
     */
    std::expected<void, IOKitError> handleOverrunRecovery();
    
    // Static callbacks with proper refcon passing
    static void handleDCLComplete(uint32_t groupIndex, void* refCon);
    static void handleDCLOverrun(void* refCon);
    static void handleTransportFinalize(void* refCon);
    
    // NEW static helper for processed data
    static void handleProcessedDataStatic(const std::vector<ProcessedSample>& samples,
                                         const PacketTimingInfo& timing,
                                         void* refCon);
    
    // Instance method for processed data
    void handleProcessedData(const std::vector<ProcessedSample>& samples,
                            const PacketTimingInfo& timing);
    
    // For structured callback forwarding
    static void handleStructuredCallback(const ReceivedCycleData& data, void* refCon);
    
    // For no-data callback forwarding
    static void handleNoDataCallback(uint32_t lastCycle, void* refCon);
    
    // Configuration
    ReceiverConfig config_;
    std::shared_ptr<spdlog::logger> logger_;
    
    // Components
    std::unique_ptr<IsochDCLManager> dclManager_;
    std::unique_ptr<IsochPortChannelManager> portChannelManager_;
    std::unique_ptr<IsochBufferManager> bufferManager_;
    std::unique_ptr<IsochTransportManager> transportManager_;
    std::unique_ptr<IsochPacketProcessor> packetProcessor_;
    std::unique_ptr<IsochMonitoringManager> monitoringManager_;
    
    // Future components (placeholders)
    std::unique_ptr<AudioClockPLL> pll_{nullptr};
    std::unique_ptr<class raul::RingBuffer> appRingBuffer_{nullptr};
    
    // RunLoop reference
    CFRunLoopRef runLoopRef_{nullptr};
    
    // Callbacks with proper refcons
    ProcessedDataCallback processedDataCallback_{nullptr};
    void* processedDataCallbackRefCon_{nullptr};
    
    StructuredDataCallback structuredCallback_{nullptr};
    void* structuredCallbackRefCon_{nullptr};
    
    NoDataCallback noDataCallback_{nullptr};
    void* noDataCallbackRefCon_{nullptr};
    
    MessageCallback messageCallback_{nullptr};
    void* messageCallbackRefCon_{nullptr};
    
    GroupCompletionCallback groupCompletionCallback_{nullptr};
    void* groupCompletionRefCon_{nullptr};
    
    // State
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    
    // Callback forwarding structures
    struct CallbackData {
        AmdtpReceiver* receiver;
        void* clientRefCon;
    };
    
    // To store callback data on heap
    std::vector<std::unique_ptr<CallbackData>> callbackDataStore_;
};

} // namespace Isoch
} // namespace FWA


