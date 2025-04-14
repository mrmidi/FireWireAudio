#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include "FWA/AudioPlug.hpp"
#include "FWA/AudioStreamFormat.hpp"
#include "FWA/Subunit.hpp"
#include "FWA/AVCInfoBlock.hpp"

namespace FWA {

/**
 * @brief Container class for device capabilities and configuration information
 * 
 * This class holds the discovered capabilities, subunits, and configuration
 * information for a FireWire audio device.
 */
class DeviceInfo {
    friend class DeviceParser; // Allow parser to modify private members

public:
    DeviceInfo() = default;
    ~DeviceInfo() = default;

    // --- Public accessors ---
    bool hasMusicSubunit() const { return hasMusicSubunit_; }
    bool hasAudioSubunit() const { return hasAudioSubunit_; }
    
    const MusicSubunit& getMusicSubunit() const { return musicSubunit_; }
    MusicSubunit& getMusicSubunit() { return musicSubunit_; }
    
    const AudioSubunit& getAudioSubunit() const { return audioSubunit_; }
    AudioSubunit& getAudioSubunit() { return audioSubunit_; }
    
    uint32_t getNumIsoInputPlugs() const { return numIsoInputPlugs_; }
    uint32_t getNumIsoOutputPlugs() const { return numIsoOutputPlugs_; }
    uint32_t getNumExternalInputPlugs() const { return numExternalInputPlugs_; }
    uint32_t getNumExternalOutputPlugs() const { return numExternalOutputPlugs_; }
    
    const std::vector<std::shared_ptr<AudioPlug>>& getIsoInputPlugs() const { return isoInputPlugs_; }
    const std::vector<std::shared_ptr<AudioPlug>>& getIsoOutputPlugs() const { return isoOutputPlugs_; }
    const std::vector<std::shared_ptr<AudioPlug>>& getExternalInputPlugs() const { return externalInputPlugs_; }
    const std::vector<std::shared_ptr<AudioPlug>>& getExternalOutputPlugs() const { return externalOutputPlugs_; }
    
    const std::vector<std::shared_ptr<AVCInfoBlock>>& getParsedInfoBlocks() const { return parsedInfoBlocks_; }

private:
    // --- Data members managed by DeviceParser ---
    // Unit Plug Counts
    uint32_t numIsoInputPlugs_{0};
    uint32_t numIsoOutputPlugs_{0};
    uint32_t numExternalInputPlugs_{0};
    uint32_t numExternalOutputPlugs_{0};

    // Subunit capabilities
    bool hasMusicSubunit_{false};
    bool hasAudioSubunit_{false};
    MusicSubunit musicSubunit_;
    AudioSubunit audioSubunit_;

    // Unit plugs
    std::vector<std::shared_ptr<AudioPlug>> isoInputPlugs_;
    std::vector<std::shared_ptr<AudioPlug>> isoOutputPlugs_;
    std::vector<std::shared_ptr<AudioPlug>> externalInputPlugs_;
    std::vector<std::shared_ptr<AudioPlug>> externalOutputPlugs_;

    std::vector<std::shared_ptr<AVCInfoBlock>> parsedInfoBlocks_;
};

} // namespace FWA
