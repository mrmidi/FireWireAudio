#pragma once

#include <cstdint>
#include <memory>
#include <functional> // For std::function if used later, though not for basic callbacks
#include <spdlog/logger.h>
#include <IOKit/firewire/IOFireWireFamilyCommon.h> // For IOFWSpeed

// Forward declare RingBuffer if needed, or include header
// Assumes RingBuffer lives in the raul namespace globally
namespace raul { class RingBuffer; }

namespace FWA {
namespace Isoch {

// --- Configuration ---


enum class TransmissionType {
    Blocking     // Apple's blocking algorithm - only mode supported for 44.1 kHz
};



/**
 * @brief Configuration parameters for the AmdtpTransmitter.
 */
struct TransmitterConfig {
    std::shared_ptr<spdlog::logger> logger; ///< Shared pointer to the logger instance.

    // DCL Program & Buffer Structure (Mirroring Receiver for consistency)
    uint32_t numGroups{8};             ///< Number of buffer groups (segments) in the DCL ring.
    uint32_t packetsPerGroup{16};      ///< Number of FireWire packets per buffer group.
    uint32_t callbackGroupInterval{1}; ///< Trigger DCL completion callback every N groups (1 = every group).

    // Client Data Buffer (Area managed by IsochTransmitBufferManager for client interaction)
    uint32_t clientBufferSize{0};      ///< Size (in bytes) of the buffer area dedicated for client audio data.
                                       ///< Must be large enough for numGroups * packetsPerGroup * calculated_audio_payload_per_packet.

    // Audio Format & Rate
    double sampleRate{44100.0};        ///< Target audio sample rate in Hz.
    uint32_t numChannels{2};           ///< Number of audio channels (e.g., 2 for stereo).
                                       ///< Note: Currently assumes interleaved stereo float in provider.

    // FireWire Isochronous Parameters
    IOFWSpeed initialSpeed{kFWSpeed400MBit}; ///< Initial speed for channel allocation/negotiation.
    uint32_t initialChannel{0xFFFFFFFF};   ///< Initial channel (0xFFFFFFFF = any available).
    bool doIRMAllocations{true};       ///< Whether to use Isochronous Resource Manager for bandwidth/channel.
    uint32_t irmPacketPayloadSize{72}; ///< Maximum PAYLOAD size (CIPHdr + AudioData) in bytes for IRM bandwidth calculation.
                                       ///< Example: 8 bytes CIP Header + 64 bytes Audio Data = 72 bytes.
                                       ///< The Isochronous Header (4 bytes) is NOT included here.

    // Timing & Sync (Potentially add more later)
    uint32_t numStartupCycleMatchBits{0}; ///< For cycle-matching start (0 usually sufficient for transmitter).

    // Fixed: Only blocking mode supported for 44.1 kHz
    TransmissionType transmissionType{TransmissionType::Blocking};
};

// --- Messages & Callbacks ---

/**
 * @brief Enumeration of message types sent from the Transmitter to the client.
 */
enum class TransmitterMessage : uint32_t {
    StreamStarted = 0x2000,   ///< Isochronous stream transmission has successfully started.
    StreamStopped,            ///< Isochronous stream transmission has successfully stopped.
    BufferUnderrun,           ///< Packet provider ran out of client data; silence or NO_DATA sent. (param1=segment, param2=packet)
    OverrunError,             ///< DCL program overrun occurred (hardware couldn't keep up).
    OverrunRecoveryAttempt,   ///< Attempting automatic recovery from overrun.
    OverrunRecoveryFailed,    ///< Automatic recovery from overrun failed; stream stopped.
    AllocatePort,             ///< Remote port allocation occurred (param1=speed, param2=channel). (Info)
    ReleasePort,              ///< Remote port was released. (Info)
    TimestampAdjust,          ///< Internal timestamp adjustment occurred (param1=expected cycle, param2=actual cycle). (Debug/Info)
    Error                     ///< Generic or unrecoverable error occurred.
};

/**
 * @brief Callback function type for messages from the Transmitter.
 *
 * @param message The message code (from TransmitterMessage enum).
 * @param param1 First parameter (message-specific).
 * @param param2 Second parameter (message-specific).
 * @param refCon User-provided reference context pointer.
 */
using MessageCallback = void(*)(uint32_t message, uint32_t param1, uint32_t param2, void* refCon);

// --- Data Structures ---

// CIP Header is now defined in Isoch/core/CIPHeader.hpp
// Include it here for backward compatibility
#include "Isoch/core/CIPHeader.hpp"

/**
 * @brief Structure containing value and mask for Isochronous header control.
 * This is used with SetDCLUserHeaderPtr for hardware-assisted header generation.
 */
struct IsochHeaderValueMask {
    uint32_t value;  ///< Host-endian value with bits we want to control (tag, sy).
    uint32_t mask;   ///< Mask telling the DMA engine which bits from 'value' to use.
};

/**
 * @brief Creates the value/mask pair for Isochronous header control, per Apple's sample code.
 * @param tag The tag field (0-3). Should be 1 for streams with a CIP header.
 * @param sy The sync field (0-15). Typically 0.
 * @return An IsochHeaderValueMask struct with the correct value and mask.
 */
inline IsochHeaderValueMask makeIsoHeader(uint8_t tag, uint8_t sy) {
    IsochHeaderValueMask header{};
    // Mask 0x0000C00F targets bits 15, 14 (Tag) and bits 3,2,1,0 (Sy) in the
    // hardware's internal 32-bit representation before byte-swapping.
    header.value = (static_cast<uint32_t>(tag & 0x3) << 14) | (static_cast<uint32_t>(sy & 0xF));
    header.mask  = 0x0000C00F;
    return header;
}

/**
 * @brief Structure holding information about the packet currently being prepared for transmission.
 *        Passed from the AmdtpTransmitter to the CIPHeaderGenerator and IsochPacketProvider.
 */
struct TransmitPacketInfo {
    uint32_t segmentIndex;        ///< Index of the buffer group (segment) this packet belongs to.
    uint32_t packetIndexInGroup;  ///< Index of this packet within its group (0 to packetsPerGroup-1).
    uint32_t absolutePacketIndex; ///< Index of this packet since stream start (wraps).
    uint64_t hostTimestampNano;   ///< Estimated host time (nanoseconds) when this packet is expected to be sent.
    uint32_t firewireTimestamp;   ///< FireWire cycle time (seconds:cycles:offset) associated with this packet's DCL.

    // Add other relevant info if needed, e.g.:
    // uint64_t absoluteSampleFrameIndex; // Estimated sample frame index for the start of this packet
};

// --- Provider Interface Related ---
// This defines what the AmdtpTransmitter needs from the component that provides the audio data

/**
 * @brief Structure to hold prepared audio data and status from the provider.
 */
struct PreparedPacketData {
    const uint8_t* dataPtr = nullptr; ///< Pointer to the formatted audio data (e.g., AM824 interleaved).
                                      ///< NOTE: This might point into the provider's internal buffer or the target DCL buffer.
    size_t dataLength = 0;            ///< Length of the valid audio data in bytes.
    bool dataAvailable = false;       ///< True if data is available (not an underrun)
    bool generatedSilence = false;    ///< True if the provider had an underrun and generated silence instead.
    bool forceNoDataCIP = false;      ///< True if provider wants to force a NO_DATA CIP packet due to low buffer
};

// --- Constants ---
// Constants for header sizes
constexpr size_t kTransmitCIPHeaderSize = 8;        ///< Size of CIP header in bytes

} // namespace Isoch
} // namespace FWA