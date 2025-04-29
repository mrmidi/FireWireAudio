#pragma once

#include "FWA/AudioStreamFormat.hpp" // For availableAudioFormats
#include <cstdint>
#include <vector>
#include <optional>
#include <string>
#include <nlohmann/json_fwd.hpp> // For toJson declarations

namespace FWA {

/**
 * @brief Holds static capabilities parsed from the Music Subunit Identifier Descriptor.
 *
 * Based on TA 2001007, Section 5.
 */
struct MusicSubunitCapabilities {
    // Version Info (Sec 5.1)
    uint8_t musicSubunitVersion = 0; // Upper nibble=major, lower=minor

    // Capability Flags (Sec 5.2, Table 5.4)
    bool hasGeneralCapability = false;
    bool hasAudioCapability = false;
    bool hasMidiCapability = false;
    bool hasSmpteTimeCodeCapability = false;
    bool hasSampleCountCapability = false;
    bool hasAudioSyncCapability = false;

    // General Capability Info (Sec 5.2.1, Table 5.5, 5.6)
    std::optional<uint8_t> transmitCapabilityFlags; // Contains Blocking/Non-blocking bit
    std::optional<uint8_t> receiveCapabilityFlags;  // Contains Blocking/Non-blocking bit
    std::optional<uint32_t> latencyCapability;     // FFFF FFFF if not present/reserved

    // Audio Capability Info (Sec 5.2.2)
    std::optional<uint16_t> maxAudioInputChannels;
    std::optional<uint16_t> maxAudioOutputChannels;
    std::optional<std::vector<AudioStreamFormat>> availableAudioFormats; // Parsed FDF/Label info

    // MIDI Capability Info (Sec 5.2.3)
    std::optional<uint16_t> maxMidiInputPorts;
    std::optional<uint16_t> maxMidiOutputPorts;
    std::optional<uint8_t> midiVersionMajor; // Extracted from MIDI_version
    std::optional<uint8_t> midiVersionMinor; // Extracted from MIDI_revision
    std::optional<uint16_t> midiAdaptationLayerVersion; // Table 5.9

    // SMPTE Capability Info (Sec 5.2.4, Table 5.10)
    std::optional<uint8_t> smpteTimeCodeCapabilityFlags; // Tx/Rx bits

    // Sample Count Capability Info (Sec 5.2.5, Table 5.11)
    std::optional<uint8_t> sampleCountCapabilityFlags; // Tx/Rx bits

    // Audio SYNC Capability Info (Sec 5.2.6, Table 5.12)
    std::optional<uint8_t> audioSyncCapabilityFlags; // Bus/Ex bits

    /**
     * @brief Convert capabilities to JSON.
     * @return nlohmann::json JSON representation.
     */
    nlohmann::json toJson() const;

    // --- Helper methods for flag interpretation (optional but useful) ---
    bool supportsBlockingTransmit() const {
        return transmitCapabilityFlags.has_value() && ((*transmitCapabilityFlags & 0x02) != 0); // Check bit 1
    }
    bool supportsNonBlockingTransmit() const {
         return transmitCapabilityFlags.has_value() && ((*transmitCapabilityFlags & 0x01) != 0); // Check bit 0
    }
    bool supportsBlockingReceive() const {
        return receiveCapabilityFlags.has_value() && ((*receiveCapabilityFlags & 0x02) != 0); // Check bit 1
    }
    bool supportsNonBlockingReceive() const {
        return receiveCapabilityFlags.has_value() && ((*receiveCapabilityFlags & 0x01) != 0); // Check bit 0
    }

    // --- NEW: SMPTE Time Code Capability Helpers (Table 5.10) ---
    bool supportsSmpteReceive() const {
        // Bit 0 (xxxx xxx1) indicates Rx capability
        return smpteTimeCodeCapabilityFlags.has_value() && ((*smpteTimeCodeCapabilityFlags & 0x01) != 0);
    }
    bool supportsSmpteTransmit() const {
        // Bit 1 (xxxx xx1x) indicates Tx capability
        return smpteTimeCodeCapabilityFlags.has_value() && ((*smpteTimeCodeCapabilityFlags & 0x02) != 0);
    }

    // --- NEW: Sample Count Capability Helpers (Table 5.11) ---
    bool supportsSampleCountReceive() const {
        // Bit 0 (xxxx xxx1) indicates Rx capability
        return sampleCountCapabilityFlags.has_value() && ((*sampleCountCapabilityFlags & 0x01) != 0);
    }
    bool supportsSampleCountTransmit() const {
        // Bit 1 (xxxx xx1x) indicates Tx capability
        return sampleCountCapabilityFlags.has_value() && ((*sampleCountCapabilityFlags & 0x02) != 0);
    }

    // --- NEW: Audio SYNC Capability Helpers (Table 5.12) ---
    bool supportsAudioSyncReceiveFromBus() const {
        // Bit 0 (xxxx xxx1) indicates receive from 1394 bus
        return audioSyncCapabilityFlags.has_value() && ((*audioSyncCapabilityFlags & 0x01) != 0);
    }
    bool supportsAudioSyncReceiveFromExternal() const {
        // Bit 1 (xxxx xx1x) indicates receive from external sync source
        return audioSyncCapabilityFlags.has_value() && ((*audioSyncCapabilityFlags & 0x02) != 0);
    }
    // Note: Audio SYNC is typically only about *receiving* sync reference.
};

} // namespace FWA
