#pragma once

#include "Enums.hpp"
#include "AudioStreamFormat.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace FWA {

class AudioPlug {
public:
    AudioPlug(uint8_t subUnit, uint8_t plugNum, PlugDirection direction, PlugUsage usage)
      : subUnit_(subUnit), plugNum_(plugNum), direction_(direction), usage_(usage) {}
    ~AudioPlug() = default;
    
    // Existing getters
    uint8_t getSubUnit() const { return subUnit_; }
    uint8_t getPlugNum() const { return plugNum_; }
    PlugDirection getDirection() const { return direction_; }
    PlugUsage getUsage() const { return usage_; }
    
    // New: Returns the plug number (same as getPlugNum)
    uint8_t getPlugNumber() const {
        return plugNum_;
    }
    
    // New: Returns a human-readable string representing the plug usage.
    std::string getPlugUsageString() const {
         switch (usage_) {
             case PlugUsage::Isochronous:   return "Isochronous";
             case PlugUsage::External:       return "External";
             case PlugUsage::MusicSubunit:   return "Music Subunit";
             case PlugUsage::AudioSubunit:   return "Audio Subunit";
             default:                      return "Unknown";
         }
    }
    
    // Connection info structure (only for destination plugs)
    struct ConnectionInfo {
        uint8_t sourceSubUnit;
        uint8_t sourcePlugNum;
        uint8_t sourcePlugStatus;
    };
    
    const std::optional<ConnectionInfo>& getConnectionInfo() const { return connectionInfo_; }
    void setConnectionInfo(ConnectionInfo info) { connectionInfo_ = info; }
    
    // Current stream format and supported formats
    const std::optional<AudioStreamFormat>& getCurrentStreamFormat() const { return currentFormat_; }
    void setCurrentStreamFormat(const AudioStreamFormat& format) { currentFormat_ = format; }
    
    const std::vector<AudioStreamFormat>& getSupportedStreamFormats() const { return supportedFormats_; }
    void addSupportedStreamFormat(const AudioStreamFormat& format) { supportedFormats_.push_back(format); }
    
    // Optional plug name
    const std::optional<std::string>& getPlugName() const { return plugName_; }
    void setPlugName(const std::string& name) { plugName_ = name; }
    
private:
    uint8_t subUnit_;
    uint8_t plugNum_;
    PlugDirection direction_;
    PlugUsage usage_;
    
    std::optional<ConnectionInfo> connectionInfo_;
    std::optional<AudioStreamFormat> currentFormat_;
    std::vector<AudioStreamFormat> supportedFormats_;
    std::optional<std::string> plugName_;
};

} // namespace FWA
