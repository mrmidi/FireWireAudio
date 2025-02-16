#pragma once

#include <memory>
#include <expected>
#include <spdlog/logger.h>
#include "FWA/Error.h"
#include "Isoch/core/AmdtpReceiver.hpp"
#include "Isoch/utils/RingBuffer.hpp"

namespace FWA {
namespace Isoch {

/**
 * @brief Factory for creating AMDTP receiver instances
 * 
 * This class provides factory methods for creating configured
 * AMDTP receiver instances with sensible defaults.
 */
class ReceiverFactory {
public:
    /**
     * @brief Create a standard AMDTP receiver with direct configuration
     * 
     * @param config Complete receiver configuration
     * @return std::shared_ptr<AmdtpReceiver> New receiver instance
     */
    static std::shared_ptr<AmdtpReceiver> createStandardReceiver(
        const ReceiverConfig& config);

    /**
     * @brief Create a standard AMDTP receiver (legacy interface)
     * 
     * @param logger Logger for diagnostics
     * @param cyclesPerSegment Number of cycles per segment (default: 8)
     * @param numSegments Number of segments (default: 4)
     * @param cycleBufferSize Size of each cycle buffer in bytes (default: 512)
     * @return std::shared_ptr<AmdtpReceiver> New receiver instance
     */
    static std::shared_ptr<AmdtpReceiver> createStandardReceiver(
        std::shared_ptr<spdlog::logger> logger,
        uint32_t cyclesPerSegment = 8,
        uint32_t numSegments = 4,
        uint32_t cycleBufferSize = 512);
    
    /**
     * @brief Create a high-performance AMDTP receiver
     * 
     * This creates a receiver with more buffer groups and packets
     * for high-bandwidth applications.
     * 
     * @param logger Logger for diagnostics
     * @return std::shared_ptr<AmdtpReceiver> New receiver instance
     */
    static std::shared_ptr<AmdtpReceiver> createHighPerformanceReceiver(
        std::shared_ptr<spdlog::logger> logger);
    
    /**
     * @brief Create a low-latency AMDTP receiver
     * 
     * This creates a receiver with fewer packets per group
     * for lower latency applications.
     * 
     * @param logger Logger for diagnostics
     * @return std::shared_ptr<AmdtpReceiver> New receiver instance
     */
    static std::shared_ptr<AmdtpReceiver> createLowLatencyReceiver(
        std::shared_ptr<spdlog::logger> logger);
};

} // namespace Isoch
} // namespace FWA