#include "FWA/JsonHelpers.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>

namespace FWA::JsonHelpers {
    std::string plugDirectionToString(PlugDirection dir) {
        return (dir == PlugDirection::Input) ? "Input" : "Output";
    }
    std::string plugUsageToString(PlugUsage usage) {
        switch (usage) {
            case PlugUsage::Isochronous: return "Isochronous";
            case PlugUsage::External: return "External";
            case PlugUsage::MusicSubunit: return "MusicSubunit";
            case PlugUsage::AudioSubunit: return "AudioSubunit";
            default: return "Unknown";
        }
    }
    std::string formatTypeToString(FormatType type) {
        switch (type) {
            case FormatType::CompoundAM824: return "CompoundAM824";
            case FormatType::AM824: return "AM824";
            default: return "Unknown";
        }
    }
    std::string sampleRateToString(SampleRate sr) {
        switch(sr) {
            case SampleRate::SR_44100: return "44.1kHz";
            case SampleRate::SR_48000: return "48kHz";
            case SampleRate::SR_88200: return "88.2kHz";
            case SampleRate::SR_96000: return "96kHz";
            case SampleRate::SR_176400: return "176.4kHz";
            case SampleRate::SR_192000: return "192kHz";
            case SampleRate::SR_22050: return "22.05kHz";
            case SampleRate::SR_24000: return "24kHz";
            case SampleRate::SR_32000: return "32kHz";
            case SampleRate::DontCare: return "Don't Care";
            default: return "Unknown";
        }
    }
    std::string streamFormatCodeToString(StreamFormatCode code) {
        switch(code) {
            case StreamFormatCode::MBLA: return "MBLA";
            case StreamFormatCode::MidiConf: return "MIDI";
            case StreamFormatCode::SyncStream: return "SyncStream";
            case StreamFormatCode::SMPTETimeCode: return "SMPTETimeCode";
            case StreamFormatCode::SampleCount: return "SampleCount";
            default: {
                std::ostringstream oss;
                oss << "Unknown(0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(code) << ")";
                return oss.str();
            }
        }
    }
    std::string infoBlockTypeToString(InfoBlockType type) {
        std::ostringstream oss;
        oss << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(type);
        return oss.str();
    }
    json serializeHexBytes(const std::vector<uint8_t>& bytes) {
        if (bytes.empty()) return nullptr;
        std::ostringstream oss;
        oss << std::hex << std::uppercase << std::setfill('0');
        for (const auto& byte : bytes) {
            oss << std::setw(2) << static_cast<int>(byte);
        }
        return oss.str();
    }
}
