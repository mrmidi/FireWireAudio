#pragma once

#include "Isoch/interfaces/ITransmitBufferManager.hpp"
#include <vector>
#include <mutex>

namespace FWA {
namespace Isoch {

class IsochTransmitBufferManager : public ITransmitBufferManager {
public:
    explicit IsochTransmitBufferManager(std::shared_ptr<spdlog::logger> logger);
    ~IsochTransmitBufferManager() override;

    // Prevent Copy
    IsochTransmitBufferManager(const IsochTransmitBufferManager&) = delete;
    IsochTransmitBufferManager& operator=(const IsochTransmitBufferManager&) = delete;

    std::expected<void, IOKitError> setupBuffers(const TransmitterConfig& config) override;
    void cleanup() noexcept override;

    std::expected<uint8_t*, IOKitError> getPacketIsochHeaderPtr(uint32_t groupIndex, uint32_t packetIndexInGroup) const override;
    std::expected<uint8_t*, IOKitError> getPacketCIPHeaderPtr(uint32_t groupIndex, uint32_t packetIndexInGroup) const override;
    std::expected<uint32_t*, IOKitError> getGroupTimestampPtr(uint32_t groupIndex) const override;

    uint8_t* getClientAudioBufferPtr() const override;
    size_t getClientAudioBufferSize() const override;
    size_t getAudioPayloadSizePerPacket() const override;

    const IOVirtualRange& getBufferRange() const override;
    size_t getTotalBufferSize() const override;

private:
    void calculateBufferLayout();

    std::shared_ptr<spdlog::logger> logger_;
    TransmitterConfig config_; // Store local copy
    uint32_t totalPackets_{0};
    size_t audioPayloadSizePerPacket_{0}; // Calculated based on channels/format

    // Buffer management
    uint8_t* mainBuffer_{nullptr};
    size_t totalBufferSize_{0};
    IOVirtualRange bufferRange_{};

    // Pointers into mainBuffer_
    uint8_t* clientAudioArea_{nullptr};  // HW writes here
    uint8_t* isochHeaderArea_{nullptr}; // Template area
    uint8_t* cipHeaderArea_{nullptr};   // Pre-filled area
    uint32_t* timestampArea_{nullptr};  // Timestamp area

    // Buffer section sizes (aligned)
    size_t clientBufferSize_aligned_{0};
    size_t isochHeaderTotalSize_aligned_{0};
    size_t cipHeaderTotalSize_aligned_{0};
    size_t timestampTotalSize_aligned_{0};

    mutable std::mutex mutex_; // For thread safety
};

} // namespace Isoch
} // namespace FWA