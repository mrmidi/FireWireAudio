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

/**
 * @brief Parser for discovering and configuring FireWire audio device capabilities
 *
 * DeviceParser is responsible for querying the device via its CommandInterface,
 * discovering plug counts, and then performing further discovery based on the
 * discovered plugs.
 */
class DeviceParser {
public:
    /**
     * @brief Construct a new Device Parser
     * @param device Shared pointer to the AudioDevice to parse
     */
    explicit DeviceParser(std::shared_ptr<AudioDevice> device);
    ~DeviceParser() = default;

    /**
     * @brief Main parse routine to discover device capabilities
     * @return Success or error status
     */
    std::expected<void, IOKitError> parse();

    /**
     * @brief Get the discovered music subunit
     * @return std::shared_ptr<MusicSubunit> Pointer to music subunit or nullptr if not found
     */
    std::shared_ptr<MusicSubunit> getMusicSubunit() const { return musicSubunit_; }

    /**
     * @brief Get the discovered audio subunit
     * @return std::shared_ptr<AudioSubunit> Pointer to audio subunit or nullptr if not found
     */
    std::shared_ptr<AudioSubunit> getAudioSubunit() const { return audioSubunit_; }

private:
    DeviceInfo& info_;
    std::shared_ptr<AudioDevice> device_;
    std::shared_ptr<MusicSubunit> musicSubunit_;
    std::shared_ptr<AudioSubunit> audioSubunit_;

    /**
     * @brief Discover and enumerate unit plugs
     * @return Success or error status
     */
    std::expected<void, IOKitError> discoverUnitPlugs();

    /**
     * @brief Parse isochronous input plugs
     * @return Success or error status
     */
    std::expected<void, IOKitError> parseIsoInPlugs();

    /**
     * @brief Parse isochronous output plugs
     * @return Success or error status
     */
    std::expected<void, IOKitError> parseIsoOutPlugs();

    /**
     * @brief Parse music subunit capabilities
     * @return Success or error status
     */
    std::expected<void, IOKitError> parseMusicSubunit();

    /**
     * @brief Parse audio subunit capabilities
     * @return Success or error status
     */
    std::expected<void, IOKitError> parseAudioSubunit();

    /**
     * @brief Parse a stream format block from device response
     * @param response Raw response data containing stream format information
     * @return Parsed AudioStreamFormat or error status
     */
    std::expected<AudioStreamFormat, IOKitError> parseStreamFormat(const std::vector<uint8_t>& response);
    
    static constexpr uint8_t kMusicSubunitSubUnitID = 0x60;  ///< Standard Music subunit ID
};

} // namespace FWA
