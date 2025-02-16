#pragma once

#include "Enums.hpp"
#include "AudioStreamFormat.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace FWA {

/**
 * @brief Represents an audio plug (input or output) on a FireWire audio device
 * 
 * This class encapsulates the properties and capabilities of an audio plug,
 * including its connection status, stream format, and supported formats.
 */
class AudioPlug {
public:
    /**
     * @brief Construct a new Audio Plug object
     * @param subUnit Subunit ID that owns this plug
     * @param plugNum Plug number within the subunit
     * @param direction Direction of the plug (input/output)
     * @param usage Usage type of the plug
     */
    AudioPlug(uint8_t subUnit, uint8_t plugNum, PlugDirection direction, PlugUsage usage)
      : subUnit_(subUnit), plugNum_(plugNum), direction_(direction), usage_(usage) {}
    
    ~AudioPlug() = default;
    
    /**
     * @brief Get the subunit ID
     * @return uint8_t The subunit ID
     */
    uint8_t getSubUnit() const { return subUnit_; }

    /**
     * @brief Get the plug number
     * @return uint8_t The plug number
     */
    uint8_t getPlugNum() const { return plugNum_; }

    /**
     * @brief Get the plug direction
     * @return PlugDirection The direction (input/output)
     */
    PlugDirection getDirection() const { return direction_; }

    /**
     * @brief Get the plug usage type
     * @return PlugUsage The usage type
     */
    PlugUsage getUsage() const { return usage_; }
    
    /**
     * @brief Get the plug number (alias for getPlugNum)
     * @return uint8_t The plug number
     */
    uint8_t getPlugNumber() const { return plugNum_; }
    
    /**
     * @brief Get a human-readable string representing the plug usage
     * @return std::string Usage description
     */
    std::string getPlugUsageString() const {
         switch (usage_) {
             case PlugUsage::Isochronous:   return "Isochronous";
             case PlugUsage::External:       return "External";
             case PlugUsage::MusicSubunit:   return "Music Subunit";
             case PlugUsage::AudioSubunit:   return "Audio Subunit";
             default:                      return "Unknown";
         }
    }
    
    /**
     * @brief Structure containing connection information for destination plugs
     */
    struct ConnectionInfo {
        uint8_t sourceSubUnit;    ///< Source subunit ID
        uint8_t sourcePlugNum;    ///< Source plug number
        uint8_t sourcePlugStatus; ///< Status of the source plug
    };
    
    /**
     * @brief Get the current connection information
     * @return const std::optional<ConnectionInfo>& Optional connection info
     */
    const std::optional<ConnectionInfo>& getConnectionInfo() const { return connectionInfo_; }

    /**
     * @brief Set the connection information
     * @param info Connection information to set
     */
    void setConnectionInfo(ConnectionInfo info) { connectionInfo_ = info; }
    
    /**
     * @brief Get the current stream format
     * @return const std::optional<AudioStreamFormat>& Optional current format
     */
    const std::optional<AudioStreamFormat>& getCurrentStreamFormat() const { return currentFormat_; }

    /**
     * @brief Set the current stream format
     * @param format Format to set as current
     */
    void setCurrentStreamFormat(const AudioStreamFormat& format) { currentFormat_ = format; }
    
    /**
     * @brief Get the list of supported stream formats
     * @return const std::vector<AudioStreamFormat>& List of supported formats
     */
    const std::vector<AudioStreamFormat>& getSupportedStreamFormats() const { return supportedFormats_; }

    /**
     * @brief Add a supported stream format
     * @param format Format to add to supported formats list
     */
    void addSupportedStreamFormat(const AudioStreamFormat& format) { supportedFormats_.push_back(format); }
    
    /**
     * @brief Get the plug name if available
     * @return const std::optional<std::string>& Optional plug name
     */
    const std::optional<std::string>& getPlugName() const { return plugName_; }

    /**
     * @brief Set the plug name
     * @param name Name to set for the plug
     */
    void setPlugName(const std::string& name) { plugName_ = name; }
    
private:
    uint8_t subUnit_;               ///< Subunit ID
    uint8_t plugNum_;              ///< Plug number
    PlugDirection direction_;       ///< Direction of the plug
    PlugUsage usage_;              ///< Usage type of the plug
    
    std::optional<ConnectionInfo> connectionInfo_;        ///< Current connection info
    std::optional<AudioStreamFormat> currentFormat_;     ///< Current stream format
    std::vector<AudioStreamFormat> supportedFormats_;    ///< Supported stream formats
    std::optional<std::string> plugName_;               ///< Optional plug name
};

} // namespace FWA
