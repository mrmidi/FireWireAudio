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

    bool descriptorMechanismSupported_{false}; // <-- Track if standard descriptor mechanism is supported

    // --- New Helper Methods for Plug Parsing ---
    std::expected<void, IOKitError> parseUnitPlugs(class PlugDetailParser& plugDetailParser, DeviceInfo& info);
    std::expected<void, IOKitError> parseSubunitPlugs(class PlugDetailParser& plugDetailParser, DeviceInfo& info);
};

} // namespace FWA