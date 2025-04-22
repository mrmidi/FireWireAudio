#include "FWA/MusicSubunit.hpp"
#include "FWA/JsonHelpers.hpp"
#include <nlohmann/json.hpp>
using json = nlohmann::json;
using namespace FWA::JsonHelpers;

namespace FWA {
json MusicSubunit::toJson() const {
    json j;
    j["id"] = getId();
    j["numDestPlugs"] = getMusicDestPlugCount();
    j["numSourcePlugs"] = getMusicSourcePlugCount();
    j["destPlugs"] = json::array();
    for (const auto& plug : getMusicDestPlugs()) {
        if (plug) j["destPlugs"].push_back(plug->toJson());
    }
    j["sourcePlugs"] = json::array();
    for (const auto& plug : getMusicSourcePlugs()) {
        if (plug) j["sourcePlugs"].push_back(plug->toJson());
    }
    if (getStatusDescriptorData()) {
        j["statusDescriptorRaw"] = serializeHexBytes(getStatusDescriptorData().value());
        j["statusDescriptorParsed"] = json::array();
        for (const auto& block : getParsedStatusInfoBlocks()) {
            if (block) j["statusDescriptorParsed"].push_back(block->toJson());
        }
    }
    return j;
}
} // namespace FWA