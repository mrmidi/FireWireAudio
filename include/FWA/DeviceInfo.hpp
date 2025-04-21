#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include "FWA/AudioPlug.hpp"
#include "FWA/AudioStreamFormat.hpp"
#include "FWA/Subunit.hpp"        // Include base Subunit for definition
#include "FWA/MusicSubunit.hpp"   // Include derived MusicSubunit
#include "FWA/AudioSubunit.hpp"   // Include derived AudioSubunit (assuming it exists)
#include "FWA/AVCInfoBlock.hpp"

namespace FWA {

// Forward declarations for friend classes
class DeviceParser;
class UnitPlugDiscoverer;
class SubunitDiscoverer;

/**
 * @brief Container class for device capabilities and configuration information
 *
 * This class holds the discovered capabilities, subunits, and configuration
 * information for a FireWire audio device.
 */
class DeviceInfo {
    // Grant access to the classes responsible for populating this info
    friend class DeviceParser;
    friend class UnitPlugDiscoverer;
    friend class SubunitDiscoverer;

public:
    DeviceInfo() = default;
    ~DeviceInfo() = default;

    // --- Public accessors ---
    bool hasMusicSubunit() const { return hasMusicSubunit_; }
    bool hasAudioSubunit() const { return hasAudioSubunit_; }

    // Provide const and non-const access to subunits
    const MusicSubunit& getMusicSubunit() const { return musicSubunit_; }
    MusicSubunit& getMusicSubunit() { return musicSubunit_; } // Needed for MusicSubunitDescriptorParser & DeviceParser

    const AudioSubunit& getAudioSubunit() const { return audioSubunit_; }
    AudioSubunit& getAudioSubunit() { return audioSubunit_; } // Needed for DeviceParser

    // Unit Plug Counts
    uint32_t getNumIsoInputPlugs() const { return numIsoInPlugs_; }
    uint32_t getNumIsoOutputPlugs() const { return numIsoOutPlugs_; }
    uint32_t getNumExternalInputPlugs() const { return numExtInPlugs_; }
    uint32_t getNumExternalOutputPlugs() const { return numExtOutPlugs_; }

    // Unit Plug Lists (populated by DeviceParser)
    const std::vector<std::shared_ptr<AudioPlug>>& getIsoInputPlugs() const { return isoInputPlugs_; }
    const std::vector<std::shared_ptr<AudioPlug>>& getIsoOutputPlugs() const { return isoOutputPlugs_; }
    const std::vector<std::shared_ptr<AudioPlug>>& getExternalInputPlugs() const { return externalInputPlugs_; }
    const std::vector<std::shared_ptr<AudioPlug>>& getExternalOutputPlugs() const { return externalOutputPlugs_; }

    // Removing this as it seems unused in the context of the parser logic shown
    // const std::vector<std::shared_ptr<AVCInfoBlock>>& getParsedInfoBlocks() const { return parsedInfoBlocks_; }

private:
    // --- Data members managed by friend classes ---

    // Populated by UnitPlugDiscoverer
    uint32_t numIsoInPlugs_ = 0;
    uint32_t numIsoOutPlugs_ = 0;
    uint32_t numExtInPlugs_ = 0;
    uint32_t numExtOutPlugs_ = 0;

    // Populated by SubunitDiscoverer
    bool hasMusicSubunit_{false};
    bool hasAudioSubunit_{false};
    MusicSubunit musicSubunit_; // ID set by SubunitDiscoverer, counts by SubunitDiscoverer, plugs by DeviceParser
    AudioSubunit audioSubunit_; // ID set by SubunitDiscoverer, counts by SubunitDiscoverer, plugs by DeviceParser

    // Populated by DeviceParser (using Plugs created by PlugDetailParser)
    std::vector<std::shared_ptr<AudioPlug>> isoInputPlugs_;
    std::vector<std::shared_ptr<AudioPlug>> isoOutputPlugs_;
    std::vector<std::shared_ptr<AudioPlug>> externalInputPlugs_;
    std::vector<std::shared_ptr<AudioPlug>> externalOutputPlugs_;

    // This member seems unused by the current parsing logic, consider removing if confirmed obsolete.
    // std::vector<std::shared_ptr<AVCInfoBlock>> parsedInfoBlocks_;


    // --- Note on Subunit Modification ---
    // While SubunitDiscoverer is a friend, it should primarily modify
    // hasMusicSubunit_, hasAudioSubunit_, and call public/friend setters on
    // the musicSubunit_ and audioSubunit_ members (like setId, setPlugCounts).
    // DeviceParser will call public/friend methods to add plugs.
    // MusicSubunitDescriptorParser gets a non-const reference via getMusicSubunit()
    // and calls public/friend methods on it.
};

} // namespace FWA