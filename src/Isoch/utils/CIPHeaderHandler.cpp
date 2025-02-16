#include "Isoch/utils/CIPHeaderHandler.hpp"

namespace FWA {
namespace Isoch {

CIPHeaderHandler::CIPHeaderHandler(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {
    if (logger_) {
        logger_->info("Created CIPHeaderHandler");
    }
}

std::expected<void, IOKitError> CIPHeaderHandler::initialize(uint32_t currentFireWireCycleTime) {
    initializeTransferDelay(currentFireWireCycleTime);
    
    // Reset state
    wasNoData_ = true;
    dbcCount_ = 0;
    firstCallbackOccurred_ = false;
    
    if (logger_) {
        logger_->info("Initialized with FireWire cycle time: {}", currentFireWireCycleTime);
    }
    
    return {};  // Return success
}

void CIPHeaderHandler::initializeTransferDelay(uint32_t currentFireWireCycleTime) noexcept {
    // Extract cycle count and seconds from FireWire cycle time
    uint32_t currentCycleCount = (currentFireWireCycleTime & 0x01FFF000) >> 12;
    uint32_t currentSeconds = (currentFireWireCycleTime & 0x0E000000) >> 25;
    
    // Calculate absolute cycle number within 8-second window
    uint32_t absoluteCycle = (currentSeconds * CYCLES_PER_SECOND) + currentCycleCount;
    
    // Set base SYT offset using current FireWire cycle time
    sytOffset_ = (absoluteCycle * TICKS_PER_CYCLE) % TICKS_PER_SECOND;
    
    if (logger_) {
        logger_->debug("Transfer delay initialized: absCycle={}, sytOffset={}", 
                    absoluteCycle, sytOffset_);
    }
}

void CIPHeaderHandler::handle44100Mode() noexcept {
    uint32_t phase = sytPhase_ % SYT_PHASE_MOD;
    bool addExtra = (phase && !(phase & 3)) || (sytPhase_ == 146);
    sytOffset_ += BASE_TICKS_44K;
    if (addExtra) sytOffset_ += 1;
    if (++sytPhase_ >= SYT_PHASE_RESET) sytPhase_ = 0;
}

void CIPHeaderHandler::handle48000Mode() noexcept {
    sytOffset_ += BASE_TICKS_48K;
}

void CIPHeaderHandler::updateSYTOffset() noexcept {
    if (sytOffset_ >= TICKS_PER_CYCLE) {
        sytOffset_ -= TICKS_PER_CYCLE;
    } else if (sampleRate_ == 44100) {
        handle44100Mode();
    } else {
        handle48000Mode();
    }
}

std::expected<CIPUpdateParams, IOKitError> CIPHeaderHandler::calculatePacketParams(
    uint32_t segment, uint32_t cycle) {
    
    CIPUpdateParams params;
    params.wasNoData = wasNoData_;
    params.dbc = dbcCount_;
    
    // Default to no-data if we haven't received first callback
    if (!firstCallbackOccurred_) {
        params.isNoData = true;
        params.syt = 0xFFFF;
        return params;
    }
    
    updateSYTOffset();
    
    // Check for overflow
    if (sytOffset_ >= TICKS_PER_CYCLE) {
        params.isNoData = true;
        params.syt = 0xFFFF;
    } else {
        params.isNoData = false;
        params.syt = sytOffset_;
    }
    
    // Update state for next iteration
    wasNoData_ = params.isNoData;
    if (!wasNoData_) {
        dbcCount_ = (dbcCount_ + 8) & 0xFF;
    }
    
    if (logger_) {
        logger_->debug("seg={} cycle={} sytOffset={} isNoData={}", 
                    segment, cycle, sytOffset_, params.isNoData);
    }
    
    return params;
}

void CIPHeaderHandler::updateCIPHeader(
    CIPHeader* header, uint16_t nodeID, const CIPUpdateParams& params) noexcept {
    
    header->sid = nodeID & 0x3F;
    header->dbs = 2;  // 2 channels
    header->fmt = IEC61883_FMT_AMDTP;
    header->sph = 0;
    header->fn = 0;
    header->qpc = 0;
    
    if (params.isNoData) {
        header->fdf = IEC61883_FDF_NODATA;
        header->syt = 0xFFFF;
    } else {
        header->fdf = (sampleRate_ == 44100) ? 
            IEC61883_FDF_SFC_44K1HZ : IEC61883_FDF_SFC_48KHZ;
        header->syt = params.syt & 0xFFF;
    }
    
    // Update DBC based on packet type
    header->dbc = params.wasNoData && params.isNoData ? 
        params.dbc :  // Keep same DBC for consecutive no-data packets
        params.dbc;   // Normal DBC update
}

void CIPHeaderHandler::setSampleRate(uint32_t newRate) noexcept {
    sampleRate_ = newRate;
    baseTicks_ = (newRate == 44100) ? BASE_TICKS_44K : BASE_TICKS_48K;
    
    if (logger_) {
        logger_->info("Sample rate set to {} Hz", newRate);
    }
}

} // namespace Isoch
} // namespace FWA