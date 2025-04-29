#include "FWA/MusicSubunit.hpp"
#include "FWA/JsonHelpers.hpp"
#include <nlohmann/json.hpp>
using json = nlohmann::json;
using namespace FWA::JsonHelpers;
#include <spdlog/spdlog.h>


namespace FWA {
json MusicSubunit::toJson() const {
    json j;
    j["id"] = getId();
    j["subunitType"] = getSubunitTypeName(); 

    // --- Serialize capabilities ---
    if (capabilities_) {
        spdlog::debug("MusicSubunit::toJson: Capabilities object exists. Calling capabilities_->toJson()."); // <-- Log Before Call
        j["staticCapabilities"] = capabilities_->toJson();
    } else {
        spdlog::debug("MusicSubunit::toJson: Capabilities object does NOT exist (is nullopt). Setting JSON to null."); // <-- Log If Null
        j["staticCapabilities"] = nullptr;
    }

    // --- Existing plug serialization ---
    j["numDestPlugs"] = getMusicDestPlugCount();
    j["numSourcePlugs"] = getMusicSourcePlugCount();
    json destPlugsArr = json::array();
    for (const auto& plug : getMusicDestPlugs()) {
        if (plug) destPlugsArr.push_back(plug->toJson());
    }
    j["destPlugs"] = destPlugsArr;

    json sourcePlugsArr = json::array();
    for (const auto& plug : getMusicSourcePlugs()) {
        if (plug) sourcePlugsArr.push_back(plug->toJson());
    }
    j["sourcePlugs"] = sourcePlugsArr;

    // --- Existing status descriptor serialization ---
    if (getStatusDescriptorData()) {
        j["statusDescriptorRaw"] = serializeHexBytes(getStatusDescriptorData().value());
        json parsedBlocksArr = json::array();
        for (const auto& block : getParsedStatusInfoBlocks()) {
            if (block) parsedBlocksArr.push_back(block->toJson());
        }
         j["statusDescriptorParsed"] = parsedBlocksArr;
    } else {
         j["statusDescriptorRaw"] = nullptr;
         j["statusDescriptorParsed"] = nullptr;
    }

    return j;
}
} // namespace FWA