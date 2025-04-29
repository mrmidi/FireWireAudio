// include/FWA/AudioStreamFormat.hpp
#pragma once

#include "Enums.hpp" // Include our defined enums
#include "FWA/Error.h" // For IOKitError
#include <string>
#include <vector>
#include <cstdint>
#include <sstream>      // For toString (needed for ChannelFormatInfo::toString declaration)
#include <iomanip>     // For toString formatting
#include <expected>    // For parse function return type declaration
#include <utility>     // For std::move in constructor
#include <optional>    // Useful for potential future extensions
#include <nlohmann/json_fwd.hpp> // For toJson declarations

// Forward declaration (if needed, though not strictly necessary here as spdlog is only used in .cpp)
// namespace spdlog { class logger; }

namespace FWA {

/**
 * @brief Represents per-channel (or per-field) format information within an audio stream.
 *        Defined according to AV/C Stream Format Information Specification.
 */
struct ChannelFormatInfo {
    uint8_t channelCount = 0;                                ///< Number of channels for this format field
    StreamFormatCode formatCode = StreamFormatCode::DontCare; ///< Format code (e.g., MBLA, IEC60958-3)

    // Default constructor, copy/move constructors/assignments are implicitly generated
    ChannelFormatInfo() = default;
    ChannelFormatInfo(uint8_t count, StreamFormatCode code) : channelCount(count), formatCode(code) {}

    /**
     * @brief Convert channel format info to a string.
     * @return std::string Human-readable representation.
     */
    std::string toString() const; // Declare here, define in .cpp

    /**
     * @brief Convert channel format info to JSON.
     * @return nlohmann::json JSON representation.
     */
    nlohmann::json toJson() const;
};

/**
 * @brief Represents the format and capabilities of an audio stream,
 *        typically derived from AV/C Stream Format Information responses (TA 2001002).
 *        This class is designed to be copyable/movable for easy use across C++/Swift boundary.
 */
class AudioStreamFormat {
public:
    /**
     * @brief Default constructor. Initializes to an unknown state.
     */
    AudioStreamFormat() = default;

    /**
     * @brief Construct a new Audio Stream Format object.
     * @param type Format type of the stream.
     * @param sampleRate Sample rate of the stream.
     * @param syncSource Whether this stream is a sync source.
     * @param channels Vector of channel format information.
     */
    AudioStreamFormat(FormatType type, SampleRate sampleRate, bool syncSource, std::vector<ChannelFormatInfo> channels)
      : formatType_(type),
        sampleRate_(sampleRate),
        syncSource_(syncSource),
        channels_(std::move(channels)) {} // Use move

    // Default copy/move constructors and assignment operators are suitable

    // --- Getters ---

    /**
     * @brief Get the format type.
     * @return FormatType The stream's format type.
     */
    FormatType getFormatType() const { return formatType_; }

    /**
     * @brief Get the sample rate.
     * @return SampleRate The stream's sample rate.
     */
    SampleRate getSampleRate() const { return sampleRate_; }

    /**
     * @brief Check if this stream is a sync source.
     * @return bool True if this stream is a sync source.
     */
    bool isSyncSource() const { return syncSource_; }

    /**
     * @brief Get the channel formats. Returns a copy for Swift interop safety.
     * @return std::vector<ChannelFormatInfo> Vector of channel format information.
     */
    std::vector<ChannelFormatInfo> getChannelFormats() const { return channels_; }

    // Parsing is now handled by DeviceParser::parseStreamFormatResponse

    // --- Utility ---

    /**
     * @brief Get a human-readable string representation of the stream format.
     * @return std::string Description of the stream format.
     */
    std::string toString() const; // Declaration only

    /**
     * @brief Serialize the format object back into AV/C stream format block bytes.
     * Currently primarily supports Compound AM824 used by the target device.
     * @return std::vector<uint8_t> The serialized format block, or empty on error/unsupported type.
     */
    std::vector<uint8_t> serializeToBytes() const;

    /**
     * @brief Convert the stream format to JSON.
     * @return nlohmann::json JSON representation.
     */
    nlohmann::json toJson() const;

private:
    static uint8_t sampleRateToByte(SampleRate sr);

    FormatType formatType_{FormatType::Unknown};              ///< Format type of the stream
    SampleRate sampleRate_{SampleRate::Unknown};              ///< Sample rate of the stream
    bool syncSource_{false};                                  ///< Whether this stream is a sync source
    std::vector<ChannelFormatInfo> channels_;                 ///< Channel format information
};

} // namespace FWA