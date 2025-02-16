#pragma once
#include <cstdint>

namespace FWA {

/**
 * @brief Direction of audio plug (input or output)
 */
enum class PlugDirection : uint8_t {
    Input,    ///< Input direction (receiving audio)
    Output    ///< Output direction (sending audio)
};

/**
 * @brief Type of plug usage in the audio device
 */
enum class PlugUsage : uint8_t {
    Isochronous,    ///< Used for isochronous streaming
    External,       ///< External connection
    MusicSubunit,   ///< Connected to music subunit
    AudioSubunit,   ///< Connected to audio subunit
    Unknown         ///< Unknown usage type
};

/**
 * @brief Audio format type
 */
enum class FormatType : uint8_t {
    CompoundAM824,  ///< Compound AM824 format
    AM824,          ///< Standard AM824 format
    Unknown         ///< Unknown format type
};

/**
 * @brief Sample rates supported by the device
 */
enum class SampleRate : uint8_t {
    SR_22050 = 0x00,     ///< 22.05 kHz
    SR_24000 = 0x01,     ///< 24 kHz
    SR_32000 = 0x02,     ///< 32 kHz
    SR_44100 = 0x03,     ///< 44.1 kHz
    SR_48000 = 0x04,     ///< 48 kHz
    SR_96000 = 0x05,     ///< 96 kHz
    SR_176400 = 0x06,    ///< 176.4 kHz
    SR_192000 = 0x07,    ///< 192 kHz
    SR_88200 = 0x0A,     ///< 88.2 kHz
    DontCare = 0x0F,     ///< Sample rate doesn't matter
    Unknown = 0xFF       ///< Unknown sample rate
};

/**
 * @brief Sample rates specifically for music subunit
 */
enum class MusicSubunitSampleRate : uint8_t {
    SR_32000 = 0x00,     ///< 32 kHz
    SR_44100 = 0x01,     ///< 44.1 kHz
    SR_48000 = 0x02,     ///< 48 kHz
    SR_88200 = 0x03,     ///< 88.2 kHz
    SR_96000 = 0x04,     ///< 96 kHz
    SR_176400 = 0x05,    ///< 176.4 kHz
    SR_192000 = 0x06,    ///< 192 kHz
    Unknown = 0xFF       ///< Unknown sample rate
};

} // namespace FWA
