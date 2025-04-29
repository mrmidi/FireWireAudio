#pragma once

#include <memory>
#include <expected>
#include <functional>
#include <atomic>   // For atomic state variables
#include <vector>   // For ProcessedSample vector
#include <spdlog/logger.h>
#include "FWA/Error.h"
#include "Isoch/core/ReceiverTypes.hpp"
#include "Isoch/core/IsochBufferManager.hpp" // Include buffer manager for constants

namespace FWA {
namespace Isoch {

/**
 * @brief Processor for FireWire isochronous packets
 * 
 * This class handles processing received packets, extracting data,
 * and forwarding to client code with proper refcon preservation.
 */
class IsochPacketProcessor {
public:
    /**
     * @brief Set callback for overrun events
     * 
     * @param callback Function to call on overrun
     * @param refCon Context pointer to pass to the callback
     */
    using OverrunCallback = void(*)(void* refCon);
    
    /**
     * @brief Construct a new IsochPacketProcessor
     * 
     * @param logger Logger for diagnostic information
     */
    explicit IsochPacketProcessor(std::shared_ptr<spdlog::logger> logger);
    
    /**
     * @brief Destructor
     */
    ~IsochPacketProcessor() = default;
    
    // Prevent copying
    IsochPacketProcessor(const IsochPacketProcessor&) = delete;
    IsochPacketProcessor& operator=(const IsochPacketProcessor&) = delete;
    
    /**
     * @brief Set callback for processed data
     * 
     * @param callback Function to call with processed sample data
     * @param refCon Context pointer to pass to the callback
     */
    void setProcessedDataCallback(ProcessedDataCallback callback, void* refCon);

    /**
     * @brief Set callback for overrun events
     * 
     * @param callback Function to call on overrun
     * @param refCon Context pointer to pass to the callback
     */
    void setOverrunCallback(OverrunCallback callback, void* refCon);
    
    /**
     * @brief Process a received packet with separate pointers for headers and data
     * 
     * @param groupIndex Group index
     * @param packetIndexInGroup Packet index within the group
     * @param isochHeader Pointer to 4-byte Isoch Header
     * @param cipHeader Pointer to 8-byte CIP Header
     * @param packetData Pointer to actual audio data payload
     * @param packetDataLength Length of packetData
     * @param timestamp Timestamp value for this packet
     * @return std::expected<void, IOKitError> Success or error code
     */
    std::expected<void, IOKitError> processPacket(
        uint32_t groupIndex,
        uint32_t packetIndexInGroup,
        const uint8_t* isochHeader,
        const uint8_t* cipHeader,
        const uint8_t* packetData,
        size_t packetDataLength,
        uint32_t timestamp);
    
    /**
     * @brief Legacy method for backwards compatibility
     * 
     * @deprecated Use the new processPacket with separate pointers instead
     */
    std::expected<void, IOKitError> processPacket(
        uint32_t segment,
        uint32_t cycle,
        const uint8_t* data,
        size_t length);
    
    /**
     * @brief Handle packet overrun condition
     * 
     * @return std::expected<void, IOKitError> Success or error code
     */
    std::expected<void, IOKitError> handleOverrun();
    
private:
    // Callback info
    ProcessedDataCallback processedDataCallback_{nullptr};
    void* processedDataCallbackRefCon_{nullptr};
    OverrunCallback overrunCallback_{nullptr};
    void* overrunCallbackRefCon_{nullptr};

    // Internal State
    std::shared_ptr<spdlog::logger> logger_;
    uint8_t expectedDBC_{0}; 
    bool dbcInitialized_{false};
    uint64_t currentAbsSampleIndex_{0};
    bool sampleIndexInitialized_{false};
    uint32_t lastPacketNumDataBlocks_{0}; // Track number of blocks in the previous packet
    bool lastPacketWasNoData_{false}; // Track if the *immediately preceding* processed packet was NO_DATA
    
    /**
     * @brief Extract SFC (Sample Frequency Code) from FDF field
     * 
     * @param fdf Format Dependent Field from CIP header
     * @return uint8_t Sample Frequency Code
     */
    static uint8_t getSFCFromFDF(uint8_t fdf) {
        // Basic FDF for AM824: lower 3 bits are SFC
        return fdf & 0x07;
    }
};

} // namespace Isoch
} // namespace FWA