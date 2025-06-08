#include "Isoch/core/IsochTransmitBufferManager.hpp"
#include "Isoch/core/TransmitterTypes.hpp" // Include for kTransmitCIPHeaderSize and kTransmitIsochHeaderSize constants
#include <mach/mach.h>
#include <cstring> // For bzero

namespace FWA {
namespace Isoch {

// Constants from TransmitterTypes.hpp are now used
constexpr size_t kTimestampSize = 4;

IsochTransmitBufferManager::IsochTransmitBufferManager(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {
    if (logger_) logger_->debug("IsochTransmitBufferManager created");
}

IsochTransmitBufferManager::~IsochTransmitBufferManager() {
    // Acquire lock here before calling cleanup
    std::lock_guard<std::mutex> lock(mutex_);
    cleanup();
    if (logger_) logger_->debug("IsochTransmitBufferManager destroyed");
}

void IsochTransmitBufferManager::cleanup() noexcept {
    // NOTE: This function now assumes the caller holds the mutex_ lock.
    if (mainBuffer_) {
        vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(mainBuffer_), totalBufferSize_);
        mainBuffer_ = nullptr;
        clientAudioArea_ = nullptr;
        isochHeaderArea_ = nullptr;
        cipHeaderArea_ = nullptr;
        timestampArea_ = nullptr;
        totalBufferSize_ = 0;
        bufferRange_ = {};
        if (logger_) logger_->debug("IsochTransmitBufferManager::cleanup: Released buffer");
    }
}

void IsochTransmitBufferManager::calculateBufferLayout() {
    totalPackets_ = config_.numGroups * config_.packetsPerGroup;

    // FIXED for Blocking 44.1 kHz: always 8 frames per packet
    constexpr size_t framesPerPacket = 8;
    const size_t bytesPerFrameStereoAM824 = 8; // 2ch × 4 bytes
    audioPayloadSizePerPacket_ = framesPerPacket * bytesPerFrameStereoAM824; // 64 bytes

    if (logger_) {
         logger_->debug("Buffer layout calculated for SampleRate={:.1f}Hz", config_.sampleRate);
         logger_->debug("  Fixed FramesPerPacket={}, BytesPerFrame={}, Resulting PayloadSize={}",
                        framesPerPacket, bytesPerFrameStereoAM824, audioPayloadSizePerPacket_);
    }

    // --- Sizes calculation (NO CHANGE needed here, uses config/constants) ---
    size_t clientDataSize = config_.clientBufferSize;
    size_t cipHeadersSize = totalPackets_ * kTransmitCIPHeaderSize;
    size_t isochHeadersSize = totalPackets_ * kTransmitIsochHeaderSize; // Template only
    size_t timestampsSize = config_.numGroups * kTimestampSize; // Only need one per group/segment completion
    // --- End Sizes calculation ---

    // Align each section
    clientBufferSize_aligned_ = (clientDataSize + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    cipHeaderTotalSize_aligned_ = (cipHeadersSize + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    isochHeaderTotalSize_aligned_ = (isochHeadersSize + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    timestampTotalSize_aligned_ = (timestampsSize + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    totalBufferSize_ = clientBufferSize_aligned_ + cipHeaderTotalSize_aligned_ + isochHeaderTotalSize_aligned_ + timestampTotalSize_aligned_;

    if (logger_) {
        logger_->debug("Buffer layout calculated:");
        logger_->debug("  Total packets: {}", totalPackets_);
        logger_->debug("  Audio payload per packet: {} bytes", audioPayloadSizePerPacket_);
        logger_->debug("  Client buffer: {} bytes (aligned: {})", clientDataSize, clientBufferSize_aligned_);
        logger_->debug("  CIP headers: {} bytes (aligned: {})", cipHeadersSize, cipHeaderTotalSize_aligned_);
        logger_->debug("  Isoch headers: {} bytes (aligned: {})", isochHeadersSize, isochHeaderTotalSize_aligned_);
        logger_->debug("  Timestamps: {} bytes (aligned: {})", timestampsSize, timestampTotalSize_aligned_);
        logger_->debug("  Total buffer size: {} bytes", totalBufferSize_);
    }
}

std::expected<void, IOKitError> IsochTransmitBufferManager::setupBuffers(const TransmitterConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    cleanup(); // Clean up previous if any
    config_ = config;

    if (config_.numGroups == 0 || config_.packetsPerGroup == 0 || config_.clientBufferSize == 0) {
        if (logger_) logger_->error("IsochTransmitBufferManager: Invalid config (zeros)");
        return std::unexpected(IOKitError::BadArgument);
    }

    calculateBufferLayout();

    vm_address_t buffer = 0;
    kern_return_t result = vm_allocate(mach_task_self(), &buffer, totalBufferSize_, VM_FLAGS_ANYWHERE);
    if (result != KERN_SUCCESS) {
        if (logger_) logger_->error("IsochTransmitBufferManager: vm_allocate failed: {}", result);
        return std::unexpected(IOKitError::NoMemory);
    }

    mainBuffer_ = reinterpret_cast<uint8_t*>(buffer);
    bzero(mainBuffer_, totalBufferSize_);

    // Assign pointers based on layout
    // clientAudioArea_ = mainBuffer_;
    // cipHeaderArea_ = clientAudioArea_ + clientBufferSize_aligned_;
    // isochHeaderArea_ = cipHeaderArea_ + cipHeaderTotalSize_aligned_;
    // timestampArea_ = reinterpret_cast<uint32_t*>(isochHeaderArea_ + isochHeaderTotalSize_aligned_);

    // SANITY CHECKS
    clientAudioArea_ = mainBuffer_;
    cipHeaderArea_   = clientAudioArea_ + clientBufferSize_aligned_;
    assert(reinterpret_cast<uintptr_t>(cipHeaderArea_) % 16 == 0); // Ensure CIP area is 16-byte aligned

    isochHeaderArea_ = cipHeaderArea_ + cipHeaderTotalSize_aligned_;
    assert(reinterpret_cast<uintptr_t>(isochHeaderArea_) % 16 == 0); // Ensure Isoch header area is 16-byte aligned

    timestampArea_   = reinterpret_cast<uint32_t*>(isochHeaderArea_ + isochHeaderTotalSize_aligned_);
    assert(reinterpret_cast<uintptr_t>(timestampArea_) % sizeof(uint32_t) == 0); // Ensure Timestamp area is 4-byte aligned

    bufferRange_.address = reinterpret_cast<IOVirtualAddress>(mainBuffer_);
    bufferRange_.length = totalBufferSize_;

    if (logger_) {
        logger_->info("IsochTransmitBufferManager::setupBuffers: Allocated buffer at {:p} size {}", (void*)mainBuffer_, totalBufferSize_);
        logger_->debug("  Client audio area: {:p}", (void*)clientAudioArea_);
        logger_->debug("  CIP header area: {:p}", (void*)cipHeaderArea_);
        logger_->debug("  Isoch header area: {:p}", (void*)isochHeaderArea_);
        logger_->debug("  Timestamp area: {:p}", (void*)timestampArea_);
    }

    return {};
}

// Implement Getters (with basic checks)
std::expected<uint8_t*, IOKitError> IsochTransmitBufferManager::getPacketIsochHeaderPtr(uint32_t g, uint32_t p) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!mainBuffer_ || g >= config_.numGroups || p >= config_.packetsPerGroup) {
        return std::unexpected(IOKitError::BadArgument);
    }
    size_t offset = (g * config_.packetsPerGroup + p) * kTransmitIsochHeaderSize;
    return isochHeaderArea_ + offset;
}

std::expected<uint8_t*, IOKitError> IsochTransmitBufferManager::getPacketCIPHeaderPtr(uint32_t g, uint32_t p) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!mainBuffer_ || g >= config_.numGroups || p >= config_.packetsPerGroup) {
        return std::unexpected(IOKitError::BadArgument);
    }
    size_t offset = (g * config_.packetsPerGroup + p) * kTransmitCIPHeaderSize;
    return cipHeaderArea_ + offset;
}

std::expected<uint32_t*, IOKitError> IsochTransmitBufferManager::getGroupTimestampPtr(uint32_t g) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!mainBuffer_ || g >= config_.numGroups) {
        return std::unexpected(IOKitError::BadArgument);
    }
    return timestampArea_ + g;
}

uint8_t* IsochTransmitBufferManager::getClientAudioBufferPtr() const { 
    return clientAudioArea_; 
}

size_t IsochTransmitBufferManager::getClientAudioBufferSize() const { 
    return config_.clientBufferSize;  // Return requested size
}

size_t IsochTransmitBufferManager::getAudioPayloadSizePerPacket() const { 
    return audioPayloadSizePerPacket_; 
}

const IOVirtualRange& IsochTransmitBufferManager::getBufferRange() const { 
    return bufferRange_; 
}

size_t IsochTransmitBufferManager::getTotalBufferSize() const { 
    return totalBufferSize_; 
}

} // namespace Isoch
} // namespace FWA