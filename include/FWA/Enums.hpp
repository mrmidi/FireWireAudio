#pragma once
#include <cstdint>

namespace FWA {

enum class PlugDirection : uint8_t {
    Input,
    Output
};

enum class PlugUsage : uint8_t {
    Isochronous,
    External,
    MusicSubunit,
    AudioSubunit,
    Unknown
};

enum class FormatType : uint8_t {
    CompoundAM824,
    AM824,
    Unknown
};

enum class SampleRate : uint8_t {
    SR_22050 = 0x00,
    SR_24000 = 0x01,
    SR_32000 = 0x02,
    SR_44100 = 0x03,
    SR_48000 = 0x04,
    SR_96000 = 0x05,
    SR_176400 = 0x06,
    SR_192000 = 0x07,
    SR_88200 = 0x0A,
    DontCare = 0x0F,
    Unknown = 0xFF
};

enum class MusicSubunitSampleRate : uint8_t {
    SR_32000 = 0x00,
    SR_44100 = 0x01,
    SR_48000 = 0x02,
    SR_88200 = 0x03,
    SR_96000 = 0x04,
    SR_176400 = 0x05,
    SR_192000 = 0x06,
    Unknown = 0xFF
};

} // namespace FWA
