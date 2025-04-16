// include/FWA/DeviceParser.hpp
#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <vector>
#include "FWA/Error.h"
#include "FWA/Enums.hpp"             // Include Enums definition
#include "FWA/DescriptorSpecifier.hpp"
#include "FWA/AudioPlug.hpp"         // Include AudioPlug definition (needed for ConnectionInfo and parameters)
#include "FWA/AudioStreamFormat.hpp" // Include AudioStreamFormat definition (needed for return types)

// Forward Declarations (Still okay for types only used as pointers/references internally)
namespace FWA {
    class AudioDevice;
    class CommandInterface;
    class DeviceInfo;
    class MusicSubunit;
    class AudioSubunit;
    class AVCInfoBlock;
} // namespace FWA

namespace FWA {

/**
 * @brief Parses FireWire audio device capabilities and populates DeviceInfo.
 * (Documentation as before)
 */
class DeviceParser {
public:
    explicit DeviceParser(AudioDevice* device);
    ~DeviceParser() = default;

    DeviceParser(const DeviceParser&) = delete;
    DeviceParser& operator=(const DeviceParser&) = delete;
    DeviceParser(DeviceParser&&) = delete;
    DeviceParser& operator=(DeviceParser&&) = delete;

    std::expected<void, IOKitError> parse();

private:
    AudioDevice* device_;
    CommandInterface* commandInterface_;
    DeviceInfo& info_;

    uint8_t streamFormatOpcode_ = kStartingStreamFormatOpcode;
    static constexpr uint8_t kStartingStreamFormatOpcode = 0xBF;
    static constexpr uint8_t kAlternateStreamFormatOpcode  = 0x2F;
    static constexpr uint8_t kUnitAddress = 0xFF;

    // --- Helper Methods for Parsing Stages (Corrected Names) ---
    std::expected<void, IOKitError> discoverUnitPlugs();
    std::expected<void, IOKitError> parseUnitIsoPlugs();          // Correct name
    std::expected<void, IOKitError> parseUnitExternalPlugs();     // Correct name
    std::expected<void, IOKitError> discoverAndParseSubunits();
    std::expected<void, IOKitError> parseMusicSubunitDetails();   // Correct name
    std::expected<void, IOKitError> parseAudioSubunitDetails();   // Correct name
    std::expected<std::vector<uint8_t>, IOKitError> fetchMusicSubunitStatusDescriptor(); // Correct name
    std::expected<void, IOKitError> parseMusicSubunitStatusDescriptor(const std::vector<uint8_t>& descriptorData); // Correct name

    // --- Helper Methods for Parsing Specific Items (Corrected Signatures) ---
    /**
     * @brief Parses details for a single plug (Unit or Subunit).
     */
    std::expected<std::shared_ptr<AudioPlug>, IOKitError> parsePlugDetails(
        uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage); // Use defined enums

    /**
     * @brief Queries the current stream format for a specific plug.
     */
    std::expected<AudioStreamFormat, IOKitError> queryPlugStreamFormat( // Return type needs full definition
        uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage); // Use defined enums

    /**
     * @brief Queries all supported stream formats for a specific plug.
     */
    std::expected<std::vector<AudioStreamFormat>, IOKitError> querySupportedPlugStreamFormats( // Return type needs full definition
         uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage); // Use defined enums

    /**
     * @brief Parses the format information block from a raw AV/C response.
     */
    std::expected<AudioStreamFormat, IOKitError> parseStreamFormatResponse(const std::vector<uint8_t>& responseData); // Correct name & return type

    /**
     * @brief Queries the source connected to a destination plug (Currently Placeholder).
     */
    std::expected<AudioPlug::ConnectionInfo, IOKitError> querySignalSource( // Needs AudioPlug definition
        uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage); // Use defined enums

    /**
     * @brief Fetches a descriptor's raw data (Currently Placeholder).
     */
    std::expected<std::vector<uint8_t>, IOKitError> readDescriptor(
        uint8_t subunitAddr,
        DescriptorSpecifierType descriptorSpecifierType,
        const std::vector<uint8_t>& descriptorSpecifierSpecificData);


     /**
      * @brief Sends a command using the CommandInterface, handling stream format opcode fallback.
      */
    std::expected<std::vector<uint8_t>, IOKitError> sendStreamFormatCommand(std::vector<uint8_t>& command);
};

} // namespace FWA