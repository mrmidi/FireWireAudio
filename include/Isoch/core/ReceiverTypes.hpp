#pragma once

#include <cstdint>
#include <memory>
#include <vector> // Added for ProcessedSample vector
#include <spdlog/logger.h>

namespace FWA {
namespace Isoch {

// Forward declarations
class AmdtpReceiver;

/**
 * @brief Enumeration of receiver message types
 */
enum class ReceiverMessage : uint32_t {
    BufferError = 0x1000,     ///< Error accessing buffer
    PacketError,              ///< Error processing packet
    OverrunError,             ///< Buffer overrun occurred
    GroupError,               ///< Error with group completion
    NoDataTimeout,            ///< No data received within timeout
    DBCDiscontinuity          ///< DBC discontinuity detected
};

/**
 * @brief Structure containing received cycle data
 */
struct ReceivedCycleData {
    void* refCon{nullptr};           ///< Client reference data
    uint32_t payloadLength{0};       ///< Length of payload in bytes
    const uint8_t* payload{nullptr}; ///< Pointer to payload data
    uint32_t isochHeader{0};         ///< Original isochronous header
    uint32_t fireWireTimeStamp{0};   ///< FireWire cycle timestamp
    uint64_t nanoSecondsTimeStamp{0};///< Timestamp in nanoseconds
    uint32_t groupIndex{0};          ///< Group index
    uint32_t packetIndex{0};         ///< Packet index within group
    void* expansionData{nullptr};    ///< Reserved for future use
};

/**
 * @brief Callback for received packet data
 * 
 * @param data Pointer to packet data
 * @param length Length of packet data in bytes
 * @param refCon Client-provided reference context
 */
using PacketCallback = void(*)(const uint8_t* data, size_t length, void* refCon);

/**
 * @brief Callback for structured cycle data
 * 
 * @param data Structured cycle data
 * @param refCon Client-provided reference context
 */
using StructuredDataCallback = void(*)(const ReceivedCycleData& data, void* refCon);

/**
 * @brief Callback for no-data condition
 * 
 * @param lastCycle Last cycle number received
 * @param refCon Client-provided reference context
 */
using NoDataCallback = void(*)(uint32_t lastCycle, void* refCon);

/**
 * @brief Callback for messages from the receiver
 * 
 * @param message Message code
 * @param param1 First parameter (meaning depends on message)
 * @param param2 Second parameter (meaning depends on message)
 * @param refCon Client-provided reference context
 */
using MessageCallback = void(*)(uint32_t message, uint32_t param1, uint32_t param2, void* refCon);

/**
 * @brief Configuration for AMDTP receiver
 */
struct ReceiverConfig {
    uint32_t numGroups{8};           ///< Total number of buffer groups
    uint32_t packetsPerGroup{16};    ///< Number of FW packets per group
    uint32_t packetDataSize{64};     ///< Bytes of audio data per FW packet
    uint32_t callbackGroupInterval{1};///< Trigger callback every N groups
    uint32_t timeout{1000};           ///< Timeout for no-data detection in milliseconds
    bool doIRMAllocations{true};      ///< Whether to use IRM allocations
    uint32_t irmPacketSize{72};       ///< Packet size for IRM allocations
    std::shared_ptr<spdlog::logger> logger; ///< Logger for diagnostics
};

/**
 * @brief Callback for segment completion events (legacy)
 * 
 * @param segment Segment number that completed
 * @param timestamp FireWire timestamp for the segment
 * @param refCon Client-provided reference context
 */
using SegmentCompletionCallback = void(*)(uint32_t segment, uint32_t timestamp, void* refCon);

/**
 * @brief Callback for group completion events
 * 
 * @param groupIndex Group index that completed
 * @param timestamp FireWire timestamp for the group
 * @param refCon Client-provided reference context
 */
using GroupCompletionCallback = void(*)(uint32_t groupIndex, uint32_t timestamp, void* refCon);

/**
 * @brief Represents timing information extracted from a single packet
 */
struct PacketTimingInfo {
    uint32_t fwTimestamp{0};      ///< DCL completion timestamp for this packet's group/DCL
    uint16_t syt{0xFFFF};         ///< SYT field from CIP header (0xFFFF if invalid/no info)
    uint8_t firstDBC{0};          ///< DBC value of the first data block in this packet
    uint32_t numSamplesInPacket{0};///< Total number of valid audio samples extracted from this packet
    uint32_t fdf{0xFF};           ///< FDF field for context
    uint8_t sfc{0xFF};            ///< Sample Frequency Code extracted from FDF (if applicable)
    uint64_t firstAbsSampleIndex{0}; ///< Absolute sample index of the first sample in packet
};

/**
 * @brief Represents a single processed audio sample with its absolute index
 */
struct ProcessedSample {
    float sampleL{0.0f};          ///< Left channel sample value
    float sampleR{0.0f};          ///< Right channel sample value
    uint64_t absoluteSampleIndex{0}; ///< The index of this frame since stream start
};

/**
 * @brief New callback type for passing processed data + timing upstream
 *
 * @param samples Vector of samples from one packet
 * @param timing Timing info for that packet
 * @param refCon Client-provided reference context
 */
using ProcessedDataCallback = void(*)(
    const std::vector<ProcessedSample>& samples,
    const PacketTimingInfo& timing,
    void* refCon
);

/**
 * @brief Represents an audio frame with presentation timestamp for application consumption
 */
struct ProcessedAudioFrame {
    float sampleL{0.0f};          ///< Left channel sample value
    float sampleR{0.0f};          ///< Right channel sample value
    uint64_t presentationNanos{0};///< Host time (in nanoseconds) when this sample should be presented
};

} // namespace Isoch
} // namespace FWA