#include "FWA/SubunitDiscoverer.hpp"
#include "FWA/CommandInterface.h"
#include "FWA/DeviceInfo.hpp"
#include "FWA/Subunit.hpp"
#include "FWA/Enums.hpp"
#include "FWA/Helpers.h"
#include <spdlog/spdlog.h>
#include <IOKit/avc/IOFireWireAVCConsts.h>

namespace FWA {

SubunitDiscoverer::SubunitDiscoverer(CommandInterface* commandInterface)
    : commandInterface_(commandInterface) {}

std::expected<void, IOKitError> SubunitDiscoverer::discoverSubunits(DeviceInfo& info) {
    spdlog::debug("SubunitDiscoverer: Discovering subunits (types and IDs only)...");
    std::vector<uint8_t> cmd = {
        kAVCStatusInquiryCommand,
        0xFF, // Unit Address
        kAVCSubunitInfoOpcode, // 0x31
        0x07, // Page 7 (Maximum configuration)
        0xFF, 0xFF, 0xFF, 0xFF // Wildcards for response operands
    };
    auto respResult = this->commandInterface_->sendCommand(cmd);
    if (!respResult) {
        spdlog::error("SubunitDiscoverer: Failed to send subunit info command.");
        return std::unexpected(respResult.error());
    }
    const auto& response = respResult.value();
    if (response.size() < 5 || (response[0] != kAVCImplementedStatus && response[0] != kAVCAcceptedStatus)) {
        spdlog::error("SubunitDiscoverer: Subunit info command (page 7) returned unexpected status (0x{:02x}) or insufficient bytes ({}).", response.size() >=1 ? response[0] : 0xFF, response.size());
        return std::unexpected(IOKitError(kIOReturnError));
    }
    info.hasMusicSubunit_ = false;
    info.hasAudioSubunit_ = false;
    spdlog::debug("SubunitDiscoverer: Parsing SUBUNIT INFO (Page 7) response:");
    for (size_t i = 4; i < response.size(); ++i) {
        uint8_t typeIdByte = response[i];
        if (typeIdByte == 0xFF) continue;
        uint8_t subunitType = (typeIdByte >> 3);
        uint8_t subunitID = (typeIdByte & 0x07);
        spdlog::debug("  Found Subunit Entry: Type=0x{:02x}, ID={} (from byte 0x{:02x} at index {})",
                      subunitType, subunitID, typeIdByte, i);
        switch (static_cast<SubunitType>(subunitType)) {
            case SubunitType::Music:
                spdlog::debug("  Found Music Subunit (ID: {})", subunitID);
                if (!info.hasMusicSubunit_) {
                    info.hasMusicSubunit_ = true;
                    info.musicSubunit_.setId(subunitID);
                }
                break;
            case SubunitType::Audio:
                spdlog::debug("  Found Audio Subunit (ID: {})", subunitID);
                if (!info.hasAudioSubunit_) {
                    info.hasAudioSubunit_ = true;
                    info.audioSubunit_.setId(subunitID);
                }
                break;
            default:
                spdlog::debug("  Ignoring unknown or unsupported subunit type 0x{:02x}", subunitType);
                break;
        }
    }
    if (!info.hasMusicSubunit_ && !info.hasAudioSubunit_) {
        spdlog::info("SubunitDiscoverer: No Music or Audio subunits detected in the SUBUNIT INFO response.");
    } else {
        if(info.hasMusicSubunit_)
            spdlog::info("SubunitDiscoverer: Music subunit detected (ID: {}).", info.musicSubunit_.getId());
        if(info.hasAudioSubunit_)
            spdlog::info("SubunitDiscoverer: Audio subunit detected (ID: {}).", info.audioSubunit_.getId());
    }
    return {};
}

std::expected<void, IOKitError> SubunitDiscoverer::queryPlugCounts(DeviceInfo& info) {
    spdlog::debug("SubunitDiscoverer: Querying plug information for discovered subunits...");
    if (info.hasMusicSubunit()) {
        spdlog::debug("SubunitDiscoverer: Querying Music Subunit Plug Info...");
        uint8_t subunitAddr = FWA::Helpers::getSubunitAddress(SubunitType::Music, info.musicSubunit_.getId());
        std::vector<uint8_t> music_plug_cmd = {
            kAVCStatusInquiryCommand,
            subunitAddr,
            kAVCPlugInfoOpcode,
            0x01, 0xFF, 0xFF, 0xFF, 0xFF
        };
        auto musicPlugResp = this->commandInterface_->sendCommand(music_plug_cmd);
        if (musicPlugResp && (musicPlugResp.value()[0] == kAVCImplementedStatus || musicPlugResp.value()[0] == kAVCAcceptedStatus)) {
            const auto& response = musicPlugResp.value();
            if (response.size() >= 8) {
                info.musicSubunit_.setMusicDestPlugCount(response[4]);
                info.musicSubunit_.setMusicSourcePlugCount(response[5]);
                spdlog::info("SubunitDiscoverer: Music Subunit plugs: Dest={}, Source={}",
                    info.musicSubunit_.getMusicDestPlugCount(),
                    info.musicSubunit_.getMusicSourcePlugCount());
            } else {
                spdlog::warn("SubunitDiscoverer: Music Subunit plug info response too short ({} bytes).", response.size());
            }
        } else {
            spdlog::warn("SubunitDiscoverer: Failed to send or invalid response for Music Subunit PLUG INFO command.");
        }
    }
    if (info.hasAudioSubunit()) {
        spdlog::debug("SubunitDiscoverer: Querying Audio Subunit Plug Info...");
        uint8_t subunitAddr = FWA::Helpers::getSubunitAddress(SubunitType::Audio, info.audioSubunit_.getId());
        std::vector<uint8_t> audio_plug_cmd = {
            kAVCStatusInquiryCommand,
            subunitAddr,
            kAVCPlugInfoOpcode,
            0x01, 0xFF, 0xFF, 0xFF, 0xFF
        };
        auto audioPlugResp = this->commandInterface_->sendCommand(audio_plug_cmd);
        if (audioPlugResp) {
            const auto& response = audioPlugResp.value();
            if (response[0] == kAVCNotImplementedStatus) {
                spdlog::warn("SubunitDiscoverer: Audio Subunit does not implement PLUG INFO command (Status=0x{:02x}). Cannot determine plug counts.", response[0]);
                return {};
            } else if (response.size() >= 8 && (response[0] == kAVCImplementedStatus || response[0] == kAVCAcceptedStatus)) {
                info.audioSubunit_.setAudioDestPlugCount(response[4]);
                info.audioSubunit_.setAudioSourcePlugCount(response[5]);
                spdlog::info("SubunitDiscoverer: Audio Subunit plugs: Dest={}, Source={}",
                    info.audioSubunit_.getAudioDestPlugCount(),
                    info.audioSubunit_.getAudioSourcePlugCount());
            } else {
                spdlog::warn("SubunitDiscoverer: Audio Subunit plug info response too short or invalid status ({} bytes, status=0x{:02x}).", response.size(), response[0]);
            }
        } else {
            spdlog::warn("SubunitDiscoverer: Failed to send Audio Subunit PLUG INFO command.");
        }
    }
    return {};
}

} // namespace FWA
