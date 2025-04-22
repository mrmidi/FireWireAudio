#pragma once
#include "FWA/Enums.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json_fwd.hpp>

namespace FWA::JsonHelpers {
    using json = nlohmann::json;

    std::string plugDirectionToString(PlugDirection dir);
    std::string plugUsageToString(PlugUsage usage);
    std::string formatTypeToString(FormatType type);
    std::string sampleRateToString(SampleRate sr);
    std::string streamFormatCodeToString(StreamFormatCode code);
    std::string infoBlockTypeToString(InfoBlockType type);
    std::string infoBlockTypeToNameString(InfoBlockType type);
    json serializeHexBytes(const std::vector<uint8_t>& bytes);
}
