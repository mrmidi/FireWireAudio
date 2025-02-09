#pragma once
#include "Enums.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <sstream>

namespace FWA {

/// Represents per-channel (or per-field) information.
struct ChannelFormat {
    uint8_t channelCount;  // e.g. number of channels for this format field
    uint8_t formatCode;    // e.g. MBLA, IEC60958-3, etc.
};

class AudioStreamFormat {
public:
    AudioStreamFormat() = default;
    AudioStreamFormat(FormatType type, SampleRate sampleRate, bool syncSource, std::vector<ChannelFormat> channels)
      : formatType_(type), sampleRate_(sampleRate), syncSource_(syncSource), channels_(std::move(channels)) {}

    // Getters
    FormatType getFormatType() const { return formatType_; }
    SampleRate getSampleRate() const { return sampleRate_; }
    bool isSyncSource() const { return syncSource_; }
    const std::vector<ChannelFormat>& getChannelFormats() const { return channels_; }

    // Setters
    void setFormatType(FormatType type) { formatType_ = type; }
    void setSampleRate(SampleRate rate) { sampleRate_ = rate; }
    void setSyncSource(bool sync) { syncSource_ = sync; }
    void setChannelFormats(const std::vector<ChannelFormat>& channels) { channels_ = channels; }

    // Utility: Returns a human-readable string of the stream format.
    std::string toString() const {
        std::ostringstream oss;
        oss << "Format Type: ";
        switch(formatType_) {
            case FormatType::CompoundAM824: oss << "Compound AM824"; break;
            case FormatType::AM824: oss << "AM824"; break;
            default: oss << "Unknown"; break;
        }
        oss << ", Sample Rate: ";
        switch(sampleRate_) {
            case SampleRate::SR_22050: oss << "22.05KHz"; break;
            case SampleRate::SR_24000: oss << "24KHz"; break;
            case SampleRate::SR_32000: oss << "32KHz"; break;
            case SampleRate::SR_44100: oss << "44.1KHz"; break;
            case SampleRate::SR_48000: oss << "48KHz"; break;
            case SampleRate::SR_96000: oss << "96KHz"; break;
            case SampleRate::SR_176400: oss << "176.4KHz"; break;
            case SampleRate::SR_192000: oss << "192KHz"; break;
            case SampleRate::SR_88200: oss << "88.2KHz"; break;
            default: oss << "Unknown"; break;
        }
        oss << ", Sync Source: " << (syncSource_ ? "Yes" : "No") << "\n";
        oss << "Channel Formats: ";
        for (const auto& cf : channels_) {
            oss << cf.channelCount << " channels (Format Code: " << static_cast<int>(cf.formatCode) << ") ";
        }
        return oss.str();
    }

private:
    FormatType formatType_{FormatType::Unknown};
    SampleRate sampleRate_{SampleRate::Unknown};
    bool syncSource_{false};
    std::vector<ChannelFormat> channels_;
};

} // namespace FWA
