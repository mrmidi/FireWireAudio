#include "Isoch/core/IsochBufferManager.hpp"
#include <mach/mach.h>
#include <spdlog/spdlog.h>
#include <cstring> // For bzero

namespace FWA {
namespace Isoch {

IsochBufferManager::IsochBufferManager(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {
    if (logger_) {
        logger_->debug("IsochBufferManager created");
    }
}

IsochBufferManager::~IsochBufferManager() {
    cleanup();
    
    if (logger_) {
        logger_->debug("IsochBufferManager destroyed");
    }
}

void IsochBufferManager::cleanup() noexcept {
    if (mainBuffer_) {
        // Free the memory using vm_deallocate
        vm_deallocate(mach_task_self(),
                     reinterpret_cast<vm_address_t>(mainBuffer_),
                     totalBufferSize_);
        
        // Reset pointers
        mainBuffer_ = nullptr;
        isochHeaderArea_ = nullptr;
        cipHeaderArea_ = nullptr;
        packetDataArea_ = nullptr;
        timestampArea_ = nullptr;
        
        // Reset buffer range
        bufferRange_ = IOVirtualRange{};
        totalBufferSize_ = 0;
        
        if (logger_) {
            logger_->debug("IsochBufferManager::cleanup: Released buffer");
        }
    }
}

void IsochBufferManager::calculateBufferLayout() {
    totalPackets_ = config_.numGroups * config_.packetsPerGroup;

    // Calculate size needed for each section based on total packets
    isochHeaderTotalSize_ = totalPackets_ * kIsochHeaderSize;
    cipHeaderTotalSize_ = totalPackets_ * kCIPHeaderSize;
    packetDataTotalSize_ = totalPackets_ * config_.packetDataSize;
    timestampTotalSize_ = totalPackets_ * sizeof(uint32_t);

    // Total size is the sum of all sections
    totalBufferSize_ = isochHeaderTotalSize_ + cipHeaderTotalSize_ + packetDataTotalSize_ + timestampTotalSize_;

    // Align total size to page boundary for vm_allocate efficiency
    totalBufferSize_ = (totalBufferSize_ + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    if (logger_) {
        logger_->debug("IsochBufferManager::calculateBufferLayout:");
        logger_->debug("  NumGroups: {}, PacketsPerGroup: {}, TotalPackets: {}", 
                      config_.numGroups, config_.packetsPerGroup, totalPackets_);
        logger_->debug("  PacketDataSize: {}", config_.packetDataSize);
        logger_->debug("  IsochHdr Area Size: {}", isochHeaderTotalSize_);
        logger_->debug("  CIP Hdr Area Size: {}", cipHeaderTotalSize_);
        logger_->debug("  Data Area Size: {}", packetDataTotalSize_);
        logger_->debug("  Timestamp Area Size: {}", timestampTotalSize_);
        logger_->debug("  Total Buffer Size (Aligned): {}", totalBufferSize_);
    }
}

std::expected<void, IOKitError> IsochBufferManager::setupBuffers(const Config& config) {
    cleanup();
    config_ = config;
    
    if (config_.numGroups == 0 || config_.packetsPerGroup == 0 || config_.packetDataSize == 0) {
        if (logger_) logger_->error("IsochBufferManager: Invalid configuration parameters (zeros)");
        return std::unexpected(IOKitError::BadArgument);
    }

    calculateBufferLayout();

    // Allocate memory using vm_allocate for page alignment
    vm_address_t buffer = 0;
    kern_return_t result = vm_allocate(mach_task_self(), &buffer, totalBufferSize_, VM_FLAGS_ANYWHERE);
    if (result != KERN_SUCCESS) {
        if (logger_) {
            logger_->error("IsochBufferManager: Failed to allocate memory: size={}, error={}", 
                          totalBufferSize_, result);
        }
        return std::unexpected(IOKitError::NoMemory);
    }

    mainBuffer_ = reinterpret_cast<uint8_t*>(buffer);
    bzero(mainBuffer_, totalBufferSize_);

    // Set section pointers within the contiguous buffer
    isochHeaderArea_ = mainBuffer_;
    cipHeaderArea_ = isochHeaderArea_ + isochHeaderTotalSize_;
    packetDataArea_ = cipHeaderArea_ + cipHeaderTotalSize_;
    timestampArea_ = reinterpret_cast<uint32_t*>(packetDataArea_ + packetDataTotalSize_);

    bufferRange_.address = reinterpret_cast<IOVirtualAddress>(mainBuffer_);
    bufferRange_.length = totalBufferSize_; // The range covers the entire allocation

    if (logger_) {
        logger_->info("IsochBufferManager::setupBuffers: Allocated buffer at {:p} size {}", 
                     (void*)mainBuffer_, totalBufferSize_);
        logger_->debug("  IsochHdr Area: {:p}", (void*)isochHeaderArea_);
        logger_->debug("  CIP Hdr Area: {:p}", (void*)cipHeaderArea_);
        logger_->debug("  Data Area: {:p}", (void*)packetDataArea_);
        logger_->debug("  Timestamp Area: {:p}", (void*)timestampArea_);
    }
    return {};
}

// --- Implementation of getters for packet components ---

std::expected<uint32_t*, IOKitError> IsochBufferManager::getPacketTimestampPtr(
    uint32_t groupIndex, uint32_t packetIndexInGroup) const
{
    if (!mainBuffer_) {
        return std::unexpected(IOKitError::NotReady);
    }
    
    if (groupIndex >= config_.numGroups || packetIndexInGroup >= config_.packetsPerGroup) {
        if (logger_) logger_->error("getPacketTimestampPtr: Invalid indices G:{} P:{}, limits G:{} P:{}",
                                  groupIndex, packetIndexInGroup, config_.numGroups, config_.packetsPerGroup);
        return std::unexpected(IOKitError::BadArgument);
    }
    
    uint32_t globalPacketIndex = groupIndex * config_.packetsPerGroup + packetIndexInGroup;
    uint32_t* ptr = &timestampArea_[globalPacketIndex];
    
    if (logger_ && logger_->should_log(spdlog::level::trace)) {
        logger_->trace("getPacketTimestampPtr(G:{}, P:{}): Base={:p}, Idx={}, Result={:p}",
                      groupIndex, packetIndexInGroup, 
                      (void*)timestampArea_, globalPacketIndex, (void*)ptr);
    }
    
    return ptr;
}

std::expected<uint8_t*, IOKitError> IsochBufferManager::getPacketIsochHeaderPtr(
    uint32_t groupIndex, uint32_t packetIndexInGroup) const
{
    if (!mainBuffer_) {
        return std::unexpected(IOKitError::NotReady);
    }
    
    if (groupIndex >= config_.numGroups || packetIndexInGroup >= config_.packetsPerGroup) {
        if (logger_) logger_->error("getPacketIsochHeaderPtr: Invalid indices G:{} P:{}, limits G:{} P:{}",
                                  groupIndex, packetIndexInGroup, config_.numGroups, config_.packetsPerGroup);
        return std::unexpected(IOKitError::BadArgument);
    }
    
    uint32_t globalPacketIndex = groupIndex * config_.packetsPerGroup + packetIndexInGroup;
    size_t offset = globalPacketIndex * kIsochHeaderSize;
    uint8_t* ptr = isochHeaderArea_ + offset;
    
    if (logger_ && logger_->should_log(spdlog::level::trace)) {
        logger_->trace("getPacketIsochHeaderPtr(G:{}, P:{}): Base={:p}, Idx={}, Offset={}, Result={:p}",
                      groupIndex, packetIndexInGroup, (void*)isochHeaderArea_, 
                      globalPacketIndex, offset, (void*)ptr);
    }
    
    return ptr;
}

std::expected<uint8_t*, IOKitError> IsochBufferManager::getPacketCIPHeaderPtr(
    uint32_t groupIndex, uint32_t packetIndexInGroup) const
{
    if (!mainBuffer_) {
        return std::unexpected(IOKitError::NotReady);
    }
    
    if (groupIndex >= config_.numGroups || packetIndexInGroup >= config_.packetsPerGroup) {
        if (logger_) logger_->error("getPacketCIPHeaderPtr: Invalid indices G:{} P:{}, limits G:{} P:{}",
                                  groupIndex, packetIndexInGroup, config_.numGroups, config_.packetsPerGroup);
        return std::unexpected(IOKitError::BadArgument);
    }
    
    uint32_t globalPacketIndex = groupIndex * config_.packetsPerGroup + packetIndexInGroup;
    size_t offset = globalPacketIndex * kCIPHeaderSize;
    uint8_t* ptr = cipHeaderArea_ + offset;
    
    if (logger_ && logger_->should_log(spdlog::level::trace)) {
        logger_->trace("getPacketCIPHeaderPtr(G:{}, P:{}): Base={:p}, Idx={}, Offset={}, Result={:p}",
                      groupIndex, packetIndexInGroup, (void*)cipHeaderArea_, 
                      globalPacketIndex, offset, (void*)ptr);
    }
    
    return ptr;
}

std::expected<uint8_t*, IOKitError> IsochBufferManager::getPacketDataPtr(
    uint32_t groupIndex, uint32_t packetIndexInGroup) const
{
    if (!mainBuffer_) {
        return std::unexpected(IOKitError::NotReady);
    }
    
    if (groupIndex >= config_.numGroups || packetIndexInGroup >= config_.packetsPerGroup) {
        if (logger_) logger_->error("getPacketDataPtr: Invalid indices G:{} P:{}, limits G:{} P:{}",
                                  groupIndex, packetIndexInGroup, config_.numGroups, config_.packetsPerGroup);
        return std::unexpected(IOKitError::BadArgument);
    }
    
    uint32_t globalPacketIndex = groupIndex * config_.packetsPerGroup + packetIndexInGroup;
    size_t offset = globalPacketIndex * config_.packetDataSize;
    uint8_t* ptr = packetDataArea_ + offset;
    
    if (logger_ && logger_->should_log(spdlog::level::trace)) {
        logger_->trace("getPacketDataPtr(G:{}, P:{}): Base={:p}, Idx={}, Offset={}, Size={}, Result={:p}",
                      groupIndex, packetIndexInGroup, (void*)packetDataArea_, 
                      globalPacketIndex, offset, config_.packetDataSize, (void*)ptr);
    }
    
    return ptr;
}

std::expected<uint8_t*, IOKitError> IsochBufferManager::getRawPacketSlotPtr(
    uint32_t groupIndex, uint32_t packetIndexInGroup) const
{
     if (!mainBuffer_) {
        return std::unexpected(IOKitError::NotReady);
    }
    if (groupIndex >= config_.numGroups || packetIndexInGroup >= config_.packetsPerGroup) {
        if (logger_) logger_->error("getRawPacketSlotPtr: Invalid indices G:{} P:{}, limits G:{} P:{}",
                                  groupIndex, packetIndexInGroup, config_.numGroups, config_.packetsPerGroup);
        return std::unexpected(IOKitError::BadArgument);
    }
    
    // This calculates the offset based on the assumption that Isoch headers
    // are stored contiguously at the beginning.
    uint32_t globalPacketIndex = groupIndex * config_.packetsPerGroup + packetIndexInGroup;
    size_t offset = globalPacketIndex * kIsochHeaderSize;
    uint8_t* ptr = isochHeaderArea_ + offset;
    
    if (logger_ && logger_->should_log(spdlog::level::trace)) {
        logger_->trace("getRawPacketSlotPtr(G:{}, P:{}): Base={:p}, Idx={}, Offset={}, Result={:p}",
                      groupIndex, packetIndexInGroup, (void*)isochHeaderArea_, 
                      globalPacketIndex, offset, (void*)ptr);
    }
    
    return ptr;
}

} // namespace Isoch
} // namespace FWA