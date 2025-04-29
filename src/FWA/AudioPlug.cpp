#include "FWA/AudioPlug.hpp"
#include "FWA/JsonHelpers.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <spdlog/spdlog.h>
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
    spdlog::trace("AudioPlug::toJson (Plug 0x{:02x}/{}): connectionInfo_.has_value() = {}, destConnectionInfo_.has_value() = {}",
                  subUnit_, plugNum_, connectionInfo_.has_value(), destConnectionInfo_.has_value());
    j["connectionInfo"] = serializeConnectionInfo();
    j["destConnectionInfo"] = serializeDestConnectionInfo();
    return j;
}
json AudioPlug::serializeConnectionInfo() const {
    spdlog::trace("AudioPlug::serializeConnectionInfo (Plug 0x{:02x}/{}): has_value() = {}", subUnit_, plugNum_, connectionInfo_.has_value());
    if (!connectionInfo_) return nullptr;
    json j_conn;
    std::stringstream ssSU;
    ssSU << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(connectionInfo_->sourceSubUnit);
    j_conn["sourceSubUnit"] = ssSU.str();
    j_conn["sourcePlugNum"] = connectionInfo_->sourcePlugNum;
    std::stringstream ssStat;
    ssStat << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(connectionInfo_->sourcePlugStatus);
    j_conn["sourcePlugStatus"] = ssStat.str();
    spdlog::trace("  -> Serialized connectionInfo: {}", j_conn.dump());
    return j_conn;
}
json AudioPlug::serializeDestConnectionInfo() const {
    spdlog::trace("AudioPlug::serializeDestConnectionInfo (Plug 0x{:02x}/{}): has_value() = {}", subUnit_, plugNum_, destConnectionInfo_.has_value());
    if (!destConnectionInfo_) return nullptr;
    json j_dest;
    spdlog::trace("  -> Accessing destConnectionInfo: DestPlugID={}, StreamPos0={}, StreamPos1={}",
                  destConnectionInfo_->destSubunitPlugId, destConnectionInfo_->streamPosition0, destConnectionInfo_->streamPosition1);
    j_dest["destSubunitPlugId"] = (destConnectionInfo_->destSubunitPlugId == 0xFF) ?
                                  json("None(0xFF)") : json(destConnectionInfo_->destSubunitPlugId);
    j_dest["streamPosition0"] = (destConnectionInfo_->streamPosition0 == 0xFF) ?
                                 json("None(0xFF)") : json(destConnectionInfo_->streamPosition0);
    j_dest["streamPosition1"] = (destConnectionInfo_->streamPosition1 == 0xFF) ?
                                 json("None(0xFF)") : json(destConnectionInfo_->streamPosition1);
    spdlog::trace("  -> Serialized destConnectionInfo: {}", j_dest.dump());
    return j_dest;
}
void AudioPlug::setConnectionInfo(ConnectionInfo info) {
    spdlog::trace("AudioPlug::setConnectionInfo: Storing 0x1A info for plug 0x{:02x}/{}", subUnit_, plugNum_);
    connectionInfo_ = info;
}
void AudioPlug::setDestConnectionInfo(const DestPlugConnectionInfo& info) {
    spdlog::trace("AudioPlug::setDestConnectionInfo: Storing 0x40 fallback info for plug 0x{:02x}/{}: DestPlugID={}, StreamPos0={}, StreamPos1={}",
                  subUnit_, plugNum_, info.destSubunitPlugId, info.streamPosition0, info.streamPosition1);
    destConnectionInfo_ = info;
}
} // namespace FWA
