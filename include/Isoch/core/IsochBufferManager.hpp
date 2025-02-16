#pragma once

#include <memory>
#include <expected>
#include <cstdint>
#include <spdlog/logger.h>
#include <IOKit/firewire/IOFireWireLibIsoch.h>
#include "FWA/Error.h"

namespace FWA {
namespace Isoch {

/**
 * @brief Manages buffer allocation and access for isochronous communication
 * 
 * This class handles allocation and management of memory buffers for 
 * isochronous transport, including packet data, headers, and timestamps.
 * It ensures proper structure and alignment for FireWire DMA operations.
 */
class IsochBufferManager {
public:
    /**
     * @brief Configuration for the buffer structure
     */
    struct Config {
        uint32_t numGroups{8};         // Total number of buffer groups
        uint32_t packetsPerGroup{16};  // Number of FW packets per group
        uint32_t packetDataSize{64};   // Bytes of audio data per FW packet
    };

    /**
     * @brief Construct a new IsochBufferManager
     * 
     * @param logger Logger for diagnostic information
     */
    explicit IsochBufferManager(std::shared_ptr<spdlog::logger> logger);
    
    /**
     * @brief Destructor - ensures proper cleanup of allocated memory
     */
    ~IsochBufferManager();
    
    // Prevent copying
    IsochBufferManager(const IsochBufferManager&) = delete;
    IsochBufferManager& operator=(const IsochBufferManager&) = delete;
    
    /**
     * @brief Setup buffers for isoch communication
     * 
     * @param config Buffer configuration
     * @return std::expected<void, IOKitError> Success or error code
     */
    std::expected<void, IOKitError> setupBuffers(const Config& config);
    
    /**
     * @brief Get pointer to timestamp for a specific packet
     * 
     * @param groupIndex Group index
     * @param packetIndexInGroup Packet index within the group
     * @return std::expected<uint32_t*, IOKitError> Pointer to timestamp or error
     */
    std::expected<uint32_t*, IOKitError> getPacketTimestampPtr(
        uint32_t groupIndex, uint32_t packetIndexInGroup) const;
    
    /**
     * @brief Get pointer to isoch header for a specific packet
     * 
     * @param groupIndex Group index
     * @param packetIndexInGroup Packet index within the group
     * @return std::expected<uint8_t*, IOKitError> Pointer to isoch header or error
     */
    std::expected<uint8_t*, IOKitError> getPacketIsochHeaderPtr(
        uint32_t groupIndex, uint32_t packetIndexInGroup) const;
    
    /**
     * @brief Get pointer to CIP header for a specific packet
     * 
     * @param groupIndex Group index
     * @param packetIndexInGroup Packet index within the group
     * @return std::expected<uint8_t*, IOKitError> Pointer to CIP header or error
     */
    std::expected<uint8_t*, IOKitError> getPacketCIPHeaderPtr(
        uint32_t groupIndex, uint32_t packetIndexInGroup) const;
    
    /**
     * @brief Get pointer to packet data for a specific packet
     * 
     * @param groupIndex Group index
     * @param packetIndexInGroup Packet index within the group
     * @return std::expected<uint8_t*, IOKitError> Pointer to packet data or error
     */
    std::expected<uint8_t*, IOKitError> getPacketDataPtr(
        uint32_t groupIndex, uint32_t packetIndexInGroup) const;
    
    /**
     * @brief TEMPORARY GETTER for the start of the raw packet slot
     * 
     * @param groupIndex Group index
     * @param packetIndexInGroup Packet index within the group
     * @return std::expected<uint8_t*, IOKitError> Pointer to raw packet slot or error
     */
    std::expected<uint8_t*, IOKitError> getRawPacketSlotPtr(
        uint32_t groupIndex, uint32_t packetIndexInGroup) const;
    
    /**
     * @brief Get total size expected per packet (IsochHdr + CIPP Hdr + Data)
     * 
     * @return size_t Total size per packet in bytes
     */
    size_t getTotalPacketSize() const {
        return kIsochHeaderSize + kCIPHeaderSize + config_.packetDataSize;
    }
    
    /**
     * @brief Get buffer range for DMA operations
     * 
     * @return const IOVirtualRange& Buffer range
     */
    const IOVirtualRange& getBufferRange() const { return bufferRange_; }
    
    /**
     * @brief Get total size of allocated buffer
     * 
     * @return size_t Total buffer size in bytes
     */
    size_t getTotalBufferSize() const { return totalBufferSize_; }
    
    /**
     * @brief Get number of groups
     * 
     * @return uint32_t Number of buffer groups
     */
    uint32_t getNumGroups() const { return config_.numGroups; }
    
    /**
     * @brief Get packets per group
     * 
     * @return uint32_t Packets per group
     */
    uint32_t getPacketsPerGroup() const { return config_.packetsPerGroup; }
    
    /**
     * @brief Get packet data size
     * 
     * @return uint32_t Size of data part of each packet in bytes
     */
    uint32_t getPacketDataSize() const { return config_.packetDataSize; }
    
    /**
     * @brief Constants for header sizes
     */
    static constexpr size_t kIsochHeaderSize = 4;   // Size of isoch header in bytes
    static constexpr size_t kCIPHeaderSize = 8;     // Size of CIP header in bytes
    
private:
    void cleanup() noexcept;
    void calculateBufferLayout();
    
    std::shared_ptr<spdlog::logger> logger_;
    Config config_;
    uint32_t totalPackets_{0};
    
    // Buffer management
    uint8_t* mainBuffer_{nullptr};
    size_t totalBufferSize_{0};
    IOVirtualRange bufferRange_{};
    
    // Pointers into mainBuffer_
    uint8_t* isochHeaderArea_{nullptr};
    uint8_t* cipHeaderArea_{nullptr};
    uint8_t* packetDataArea_{nullptr};
    uint32_t* timestampArea_{nullptr};
    
    // Buffer section sizes
    size_t isochHeaderTotalSize_{0};
    size_t cipHeaderTotalSize_{0};
    size_t packetDataTotalSize_{0};
    size_t timestampTotalSize_{0};
};

} // namespace Isoch
} // namespace FWA