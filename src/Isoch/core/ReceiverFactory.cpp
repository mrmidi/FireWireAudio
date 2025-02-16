#include "Isoch/core/ReceiverFactory.hpp"
#include <spdlog/spdlog.h>

namespace FWA {
namespace Isoch {

// New method that accepts a complete ReceiverConfig directly
std::shared_ptr<AmdtpReceiver> ReceiverFactory::createStandardReceiver(const ReceiverConfig& config) {
    // Create and return receiver directly with provided config
    return AmdtpReceiver::create(config);
}

std::shared_ptr<AmdtpReceiver> ReceiverFactory::createStandardReceiver(
    std::shared_ptr<spdlog::logger> logger,
    uint32_t cyclesPerSegment,
    uint32_t numSegments,
    uint32_t cycleBufferSize) {
    
    // Create configuration with standard settings, using new packet-based parameters
    ReceiverConfig config;
    config.logger = logger ? logger : spdlog::default_logger();
    
    // Convert old parameters to new packet-based configuration
    // This maintains backward compatibility with existing code
    config.numGroups = numSegments;
    config.packetsPerGroup = cyclesPerSegment;
    config.packetDataSize = cycleBufferSize;
    config.callbackGroupInterval = 1; // Default to callback every group
    
    config.timeout = 1000; // 1 second timeout
    config.doIRMAllocations = true;
    config.irmPacketSize = 72; // Default for audio (64 bytes + 8 bytes CIP header)
    
    // Create and return receiver
    return AmdtpReceiver::create(config);
}

std::shared_ptr<AmdtpReceiver> ReceiverFactory::createHighPerformanceReceiver(
    std::shared_ptr<spdlog::logger> logger) {
    
    // Create configuration with high-performance settings
    ReceiverConfig config;
    config.logger = logger ? logger : spdlog::default_logger();
    
    // High-performance parameters
    config.numGroups = 8;           // More groups
    config.packetsPerGroup = 16;    // More packets per group
    config.packetDataSize = 1024;   // Larger packets
    config.callbackGroupInterval = 2; // Callback every other group for efficiency
    
    config.timeout = 2000; // 2 second timeout (more tolerant)
    config.doIRMAllocations = true;
    config.irmPacketSize = 144; // Double size for high-bandwidth (128 bytes + 16 bytes overhead)
    
    // Create and return receiver
    return AmdtpReceiver::create(config);
}

std::shared_ptr<AmdtpReceiver> ReceiverFactory::createLowLatencyReceiver(
    std::shared_ptr<spdlog::logger> logger) {
    
    // Create configuration with low-latency settings
    ReceiverConfig config;
    config.logger = logger ? logger : spdlog::default_logger();
    
    // Low-latency parameters
    config.numGroups = 2;          // Fewer groups
    config.packetsPerGroup = 4;    // Fewer packets per group
    config.packetDataSize = 512;   // Standard size
    config.callbackGroupInterval = 1; // Callback every group for low latency
    
    config.timeout = 500;  // 500ms timeout (more sensitive)
    config.doIRMAllocations = true;
    config.irmPacketSize = 72; // Standard size
    
    // Create and return receiver
    return AmdtpReceiver::create(config);
}

} // namespace Isoch
} // namespace FWA