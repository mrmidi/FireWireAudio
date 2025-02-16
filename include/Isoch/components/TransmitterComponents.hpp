#pragma once

#include <memory>
#include <functional>
#include "Isoch/interfaces/TransmitterInterfaces.hpp"
#include <spdlog/logger.h>

namespace FWA {
namespace Isoch {

class TransmitBufferManager final : public ITransmitBufferManager {
public:
    explicit TransmitBufferManager(const TransmitterConfig& config);
    ~TransmitBufferManager() override;

    std::expected<void, IOKitError> setupBuffers(
        uint32_t totalCycles,
        uint32_t cycleBufferSize) override;

    std::expected<uint8_t*, IOKitError> getCycleBuffer(
        uint32_t segment,
        uint32_t cycle) override;

    std::expected<uint8_t*, IOKitError> getOverrunBuffer() override;

    size_t getTotalBufferSize() const noexcept override { 
        return totalBufferSize_; 
    }

private:
    std::shared_ptr<spdlog::logger> logger_;
    uint8_t* mainBuffer_{nullptr};
    uint8_t* overrunBuffer_{nullptr};
    size_t totalBufferSize_{0};
    uint32_t totalCycles_{0};
    uint32_t cycleBufferSize_{0};

    void cleanup() noexcept;
};

class TransmitPacketManager final : public ITransmitPacketManager {
public:
    explicit TransmitPacketManager(
        const TransmitterConfig& config,
        std::weak_ptr<AmdtpTransmitter> transmitter);

    std::expected<void, IOKitError> processPacket(
        uint32_t segment,
        uint32_t cycle,
        uint8_t* data,
        size_t length) override;

    std::expected<void, IOKitError> handleOverrun() override;
    void setPacketCallback(std::function<void(uint8_t* data, size_t size)> callback) override;

private:
    std::shared_ptr<spdlog::logger> logger_;
    std::weak_ptr<AmdtpTransmitter> transmitter_;
    std::function<void(uint8_t* data, size_t size)> packetCallback_;
    uint32_t cycleBufferSize_;
};

class TransmitDCLManager final : public ITransmitDCLManager {
public:
    explicit TransmitDCLManager(
        const TransmitterConfig& config,
        std::weak_ptr<AmdtpTransmitter> transmitter);
    ~TransmitDCLManager() override;

    // ITransmitDCLManager interface implementation
    std::expected<void, IOKitError> createProgram(
        uint32_t cyclesPerSegment,
        uint32_t numSegments,
        uint32_t cycleBufferSize) override;

    std::expected<void, IOKitError> handleSegmentComplete(uint32_t segment) override;
    DCLCommandPtr getProgram() const override;

private:
    std::expected<void, IOKitError> createSegmentDCLs(uint32_t segment);
    std::expected<void, IOKitError> updateJumpTargets();

    std::shared_ptr<spdlog::logger> logger_;
    std::weak_ptr<AmdtpTransmitter> transmitter_;
    IOFireWireLibNuDCLPoolRef nuDCLPool_{nullptr};
    std::vector<NuDCLRef> dclProgram_;
    uint32_t cyclesPerSegment_{0};
    uint32_t numSegments_{0};
    uint32_t currentSegment_{0};
};

// Concrete factory implementation
class TransmitterComponentFactory final : public ITransmitterComponentFactory {
public:
    std::shared_ptr<ITransmitBufferManager> createBufferManager(
        const TransmitterConfig& config) override;
        
    std::shared_ptr<ITransmitPacketManager> createPacketManager(
        const TransmitterConfig& config,
        std::weak_ptr<AmdtpTransmitter> transmitter) override;
        
    std::shared_ptr<ITransmitDCLManager> createDCLManager(
        const TransmitterConfig& config,
        std::weak_ptr<AmdtpTransmitter> transmitter) override;
};

} // namespace Isoch
} // namespace FWA