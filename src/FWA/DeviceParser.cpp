#include "FWA/DeviceParser.hpp"
#include "FWA/AudioDevice.h"
#include "FWA/UnitPlugDiscoverer.hpp"
#include "FWA/SubunitDiscoverer.hpp"
#include "FWA/PlugDetailParser.hpp"
#include "FWA/DescriptorReader.hpp"
#include "FWA/MusicSubunitDescriptorParser.hpp"
#include "FWA/AudioPlug.hpp"
#include "FWA/Helpers.h"
#include <spdlog/spdlog.h>
#include <vector>
#include <memory>
#include <stdexcept>

namespace FWA {

DeviceParser::DeviceParser(AudioDevice* device)
    : device_(device), commandInterface_(nullptr), info_(device->info_)
{
    if (!device_) {
        throw std::runtime_error("DeviceParser: AudioDevice pointer is null.");
    }
    commandInterface_ = device_->getCommandInterface().get();
    if (!commandInterface_) {
        throw std::runtime_error("DeviceParser: CommandInterface pointer is null.");
    }
}

std::expected<void, IOKitError> DeviceParser::parse() {
    UnitPlugDiscoverer unitDiscoverer(commandInterface_);
    SubunitDiscoverer subunitDiscoverer(commandInterface_);
    PlugDetailParser plugDetailParser(commandInterface_);
    DescriptorReader descriptorReader(commandInterface_);
    MusicSubunitDescriptorParser musicDescParser(descriptorReader);

    spdlog::debug("Stage 1: Discovering Unit Plug Counts...");
    if (auto result = unitDiscoverer.discoverUnitPlugs(info_); !result) {
        spdlog::error("FATAL: Failed to discover unit plugs: 0x{:x}", static_cast<int>(result.error()));
        return result;
    }

    spdlog::debug("Stage 2: Parsing Unit Plug Details...");
    if (auto result = parseUnitPlugs(plugDetailParser, info_); !result) {
        spdlog::warn("Stage 2 WARNING: Some unit Iso/External plugs failed: 0x{:x}", static_cast<int>(result.error()));
    }

    spdlog::debug("Stage 3a: Discovering Subunits...");
    if (auto result = subunitDiscoverer.discoverSubunits(info_); !result) {
        spdlog::error("FATAL: Failed to discover subunits: 0x{:x}", static_cast<int>(result.error()));
        return result;
    }
    spdlog::debug("Stage 3a: Finished discovering subunits.");

    spdlog::debug("Stage 3b: Querying Subunit Plug Counts...");
    if (auto result = subunitDiscoverer.queryPlugCounts(info_); !result) {
        spdlog::warn("Stage 3b WARNING: Failed to query some subunit plug info: 0x{:x}", static_cast<int>(result.error()));
    }

    spdlog::debug("Stage 4: Fetching and Parsing Music Subunit Descriptor...");
    if (info_.hasMusicSubunit()) {
        if (auto result = musicDescParser.fetchAndParse(info_.musicSubunit_); !result) {
            spdlog::warn("Stage 4 WARNING: Could not fetch/parse Music Subunit Status Descriptor: 0x{:x}", static_cast<int>(result.error()));
        } else {
            spdlog::info("Successfully parsed Music Subunit Descriptor Info Blocks.");
        }
    }

    spdlog::debug("Stage 5: Parsing detailed plug information for subunits...");
    if (auto result = parseSubunitPlugs(plugDetailParser, info_); !result) {
        spdlog::warn("Stage 5 WARNING: Failed to parse some subunit plug details: 0x{:x}", static_cast<int>(result.error()));
    }

    spdlog::info("Device capability parsing finished successfully for GUID: 0x{:x}", device_->getGuid());
    return {};
}

std::expected<void, IOKitError> DeviceParser::parseUnitPlugs(PlugDetailParser& plugDetailParser, DeviceInfo& info) {
    spdlog::debug("Parsing Unit Iso Plugs (In: {}, Out: {})...", info.getNumIsoInputPlugs(), info.getNumIsoOutputPlugs());
    info.isoInputPlugs_.clear();
    info.isoInputPlugs_.reserve(info.getNumIsoInputPlugs());
    for (uint8_t i = 0; i < info.getNumIsoInputPlugs(); ++i) {
        auto plugResult = plugDetailParser.parsePlugDetails(0xFF, i, PlugDirection::Input, PlugUsage::Isochronous);
        if (plugResult) {
            info.isoInputPlugs_.push_back(plugResult.value());
            spdlog::debug("Added Iso Input plug {}: {}", i, plugResult.value()->getCurrentStreamFormat() ? plugResult.value()->getCurrentStreamFormat()->toString() : "No format");
        } else {
            spdlog::warn("Failed to parse details for Iso In plug {}: 0x{:x}", i, static_cast<int>(plugResult.error()));
        }
    }
    info.isoOutputPlugs_.clear();
    info.isoOutputPlugs_.reserve(info.getNumIsoOutputPlugs());
    for (uint8_t i = 0; i < info.getNumIsoOutputPlugs(); ++i) {
        auto plugResult = plugDetailParser.parsePlugDetails(0xFF, i, PlugDirection::Output, PlugUsage::Isochronous);
        if (plugResult) {
            info.isoOutputPlugs_.push_back(plugResult.value());
            spdlog::debug("Added Iso Output plug {}: {}", i, plugResult.value()->getCurrentStreamFormat() ? plugResult.value()->getCurrentStreamFormat()->toString() : "No format");
        } else {
            spdlog::warn("Failed to parse details for Iso Out plug {}: 0x{:x}", i, static_cast<int>(plugResult.error()));
        }
    }
    spdlog::debug("Parsing Unit External Plugs (In: {}, Out: {})...", info.getNumExternalInputPlugs(), info.getNumExternalOutputPlugs());
    info.externalInputPlugs_.clear();
    info.externalInputPlugs_.reserve(info.getNumExternalInputPlugs());
    for (uint8_t i = 0; i < info.getNumExternalInputPlugs(); ++i) {
        uint8_t plugNum = 0x80 + i;
        auto plugResult = plugDetailParser.parsePlugDetails(0xFF, plugNum, PlugDirection::Input, PlugUsage::External);
        if (plugResult) {
            info.externalInputPlugs_.push_back(plugResult.value());
            spdlog::debug("Added External Input plug {}: {}", i, plugResult.value()->getCurrentStreamFormat() ? plugResult.value()->getCurrentStreamFormat()->toString() : "No format");
        } else {
            spdlog::warn("Failed to parse details for External In plug {}: 0x{:x}", i, static_cast<int>(plugResult.error()));
        }
    }
    info.externalOutputPlugs_.clear();
    info.externalOutputPlugs_.reserve(info.getNumExternalOutputPlugs());
    for (uint8_t i = 0; i < info.getNumExternalOutputPlugs(); ++i) {
        uint8_t plugNum = 0x80 + i;
        auto plugResult = plugDetailParser.parsePlugDetails(0xFF, plugNum, PlugDirection::Output, PlugUsage::External);
        if (plugResult) {
            info.externalOutputPlugs_.push_back(plugResult.value());
            spdlog::debug("Added External Output plug {}: {}", i, plugResult.value()->getCurrentStreamFormat() ? plugResult.value()->getCurrentStreamFormat()->toString() : "No format");
        } else {
            spdlog::warn("Failed to parse details for External Out plug {}: 0x{:x}", i, static_cast<int>(plugResult.error()));
        }
    }
    return {};
}

std::expected<void, IOKitError> DeviceParser::parseSubunitPlugs(PlugDetailParser& plugDetailParser, DeviceInfo& info) {
    if (info.hasMusicSubunit()) {
        auto& music = info.musicSubunit_;
        music.clearMusicSourcePlugs();
        music.clearMusicDestPlugs();
        uint8_t musicSubunitAddr = FWA::Helpers::getSubunitAddress(music.getSubunitType(), music.getId());
        for (uint8_t i = 0; i < music.getMusicDestPlugCount(); ++i) {
            auto plugResult = plugDetailParser.parsePlugDetails(musicSubunitAddr, i, PlugDirection::Input, PlugUsage::MusicSubunit);
            if (plugResult) {
                music.addMusicDestPlug(plugResult.value());
                spdlog::debug("Added Music Dest plug {}: {}", i, plugResult.value()->getCurrentStreamFormat() ? plugResult.value()->getCurrentStreamFormat()->toString() : "No format");
            } else {
                spdlog::warn("Failed to parse details for Music Dest plug {}: 0x{:x}", i, static_cast<int>(plugResult.error()));
            }
        }
        for (uint8_t i = 0; i < music.getMusicSourcePlugCount(); ++i) {
            auto plugResult = plugDetailParser.parsePlugDetails(musicSubunitAddr, i, PlugDirection::Output, PlugUsage::MusicSubunit);
            if (plugResult) {
                music.addMusicSourcePlug(plugResult.value());
                spdlog::debug("Added Music Source plug {}: {}", i, plugResult.value()->getCurrentStreamFormat() ? plugResult.value()->getCurrentStreamFormat()->toString() : "No format");
            } else {
                spdlog::warn("Failed to parse details for Music Source plug {}: 0x{:x}", i, static_cast<int>(plugResult.error()));
            }
        }
    }
    if (info.hasAudioSubunit()) {
        auto& audio = info.audioSubunit_;
        audio.clearAudioSourcePlugs();
        audio.clearAudioDestPlugs();
        uint8_t audioSubunitAddr = FWA::Helpers::getSubunitAddress(audio.getSubunitType(), audio.getId());
        for (uint8_t i = 0; i < audio.getAudioDestPlugCount(); ++i) {
            auto plugResult = plugDetailParser.parsePlugDetails(audioSubunitAddr, i, PlugDirection::Input, PlugUsage::AudioSubunit);
            if (plugResult) {
                audio.addAudioDestPlug(plugResult.value());
                spdlog::debug("Added Audio Dest plug {}: {}", i, plugResult.value()->getCurrentStreamFormat() ? plugResult.value()->getCurrentStreamFormat()->toString() : "No format");
            } else {
                spdlog::warn("Failed to parse details for Audio Dest plug {}: 0x{:x}", i, static_cast<int>(plugResult.error()));
            }
        }
        for (uint8_t i = 0; i < audio.getAudioSourcePlugCount(); ++i) {
            auto plugResult = plugDetailParser.parsePlugDetails(audioSubunitAddr, i, PlugDirection::Output, PlugUsage::AudioSubunit);
            if (plugResult) {
                audio.addAudioSourcePlug(plugResult.value());
                spdlog::debug("Added Audio Source plug {}: {}", i, plugResult.value()->getCurrentStreamFormat() ? plugResult.value()->getCurrentStreamFormat()->toString() : "No format");
            } else {
                spdlog::warn("Failed to parse details for Audio Source plug {}: 0x{:x}", i, static_cast<int>(plugResult.error()));
            }
        }
    }
    return {};
}

} // namespace FWA