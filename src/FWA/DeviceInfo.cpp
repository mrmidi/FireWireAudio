#include "FWA/DeviceInfo.hpp"
#include "FWA/JsonHelpers.hpp"
#include "FWA/AudioDevice.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <spdlog/spdlog.h>
using json = nlohmann::json;
using namespace FWA::JsonHelpers;

namespace FWA {
json DeviceInfo::toJson(const AudioDevice& device) const {
    spdlog::debug("DeviceInfo::toJson: Entering for GUID 0x{:x}", device.getGuid());
    json j;
    // Basic Info - Get from the passed device object
    std::stringstream ssGuid;
    ssGuid << "0x" << std::hex << device.getGuid();
    j["guid"] = ssGuid.str();
    j["deviceName"] = device.getDeviceName();
    j["vendorName"] = device.getVendorName();
    std::stringstream ssVendorId, ssModelId;
    ssVendorId << "0x" << std::hex << std::setw(8) << std::setfill('0') << device.getVendorID();
    ssModelId << "0x" << std::hex << std::setw(8) << std::setfill('0') << device.getModelID();
    j["vendorId"] = ssVendorId.str();
    j["modelId"] = ssModelId.str();

    // Unit Plugs (Access members of 'this' DeviceInfo object)
    json unitPlugsJson = json::object();
    unitPlugsJson["numIsoInput"] = numIsoInPlugs_;
    unitPlugsJson["numIsoOutput"] = numIsoOutPlugs_;
    unitPlugsJson["numExternalInput"] = numExtInPlugs_;
    unitPlugsJson["numExternalOutput"] = numExtOutPlugs_;
    unitPlugsJson["isoInputPlugs"] = serializePlugList(isoInputPlugs_);
    unitPlugsJson["isoOutputPlugs"] = serializePlugList(isoOutputPlugs_);
    unitPlugsJson["externalInputPlugs"] = serializePlugList(externalInputPlugs_);
    unitPlugsJson["externalOutputPlugs"] = serializePlugList(externalOutputPlugs_);
    j["unitPlugs"] = unitPlugsJson;

    // Subunits (Access members of 'this' DeviceInfo object)
    json subunitsJson = json::object();
    spdlog::debug("DeviceInfo::toJson: Checking subunits. hasMusicSubunit_ = {}, hasAudioSubunit_ = {}", hasMusicSubunit_, hasAudioSubunit_);
    if (hasMusicSubunit_) {
        spdlog::debug("DeviceInfo::toJson: Calling serializeMusicSubunit...");
        subunitsJson["music"] = serializeMusicSubunit(device);
    } else {
        spdlog::debug("DeviceInfo::toJson: Skipping music subunit serialization (flag is false).");
    }
    if (hasAudioSubunit_) {
        spdlog::debug("DeviceInfo::toJson: Calling serializeAudioSubunit...");
        subunitsJson["audio"] = serializeAudioSubunit(device);
    } else {
        spdlog::debug("DeviceInfo::toJson: Skipping audio subunit serialization (flag is false).");
    }
    j["subunits"] = subunitsJson;

    spdlog::debug("DeviceInfo::toJson: Exiting for GUID 0x{:x}", device.getGuid());
    return j;
}

json DeviceInfo::serializePlugList(const std::vector<std::shared_ptr<AudioPlug>>& plugs) const {
    json arr = json::array();
    for (const auto& plug : plugs) {
        if (plug) arr.push_back(plug->toJson());
    }
    return arr;
}

json DeviceInfo::serializeMusicSubunit(const AudioDevice& device) const {
    spdlog::debug("DeviceInfo::serializeMusicSubunit: Entering. Calling musicSubunit_.toJson().");
    return musicSubunit_.toJson();
}

json DeviceInfo::serializeAudioSubunit(const AudioDevice& device) const {
    spdlog::debug("DeviceInfo::serializeAudioSubunit: Entering. Calling audioSubunit_.toJson().");
    return audioSubunit_.toJson();
}

json DeviceInfo::serializeInfoBlockList(const std::vector<std::shared_ptr<AVCInfoBlock>>& blocks) const {
    json arr = json::array();
    for (const auto& blockPtr : blocks) {
        if (blockPtr) arr.push_back(blockPtr->toJson());
    }
    return arr;
}
} // namespace FWA
