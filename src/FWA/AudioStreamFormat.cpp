// src/FWA/AudioStreamFormat.cpp
#include "FWA/AudioStreamFormat.hpp" // Include the header
#include "FWA/Enums.hpp"
#include <sstream>   // For toString implementation
#include <iomanip>  // For toString formatting
#include <vector>
#include <string>
#include <utility>  // For std::move in constructor

namespace FWA {

// --- ChannelFormatInfo toString Implementation ---

std::string ChannelFormatInfo::toString() const {
    std::ostringstream oss;
    // Cast uint8_t to int for correct stream output, otherwise it might print as a character
    oss << static_cast<int>(channelCount) << "ch ";
    switch(formatCode) {
        case StreamFormatCode::IEC60958_3:          oss << "IEC60958-3"; break;
        case StreamFormatCode::IEC61937_3:          oss << "IEC61937-3"; break;
        case StreamFormatCode::IEC61937_4:          oss << "IEC61937-4"; break;
        case StreamFormatCode::IEC61937_5:          oss << "IEC61937-5"; break;
        case StreamFormatCode::IEC61937_6:          oss << "IEC61937-6"; break;
        case StreamFormatCode::IEC61937_7:          oss << "IEC61937-7"; break;
        case StreamFormatCode::MBLA:                oss << "MBLA"; break;
        case StreamFormatCode::DVDAudio:            oss << "MBLA(DVD)"; break;
        case StreamFormatCode::OneBit:              oss << "OneBit(Raw)"; break;
        case StreamFormatCode::OneBitSACD:          oss << "OneBit(SACD)"; break;
        case StreamFormatCode::OneBitEncoded:       oss << "OneBit(EncodedRaw)"; break;
        case StreamFormatCode::OneBitSACDEncoded:   oss << "OneBit(EncodedSACD)"; break;
        case StreamFormatCode::HiPrecisionMBLA:     oss << "HPMBLA"; break;
        case StreamFormatCode::MidiConf:            oss << "MIDI"; break;
        case StreamFormatCode::SMPTETimeCode:       oss << "SMPTE-TimeCode"; break;
        case StreamFormatCode::SampleCount:         oss << "SampleCount"; break;
        case StreamFormatCode::AncillaryData:       oss << "AncillaryData"; break;
        case StreamFormatCode::SyncStream:          oss << "SyncStream"; break;
        case StreamFormatCode::DontCare:            oss << "Don't Care"; break;
        default:
            oss << "Unknown(0x" << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(formatCode) << ")";
            break;
    }
    return oss.str();
}

// --- AudioStreamFormat toString Implementation ---

std::string AudioStreamFormat::toString() const {
    std::ostringstream oss;
    oss << "FormatType: ";
    switch(formatType_) {
        case FormatType::CompoundAM824: oss << "Compound AM824"; break;
        case FormatType::AM824:         oss << "AM824"; break;
        default:                        oss << "Unknown"; break;
    }
    oss << ", SampleRate: ";
    switch(sampleRate_) {
        case SampleRate::SR_22050:  oss << "22.05kHz"; break;
        case SampleRate::SR_24000:  oss << "24kHz"; break;
        case SampleRate::SR_32000:  oss << "32kHz"; break;
        case SampleRate::SR_44100:  oss << "44.1kHz"; break;
        case SampleRate::SR_48000:  oss << "48kHz"; break;
        case SampleRate::SR_96000:  oss << "96kHz"; break;
        case SampleRate::SR_176400: oss << "176.4kHz"; break;
        case SampleRate::SR_192000: oss << "192kHz"; break;
        case SampleRate::SR_88200:  oss << "88.2kHz"; break;
        case SampleRate::DontCare:  oss << "Don't Care"; break;
        default:
            oss << "Unknown (0x" << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(sampleRate_) << ")";
            break;
    }
    oss << ", SyncSource: " << (syncSource_ ? "Yes" : "No");

    if (!channels_.empty()) {
         // Use std::dec to ensure channel count is printed in decimal
         oss << "\n  Channels (" << std::dec << channels_.size() << " field(s)):";
         for (const auto& cf : channels_) {
             oss << "\n    - " << cf.toString(); // Use ChannelFormatInfo's toString
         }
    } else {
        oss << "\n  Channels: None";
    }
    return oss.str();
}

// Note: The static AudioStreamFormat::parse(...) method implementation
// is intentionally omitted here, as per our decision to place that logic
// within the DeviceParser class (specifically, in a method like
// DeviceParser::parseStreamFormatResponse).

} // namespace FWA