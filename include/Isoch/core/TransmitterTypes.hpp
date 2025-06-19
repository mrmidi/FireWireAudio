#pragma once

#include <cstdint>
#include <memory>
#include <functional> // For std::function if used later, though not for basic callbacks
#include <string>
#include <spdlog/fmt/fmt.h>
#include <spdlog/logger.h>
#include <IOKit/firewire/IOFireWireFamilyCommon.h> // For IOFWSpeed

// Forward declare RingBuffer if needed, or include header
// Assumes RingBuffer lives in the raul namespace globally
namespace raul { class RingBuffer; }

namespace FWA {
namespace Isoch {

// --- Configuration ---

// Maximum number of DCLs to include in a single Notify(kFWNuDCLModifyNotification) call
// Apple's UniversalTransmitter used 30. This prevents overwhelming the kernel driver.
constexpr uint32_t kMaxDCLsPerModifyNotify = 30;


enum class TransmissionType {
    NonBlocking, // Represents the current AmdtpTransmitter SYT behavior (uses FDF=0xFF for SYT placeholders)
    Blocking     // Implements UniversalTransmitter-style SYT/NO_DATA logic
};



/**
 * @brief Configuration parameters for the AmdtpTransmitter.
 */
struct TransmitterConfig {
    uint32_t numGroups{100};
    uint32_t packetsPerGroup{8};
    uint32_t callbackGroupInterval{20};
    uint32_t packetDataSize{64};
    uint32_t clientBufferSize{4096};
    uint32_t numChannels{2};
    TransmissionType transmissionType{TransmissionType::NonBlocking};
    
    double sampleRate{44100.0};
    IOFWSpeed initialSpeed{kFWSpeed400MBit};
    uint32_t initialChannel{0};
    
    uint32_t totalDCLCommands() const { 
        return numGroups * packetsPerGroup; 
    }
    
    uint32_t callbackIntervalMs() const { 
        return callbackGroupInterval * packetsPerGroup * 125 / 1000; 
    }
    
    uint32_t safetyMarginMs() const {
        if (numGroups <= callbackGroupInterval) return 0;
        return (numGroups - callbackGroupInterval) * packetsPerGroup * 125 / 1000;
    }
    
    uint32_t totalCallbacksPerCycle() const {
        if (callbackGroupInterval == 0) return 0;
        return numGroups / callbackGroupInterval;
    }
    
    bool isValid() const {
        if (numGroups == 0 || packetsPerGroup == 0 || callbackGroupInterval == 0) {
            return false; // Basic sanity: no zeros
        }
        if (callbackGroupInterval > numGroups) {
            return false; // Callback interval cannot exceed total groups
        }
        // Apple's safety rule: At least 3x the callback interval worth of groups
        // e.g., if callback is every 20 groups, need at least 60 total groups.
        if (numGroups < callbackGroupInterval * 3) {
            return false;
        }
        // For clean callback cycles and buffer management, numGroups should be divisible
        // by callbackGroupInterval.
        if ((numGroups % callbackGroupInterval) != 0) {
            return false;
        }
        return true;
    }
    
    std::string configSummary() const {
        return fmt::format("{}x{} DCLs, {}ms callbacks ({}Hz), {}ms safety margin",
                          numGroups, packetsPerGroup, 
                          callbackIntervalMs(),
                          callbackIntervalMs() > 0 ? 1000 / callbackIntervalMs() : 0,
                          safetyMarginMs());
    }
    
    std::shared_ptr<spdlog::logger> logger;
};

namespace ApplePresets {
    const TransmitterConfig Classic = {
        .numGroups = 100,
        .packetsPerGroup = 8,
        .callbackGroupInterval = 20,
        .packetDataSize = 64,
        .sampleRate = 44100.0
    };
    
    const TransmitterConfig Conservative = {
        .numGroups = 160,
        .packetsPerGroup = 8,
        .callbackGroupInterval = 20,   
        .packetDataSize = 64,
        .sampleRate = 44100.0
    };
    
    const TransmitterConfig Minimal = {
        .numGroups = 60,
        .packetsPerGroup = 8,
        .callbackGroupInterval = 20,
        .packetDataSize = 64, 
        .sampleRate = 44100.0
    };
}

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

// Forward declaration of CIPHeader - actual definition is in CIPHeader.hpp
struct CIPHeader;

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
 * @brief OLD structure for manual isoch header data. Now OBSOLETE for hardware-assisted mode.
 */
struct IsochHeaderData {
    // IEEE-1394 Isochronous Packet Header (as used by FireWire)
    uint16_t data_length;  // Total data length (filled by hardware at transmit time)
    uint8_t tag_channel;   // Tag (upper 2 bits) and channel (lower 6 bits)
    uint8_t tcode_sy;      // Transaction code (upper 4 bits) and sync code (lower 4 bits)
};

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
};

// --- Constants ---
// Constants for header sizes
constexpr size_t kTransmitCIPHeaderSize = 8;

} // namespace Isoch
} // namespace FWA