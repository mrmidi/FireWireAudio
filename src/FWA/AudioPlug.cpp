#include "FWA/AudioPlug.hpp"
#include "FWA/JsonHelpers.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
using json = nlohmann::json;
using namespace FWA::JsonHelpers;

namespace FWA {
json AudioPlug::toJson() const {
    json j;
    j["plugNumber"] = plugNum_;
    j["direction"] = plugDirectionToString(direction_);
    j["usage"] = plugUsageToString(usage_);
    j["name"] = plugName_ ? json(*plugName_) : json(nullptr);
    j["currentFormat"] = currentFormat_ ? currentFormat_->toJson() : json(nullptr);
    json supportedArr = json::array();
    for(const auto& fmt : supportedFormats_) supportedArr.push_back(fmt.toJson());
    j["supportedFormats"] = supportedArr;
    j["connectionInfo"] = serializeConnectionInfo();
    j["destConnectionInfo"] = serializeDestConnectionInfo();
    return j;
}
json AudioPlug::serializeConnectionInfo() const {
    if (!connectionInfo_) return nullptr;
    json j_conn;
    std::stringstream ssSU;
    ssSU << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(connectionInfo_->sourceSubUnit);
    j_conn["sourceSubUnit"] = ssSU.str();
    j_conn["sourcePlugNum"] = connectionInfo_->sourcePlugNum;
    std::stringstream ssStat;
    ssStat << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(connectionInfo_->sourcePlugStatus);
    j_conn["sourcePlugStatus"] = ssStat.str();
    return j_conn;
}
json AudioPlug::serializeDestConnectionInfo() const {
    if (!destConnectionInfo_) return nullptr;
    json j_dest;
    if (destConnectionInfo_->destSubunitPlugId == 0xFF) j_dest["destSubunitPlugId"] = "None(0xFF)";
    else j_dest["destSubunitPlugId"] = destConnectionInfo_->destSubunitPlugId;
    if (destConnectionInfo_->streamPosition0 == 0xFF) j_dest["streamPosition0"] = "None(0xFF)";
    else j_dest["streamPosition0"] = destConnectionInfo_->streamPosition0;
    if (destConnectionInfo_->streamPosition1 == 0xFF) j_dest["streamPosition1"] = "None(0xFF)";
    else j_dest["streamPosition1"] = destConnectionInfo_->streamPosition1;
    return j_dest;
}
} // namespace FWA
