#include "Isoch/components/TransmitterComponents.hpp"
#include "Isoch/core/AmdtpTransmitter.hpp"
#include <mach/mach.h>

namespace FWA {
namespace Isoch {

// Static callback function for DCL completion
static void dclCompletionCallback(void* refcon, NuDCLRef dcl) {
    auto* transmitter = static_cast<AmdtpTransmitter*>(refcon);
    transmitter->handleSegmentComplete(transmitter->getCurrentSegment());
}

// --- TransmitBufferManager Implementation ---
TransmitBufferManager::TransmitBufferManager(const TransmitterConfig& config)
    : logger_(config.logger) {
    if (logger_) {
        logger_->debug("TransmitBufferManager created");
    }
}

TransmitBufferManager::~TransmitBufferManager() {
    cleanup();
}

void TransmitBufferManager::cleanup() noexcept {
    if (mainBuffer_) {
        vm_deallocate(mach_task_self(),
                     reinterpret_cast<vm_address_t>(mainBuffer_),
                     totalBufferSize_);
        mainBuffer_ = nullptr;
    }
}

std::expected<void, IOKitError> TransmitBufferManager::setupBuffers(
    uint32_t totalCycles,
    uint32_t cycleBufferSize) {
    

        
    totalCycles_ = totalCycles;
    cycleBufferSize_ = cycleBufferSize;

    size_t mainBufferSize = totalCycles * cycleBufferSize;
    size_t overrunBufferSize = cycleBufferSize;
    totalBufferSize_ = mainBufferSize + overrunBufferSize;

    vm_address_t buffer = 0;
    kern_return_t result = vm_allocate(mach_task_self(),
                                     &buffer,
                                     totalBufferSize_,
                                     VM_FLAGS_ANYWHERE);
    
    if (result != KERN_SUCCESS) {
        logger_->error("Failed to allocate buffer: size={}, error={}",
                      totalBufferSize_, result);
        return std::unexpected(IOKitError::NoMemory);
    }

    mainBuffer_ = reinterpret_cast<uint8_t*>(buffer);
    overrunBuffer_ = mainBuffer_ + mainBufferSize;

    bzero(mainBuffer_, totalBufferSize_);

    if (logger_) {
        logger_->debug("Buffers setup: total={}, main={}, overrun={}",
                      totalBufferSize_, mainBufferSize, overrunBufferSize);
    }

    return {};
}

std::expected<uint8_t*, IOKitError> TransmitBufferManager::getCycleBuffer(
    uint32_t segment,
    uint32_t cycle) {
    
    uint32_t index = (segment * cycleBufferSize_) + cycle;
    if (index >= totalCycles_) {
        return std::unexpected(IOKitError::BadArgument);
    }

    return mainBuffer_ + (index * cycleBufferSize_);
}

std::expected<uint8_t*, IOKitError> TransmitBufferManager::getOverrunBuffer() {
    return overrunBuffer_;
}

// --- TransmitPacketManager Implementation ---
TransmitPacketManager::TransmitPacketManager(
    const TransmitterConfig& config,
    std::weak_ptr<AmdtpTransmitter> transmitter)
    : logger_(config.logger)
    , transmitter_(transmitter)
    , cycleBufferSize_(config.cycleBufferSize) {
}

std::expected<void, IOKitError> TransmitPacketManager::processPacket(
    uint32_t segment,
    uint32_t cycle,
    uint8_t* data,
    size_t length) {
    
    if (!data || length > cycleBufferSize_) {
        return std::unexpected(IOKitError::BadArgument);
    }

    if (packetCallback_) {
        packetCallback_(data, length);
    }

    return {};
}

std::expected<void, IOKitError> TransmitPacketManager::handleOverrun() {
    if (auto transmitter = transmitter_.lock()) {
        transmitter->handleOverrun();
    }
    return {};
}

void TransmitPacketManager::setPacketCallback(
    std::function<void(uint8_t* data, size_t size)> callback) {
    packetCallback_ = std::move(callback);
}

// --- TransmitDCLManager Implementation ---
TransmitDCLManager::TransmitDCLManager(
    const TransmitterConfig& config,
    std::weak_ptr<AmdtpTransmitter> transmitter)
    : logger_(config.logger)
    , transmitter_(transmitter) {
}

TransmitDCLManager::~TransmitDCLManager() {
    if (nuDCLPool_) {
        (*nuDCLPool_)->Release(nuDCLPool_);
        nuDCLPool_ = nullptr;
    }
}

std::expected<void, IOKitError> TransmitDCLManager::createProgram(
    uint32_t cyclesPerSegment,
    uint32_t numSegments,
    uint32_t cycleBufferSize) {
    
    cyclesPerSegment_ = cyclesPerSegment;
    numSegments_ = numSegments;
    
    uint32_t totalDCLs = cyclesPerSegment * numSegments;
    dclProgram_.resize(totalDCLs);

    for (uint32_t segment = 0; segment < numSegments; ++segment) {
        if (auto result = createSegmentDCLs(segment); !result) {
            return result;
        }
    }

    if (auto result = updateJumpTargets(); !result) {
        return result;
    }

    return {};
}

std::expected<void, IOKitError> TransmitDCLManager::createSegmentDCLs(uint32_t segment) {
    for (uint32_t cycle = 0; cycle < cyclesPerSegment_; ++cycle) {
        uint32_t index = (segment * cyclesPerSegment_) + cycle;
        
        NuDCLRef dcl = (*nuDCLPool_)->AllocateSendPacket(nuDCLPool_,
            nullptr, 0, nullptr);
        
        if (!dcl) {
            logger_->error("Failed to allocate DCL for segment {} cycle {}", 
                segment, cycle);
            return std::unexpected(IOKitError::NoMemory);
        }

        dclProgram_[index] = dcl;
        
        // Set the callback for the last DCL in the segment
        if (cycle == cyclesPerSegment_ - 1) {
            if (auto transmitter = transmitter_.lock()) {
                (*nuDCLPool_)->SetDCLCallback(dcl, 
                    reinterpret_cast<NuDCLCallback>(dclCompletionCallback));
                (*nuDCLPool_)->SetDCLRefcon(dcl, transmitter.get());
            }
        }
    }
    return {};
}

std::expected<void, IOKitError> TransmitDCLManager::updateJumpTargets() {
    for (uint32_t segment = 0; segment < numSegments_; ++segment) {
        uint32_t currentLastIndex = (segment * cyclesPerSegment_) + (cyclesPerSegment_ - 1);
        uint32_t nextFirstIndex = ((segment + 1) % numSegments_) * cyclesPerSegment_;

        (*nuDCLPool_)->SetDCLBranch(dclProgram_[currentLastIndex],
                                   dclProgram_[nextFirstIndex]);
    }
    return {};
}

std::expected<void, IOKitError> TransmitDCLManager::handleSegmentComplete(
    uint32_t segment) {
    
    if (segment >= numSegments_) {
        return std::unexpected(IOKitError::BadArgument);
    }

    currentSegment_ = (segment + 1) % numSegments_;

    if (auto transmitter = transmitter_.lock()) {
        transmitter->handleSegmentComplete(segment);
    }

    return {};
}

DCLCommandPtr TransmitDCLManager::getProgram() const {
    if (dclProgram_.empty()) {
        return nullptr;
    }
    return reinterpret_cast<DCLCommandPtr>((*nuDCLPool_)->GetProgram(nuDCLPool_));
}

// --- TransmitterComponentFactory Implementation ---
std::shared_ptr<ITransmitBufferManager> 
TransmitterComponentFactory::createBufferManager(
    const TransmitterConfig& config) {
    return std::make_shared<TransmitBufferManager>(config);
}

std::shared_ptr<ITransmitPacketManager>
TransmitterComponentFactory::createPacketManager(
    const TransmitterConfig& config,
    std::weak_ptr<AmdtpTransmitter> transmitter) {
    return std::make_shared<TransmitPacketManager>(config, transmitter);
}

std::shared_ptr<ITransmitDCLManager>
TransmitterComponentFactory::createDCLManager(
    const TransmitterConfig& config,
    std::weak_ptr<AmdtpTransmitter> transmitter) {
    return std::make_shared<TransmitDCLManager>(config, transmitter);
}

} // namespace Isoch
} // namespace FWA