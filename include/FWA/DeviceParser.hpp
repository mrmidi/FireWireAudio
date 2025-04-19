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
 *
 * Responsible for querying the device using AV/C commands to discover its
 * structure, plugs, subunits, stream formats, and descriptor information.
 */
class DeviceParser {
public:
    /**
     * @brief Construct a new Device Parser object.
     * @param device Raw pointer to the AudioDevice being parsed. Must remain valid for the lifetime of the parser.
     */
    explicit DeviceParser(AudioDevice* device);

    /**
     * @brief Default destructor.
     */
    ~DeviceParser() = default;

    // Delete copy/move operations to prevent unintended duplication
    DeviceParser(const DeviceParser&) = delete;
    DeviceParser& operator=(const DeviceParser&) = delete;
    DeviceParser(DeviceParser&&) = delete;
    DeviceParser& operator=(DeviceParser&&) = delete;

    /**
     * @brief Executes the full device capability parsing sequence.
     * @return std::expected<void, IOKitError> Success or an IOKitError on failure.
     */
    std::expected<void, IOKitError> parse();

private:
    AudioDevice* device_; ///< Non-owning pointer to the device being parsed.
    CommandInterface* commandInterface_; ///< Non-owning pointer to the communication interface.
    DeviceInfo& info_; ///< Reference to the DeviceInfo object to populate.

    uint8_t streamFormatOpcode_; ///< Current opcode to use for stream format commands (handles fallback).
    static constexpr uint8_t kStartingStreamFormatOpcode = 0xBF; ///< Initial extended opcode.
    static constexpr uint8_t kAlternateStreamFormatOpcode  = 0x2F; ///< Legacy opcode.
    static constexpr uint8_t kUnitAddress = 0xFF; ///< AV/C address for the unit itself.

    // --- Helper Methods for Parsing Stages ---

    /** @brief Discovers the number of different types of unit plugs. */
    std::expected<void, IOKitError> discoverUnitPlugs();
    /** @brief Parses details (formats, connections) for unit isochronous plugs. */
    std::expected<void, IOKitError> parseUnitIsoPlugs();
    /** @brief Parses details (formats, connections) for unit external plugs. */
    std::expected<void, IOKitError> parseUnitExternalPlugs();
    /** @brief Discovers which subunits (type and ID) are present on the device. */
    std::expected<void, IOKitError> discoverSubunits();
    /** @brief Queries plug counts and signal sources for discovered subunits. */
    std::expected<void, IOKitError> querySubunitPlugInfo();
    /** @brief Parses detailed information (formats) for subunit plugs. */
    std::expected<void, IOKitError> parseSubunitPlugDetails();
    /** @brief Parses details specific to the Music subunit (plug counts, connections). */
    std::expected<void, IOKitError> parseMusicSubunitDetails(); // Kept for potential refactoring
    /** @brief Parses details specific to the Audio subunit (plug counts, connections). */
    std::expected<void, IOKitError> parseAudioSubunitDetails(); // Kept for potential refactoring
    /** @brief Fetches the raw descriptor data for the Music subunit's identifier descriptor. */
    std::expected<std::vector<uint8_t>, IOKitError> fetchMusicSubunitStatusDescriptor();
    /** @brief Parses info blocks contained within the Music subunit's identifier descriptor data. */
    std::expected<void, IOKitError> parseMusicSubunitStatusDescriptor(const std::vector<uint8_t>& descriptorData);

    // --- Helper Methods for Parsing Specific Items ---

    /** @brief Parses detailed information for a single plug (unit or subunit). */
    std::expected<std::shared_ptr<AudioPlug>, IOKitError> parsePlugDetails(
        uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage);

    /** @brief Queries the currently configured stream format for a specific plug. */
    std::expected<AudioStreamFormat, IOKitError> queryPlugStreamFormat(
        uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage);

    /** @brief Queries all supported stream formats for a specific plug by iterating indices. */
    std::expected<std::vector<AudioStreamFormat>, IOKitError> querySupportedPlugStreamFormats(
         uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage);

    /** @brief Parses an AudioStreamFormat structure from a raw AV/C response payload. */
    std::expected<AudioStreamFormat, IOKitError> parseStreamFormatResponse(
        const std::vector<uint8_t>& responseData,
        uint8_t generatingSubfunction); // Subfunction that generated the response (e.g., C0 or C1)

    /** @brief Queries the source plug connected to a given destination plug. */
    std::expected<AudioPlug::ConnectionInfo, IOKitError> querySignalSource(
        uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage);

    /** @brief Reads a complete descriptor (handling pagination) using OPEN/READ/CLOSE sequence. */
    std::expected<std::vector<uint8_t>, IOKitError> readDescriptor(
        uint8_t subunitAddr,
        DescriptorSpecifierType descriptorSpecifierType, // Type of descriptor to read
        const std::vector<uint8_t>& descriptorSpecifierSpecificData); // Data needed by specifier (e.g., list ID) - currently unused for type 0x00

    /** @brief Sends a command that might involve stream format opcodes, handling fallback. */
    std::expected<std::vector<uint8_t>, IOKitError> sendStreamFormatCommand(std::vector<uint8_t>& command);
};

} // namespace FWA