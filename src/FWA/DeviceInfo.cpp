#include "FWA/DeviceInfo.hpp"
#include "FWA/JsonHelpers.hpp"
#include "FWA/AudioDevice.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
using json = nlohmann::json;
using namespace FWA::JsonHelpers;

namespace FWA {
json DeviceInfo::toJson(const AudioDevice& device) const {
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
    if (hasMusicSubunit_) {
        subunitsJson["music"] = serializeMusicSubunit(device);
    }
    if (hasAudioSubunit_) {
         subunitsJson["audio"] = serializeAudioSubunit(device);
    }
    j["subunits"] = subunitsJson;

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
    json j = json::object();
    j["id"] = musicSubunit_.getId();
    j["numDestPlugs"] = musicSubunit_.getMusicDestPlugCount();
    j["numSourcePlugs"] = musicSubunit_.getMusicSourcePlugCount();
    j["destPlugs"] = serializePlugList(musicSubunit_.getMusicDestPlugs());
    j["sourcePlugs"] = serializePlugList(musicSubunit_.getMusicSourcePlugs());
    if (musicSubunit_.getStatusDescriptorData()) {
        j["statusDescriptorRaw"] = serializeHexBytes(musicSubunit_.getStatusDescriptorData().value());
        j["statusDescriptorParsed"] = serializeInfoBlockList(musicSubunit_.getParsedStatusInfoBlocks());
    }
    return j;
}

json DeviceInfo::serializeAudioSubunit(const AudioDevice& device) const {
    json j = json::object();
    j["id"] = audioSubunit_.getId();
    j["numDestPlugs"] = audioSubunit_.getAudioDestPlugCount();
    j["numSourcePlugs"] = audioSubunit_.getAudioSourcePlugCount();
    j["destPlugs"] = serializePlugList(audioSubunit_.getAudioDestPlugs());
    j["sourcePlugs"] = serializePlugList(audioSubunit_.getAudioSourcePlugs());
    return j;
}

json DeviceInfo::serializeInfoBlockList(const std::vector<std::shared_ptr<AVCInfoBlock>>& blocks) const {
    json arr = json::array();
    for (const auto& blockPtr : blocks) {
        if (blockPtr) arr.push_back(blockPtr->toJson());
    }
    return arr;
}
} // namespace FWA
