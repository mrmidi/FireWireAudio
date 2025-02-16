#pragma once
#include "Enums.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <sstream>

namespace FWA {

/**
 * @brief Represents per-channel (or per-field) information in an audio stream
 */
struct ChannelFormat {
    uint8_t channelCount;  ///< Number of channels for this format field
    uint8_t formatCode;    ///< Format code (e.g., MBLA, IEC60958-3)
};

/**
 * @brief Represents the format and capabilities of an audio stream
 * 
 * This class encapsulates the format type, sample rate, synchronization source status,
 * and channel format information for an audio stream.
 */
class AudioStreamFormat {
public:
    AudioStreamFormat() = default;

    /**
     * @brief Construct a new Audio Stream Format object
     * @param type Format type of the stream
     * @param sampleRate Sample rate of the stream
     * @param syncSource Whether this stream is a sync source
     * @param channels Vector of channel format information
     */
    AudioStreamFormat(FormatType type, SampleRate sampleRate, bool syncSource, std::vector<ChannelFormat> channels)
      : formatType_(type), sampleRate_(sampleRate), syncSource_(syncSource), channels_(std::move(channels)) {}

    /**
     * @brief Get the format type
     * @return FormatType The stream's format type
     */
    FormatType getFormatType() const { return formatType_; }

    /**
     * @brief Get the sample rate
     * @return SampleRate The stream's sample rate
     */
    SampleRate getSampleRate() const { return sampleRate_; }

    /**
     * @brief Check if this stream is a sync source
     * @return bool True if this stream is a sync source
     */
    bool isSyncSource() const { return syncSource_; }

    /**
     * @brief Get the channel formats
     * @return const std::vector<ChannelFormat>& Vector of channel format information
     */
    const std::vector<ChannelFormat>& getChannelFormats() const { return channels_; }

    /**
     * @brief Set the format type
     * @param type New format type to set
     */
    void setFormatType(FormatType type) { formatType_ = type; }

    /**
     * @brief Set the sample rate
     * @param rate New sample rate to set
     */
    void setSampleRate(SampleRate rate) { sampleRate_ = rate; }

    /**
     * @brief Set the sync source status
     * @param sync New sync source status
     */
    void setSyncSource(bool sync) { syncSource_ = sync; }

    /**
     * @brief Set the channel formats
     * @param channels New vector of channel format information
     */
    void setChannelFormats(const std::vector<ChannelFormat>& channels) { channels_ = channels; }

    /**
     * @brief Get a human-readable string representation of the stream format
     * @return std::string Description of the stream format
     */
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
    FormatType formatType_{FormatType::Unknown};      ///< Format type of the stream
    SampleRate sampleRate_{SampleRate::Unknown};      ///< Sample rate of the stream
    bool syncSource_{false};                          ///< Whether this stream is a sync source
    std::vector<ChannelFormat> channels_;             ///< Channel format information
};

} // namespace FWA
