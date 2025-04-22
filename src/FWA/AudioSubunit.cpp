#include "FWA/AudioSubunit.hpp"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace FWA {
json AudioSubunit::toJson() const {
    json j;
    j["id"] = getId();
    j["numDestPlugs"] = getAudioDestPlugCount();
    j["numSourcePlugs"] = getAudioSourcePlugCount();
    j["destPlugs"] = json::array();
    for (const auto& plug : getAudioDestPlugs()) {
        if (plug) j["destPlugs"].push_back(plug->toJson());
    }
    j["sourcePlugs"] = json::array();
    for (const auto& plug : getAudioSourcePlugs()) {
        if (plug) j["sourcePlugs"].push_back(plug->toJson());
    }
    return j;
}
} // namespace FWA