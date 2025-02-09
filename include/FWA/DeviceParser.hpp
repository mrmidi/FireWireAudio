#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <vector>
#include "FWA/Error.h"
#include "FWA/AudioDevice.h"
#include "FWA/AudioStreamFormat.hpp"
#include "FWA/AudioPlug.hpp"
#include "FWA/Subunit.hpp"
#include "FWA/DeviceInfo.hpp"

namespace FWA {

/// DeviceParser is responsible for querying the device via its CommandInterface,
/// discovering plug counts, and then performing further discovery based on the
/// discovered plugs.
class DeviceParser {
public:
    explicit DeviceParser(std::shared_ptr<AudioDevice> device);
    ~DeviceParser() = default;

    // Main parse routine.
    std::expected<void, IOKitError> parse();

    // Getters for the discovered subunits.
    std::shared_ptr<MusicSubunit> getMusicSubunit() const { return musicSubunit_; }
    std::shared_ptr<AudioSubunit> getAudioSubunit() const { return audioSubunit_; }

private:
    DeviceInfo& info_;
    std::shared_ptr<AudioDevice> device_;
    std::shared_ptr<MusicSubunit> musicSubunit_;
    std::shared_ptr<AudioSubunit> audioSubunit_;

    // Discovery routines:
    std::expected<void, IOKitError> discoverUnitPlugs();
    std::expected<void, IOKitError> parseIsoInPlugs();
    std::expected<void, IOKitError> parseIsoOutPlugs();
    std::expected<void, IOKitError> parseMusicSubunit();
    std::expected<void, IOKitError> parseAudioSubunit();

    // Helper for parsing a stream format block.
    std::expected<AudioStreamFormat, IOKitError> parseStreamFormat(const std::vector<uint8_t>& response);
    
    static constexpr uint8_t kMusicSubunitSubUnitID = 0x60;
};

} // namespace FWA
