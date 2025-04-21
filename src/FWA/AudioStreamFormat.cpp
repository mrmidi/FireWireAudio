// src/FWA/AudioStreamFormat.cpp
#include "FWA/AudioStreamFormat.hpp" // Include the header
#include "FWA/Enums.hpp"
#include <sstream>   // For toString implementation
#include <iomanip>  // For toString formatting
#include <vector>
#include <string>
#include <utility>  // For std::move in constructor
#include <spdlog/spdlog.h> // For logging

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

uint8_t AudioStreamFormat::sampleRateToByte(SampleRate sr) {
    switch (sr) {
        case SampleRate::SR_22050:  return 0x00;
        case SampleRate::SR_24000:  return 0x01;
        case SampleRate::SR_32000:  return 0x02;
        case SampleRate::SR_44100:  return 0x03;
        case SampleRate::SR_48000:  return 0x04;
        case SampleRate::SR_96000:  return 0x05;
        case SampleRate::SR_176400: return 0x06;
        case SampleRate::SR_192000: return 0x07;
        case SampleRate::SR_88200:  return 0x0A;
        case SampleRate::DontCare:  return 0x0F;
        default:                    return 0xFF; // Indicate unknown/error
    }
}

std::vector<uint8_t> AudioStreamFormat::serializeToBytes() const {
    std::vector<uint8_t> bytes;
    if (formatType_ == FormatType::CompoundAM824) {
        bytes.push_back(0x90); // Format_ID MSB
        bytes.push_back(0x40); // Format_ID LSB
        uint8_t srByte = sampleRateToByte(sampleRate_);
        if (srByte == 0xFF) {
            spdlog::error("AudioStreamFormat::serializeToBytes: Cannot serialize unknown sample rate for Compound AM824.");
            return {};
        }
        bytes.push_back(srByte); // sample_rate
        uint8_t byte3 = (syncSource_ ? 0x04 : 0x00); // Bit 2 = sync
        bytes.push_back(byte3);
        if (channels_.size() > 255) {
            spdlog::error("AudioStreamFormat::serializeToBytes: Too many channel fields ({}) for Compound AM824.", channels_.size());
            return {};
        }
        uint8_t numFields = static_cast<uint8_t>(channels_.size());
        bytes.push_back(numFields); // number_of_format_info_fields
        for (const auto& cf : channels_) {
            bytes.push_back(cf.channelCount);
            bytes.push_back(static_cast<uint8_t>(cf.formatCode));
        }
        return bytes;
    } else if (formatType_ == FormatType::AM824) {
        if (channels_.size() != 1 || channels_[0].formatCode != StreamFormatCode::MBLA) {
            spdlog::error("AudioStreamFormat::serializeToBytes: Only support serializing simple AM824 with 1 MBLA field currently.");
            return {};
        }
        bytes.push_back(0x90); // Format_ID MSB
        bytes.push_back(0x00); // Format_ID LSB
        bytes.push_back(static_cast<uint8_t>(StreamFormatCode::MBLA)); // format_code_label
        bytes.push_back(0xFF); // Reserved
        uint8_t srByte = sampleRateToByte(sampleRate_);
        if (srByte == 0xFF) {
            spdlog::error("AudioStreamFormat::serializeToBytes: Cannot serialize unknown sample rate for Simple AM824.");
            return {};
        }
        bytes.push_back((srByte << 4) | 0x0F); // byte4: sample_rate | reserved
        bytes.push_back(0xFF); // byte5: Reserved
        return bytes;
    } else {
        spdlog::error("AudioStreamFormat::serializeToBytes: Unsupported format type for serialization: {}", static_cast<int>(formatType_));
        return {};
    }
}

// Note: The static AudioStreamFormat::parse(...) method implementation
// is intentionally omitted here, as per our decision to place that logic
// within the DeviceParser class (specifically, in a method like
// DeviceParser::parseStreamFormatResponse).

} // namespace FWA