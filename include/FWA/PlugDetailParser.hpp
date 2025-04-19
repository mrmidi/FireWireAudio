#pragma once

#include "FWA/Error.h"
#include "FWA/Enums.hpp" // For PlugDirection, PlugUsage, FormatType, SampleRate etc.
#include "FWA/AudioPlug.hpp" // Need the definition for AudioPlug::ConnectionInfo <--- ADD THIS INCLUDE
#include <expected>
#include <vector>
#include <memory> // For std::shared_ptr
#include <cstdint>

namespace FWA {

// Forward declarations
class CommandInterface;
class AudioPlug; // Keep forward declaration for return type std::shared_ptr<AudioPlug>
class AudioStreamFormat;
// struct ConnectionInfo; // Forward declaration isn't sufficient if used directly in method signature


/**
 * @brief Parses detailed information for a single Audio Plug.
 *
 * Responsible for querying and parsing:
 * - Current stream format.
 * - List of supported stream formats.
 * - Signal source connection (for input plugs).
 * Manages the fallback mechanism for stream format opcodes (0xBF -> 0x2F).
 */
class PlugDetailParser {
public:
    // ... (Constructor, destructor, parsePlugDetails remain the same) ...
     explicit PlugDetailParser(CommandInterface* commandInterface);
    ~PlugDetailParser() = default;

    std::expected<std::shared_ptr<AudioPlug>, IOKitError> parsePlugDetails(
        uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage);

    PlugDetailParser(const PlugDetailParser&) = delete;
    PlugDetailParser& operator=(const PlugDetailParser&) = delete;
    PlugDetailParser(PlugDetailParser&&) = delete;
    PlugDetailParser& operator=(PlugDetailParser&&) = delete;


private:
    CommandInterface* commandInterface_; // Non-owning pointer
    uint8_t streamFormatOpcode_;        // Current opcode to use (starts with 0xBF)

    std::expected<AudioStreamFormat, IOKitError> queryPlugStreamFormat(
        uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage);

    std::expected<std::vector<AudioStreamFormat>, IOKitError> querySupportedPlugStreamFormats(
        uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage);

    /**
     * @brief Queries the signal source connection for a destination (input) plug.
     * <--- CORRECTED RETURN TYPE HERE --->
     */
    std::expected<AudioPlug::ConnectionInfo, IOKitError> querySignalSource( // Use AudioPlug::ConnectionInfo
        uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage);

    std::expected<AudioStreamFormat, IOKitError> parseStreamFormatResponse(
        const std::vector<uint8_t>& responseData, uint8_t generatingSubfunction);

     void buildStreamFormatCommandBase(
        std::vector<uint8_t>& cmd,
        uint8_t subunitAddr,
        uint8_t plugNum,
        PlugDirection direction,
        uint8_t subfunction,
        uint8_t listIndex = 0xFF
    );
};

} // namespace FWA

