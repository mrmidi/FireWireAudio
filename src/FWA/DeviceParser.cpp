#include "FWA/DeviceParser.hpp"
#include "FWA/CommandInterface.h"
#include "FWA/AudioDevice.h"
#include "FWA/DeviceInfo.hpp"
#include "FWA/DescriptorSpecifier.hpp" 
#include "FWA/DescriptorUtils.hpp"
#include <IOKit/avc/IOFireWireAVCConsts.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <algorithm> // For std::min

constexpr uint8_t kAVCReadDescriptorOpcode = 0x09;
constexpr uint8_t kAVCOpenDescriptorOpcode = 0x08;
// constexpr uint8_t kAVCStatusInquiryCommand = 0x01; // Standard STATUS command ctype
constexpr uint8_t kAVCStreamFormatCurrentQuerySubfunction = 0xC0; // Subfunction for CURRENT format
constexpr uint8_t kAVCStreamFormatSupportedQuerySubfunction = 0xC1; // Subfunction for SUPPORTED formats (by index)

#ifndef kIOReturnBadResponse
#define kIOReturnBadResponse kIOReturnError
#endif
#ifndef kIOReturnIOError
#define kIOReturnIOError kIOReturnError
#endif

#include "FWA/CommandInterface.h" // Need full definition
#include "FWA/AudioDevice.h"       // Need full definition
#include "FWA/DeviceInfo.hpp"      // Need full definition
#include "FWA/Subunit.hpp"         // Need full definition
#include "FWA/AudioPlug.hpp"       // Need full definition
#include "FWA/AudioStreamFormat.hpp" // Need full definition
#include "FWA/AVCInfoBlock.hpp"    // Need full definition
#include "FWA/Enums.hpp"           // Include Enums
#include "FWA/Helpers.h"
#include <IOKit/avc/IOFireWireAVCConsts.h>
#include <spdlog/spdlog.h>
#include <vector>
#include <memory>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <algorithm> // For std::min



namespace FWA {

    // Helper to get the subunit address byte
static uint8_t getSubunitAddress(SubunitType type, uint8_t id) {
    return (static_cast<uint8_t>(type) << 3) | (id & 0x07);
}

//--------------------------------------------------------------------------
// Constants
//--------------------------------------------------------------------------
// We'll use kAVCStatusInquiryCommand from IOFireWireAVCConsts.h.
// For the discovery command, we want the unit plug discovery:
//   [0]: kAVCStatusInquiryCommand
//   [1]: 0xFF (unit plug indicator)
//   [2]: 0x02 (discovery command)
//   [3]: 0x00 (reserved)
//   [4-7]: 0xFF (wildcard)
// Also, we use an initial extended stream format opcode (0xBF) with fallback (0x2F).
constexpr uint8_t kStartingStreamFormatOpcode = 0xBF;
constexpr uint8_t kAlternateStreamFormatOpcode  = 0x2F;

//--------------------------------------------------------------------------
// DeviceParser Constructor
//--------------------------------------------------------------------------

DeviceParser::DeviceParser(AudioDevice* device) // Use raw pointer
    : device_(device),
      info_(device->info_) // Initialize reference to DeviceInfo
{
    if (!device_) {
        throw std::runtime_error("DeviceParser: AudioDevice pointer is null.");
    }
    commandInterface_ = device_->getCommandInterface().get(); // Get raw pointer from shared_ptr
    if (!commandInterface_) {
         throw std::runtime_error("DeviceParser: CommandInterface pointer is null.");
    }
    streamFormatOpcode_ = kStartingStreamFormatOpcode;
    // Note: DeviceInfo now stores its subunits as value members.
}

//--------------------------------------------------------------------------
// DeviceParser::parse()
//--------------------------------------------------------------------------
std::expected<void, IOKitError> DeviceParser::parse() {
    spdlog::info("Parsing device capabilities for GUID: 0x{:x}", device_->getGuid());

    // --- Stage 1: Discover Unit Plugs ---
    spdlog::debug("Stage 1: Discovering Unit Plugs...");
    if (auto result = discoverUnitPlugs(); !result) {
        spdlog::error("FATAL: Failed to discover unit plugs: 0x{:x}", static_cast<int>(result.error()));
        return result;
    }

    // --- Stage 2: Parse Unit Plug Details ---
    spdlog::debug("Stage 2: Parsing Unit Plug Details...");
    if (auto result = parseUnitIsoPlugs(); !result) {
        spdlog::warn("Stage 2 WARNING: Some unit Iso plugs failed: 0x{:x}", static_cast<int>(result.error()));
        // Non-fatal: continue
    }
    if (auto result = parseUnitExternalPlugs(); !result) {
        spdlog::warn("Stage 2 WARNING: Some unit External plugs failed: 0x{:x}", static_cast<int>(result.error()));
        // Non-fatal: continue
    }

    // --- Stage 3a: Discover Subunits (only discover types/IDs) ---
    spdlog::debug("Stage 3a: Discovering Subunits...");
    if (auto result = discoverSubunits(); !result) {
        spdlog::error("FATAL: Failed to discover subunits: 0x{:x}", static_cast<int>(result.error()));
        return result;
    }
    spdlog::debug("Stage 3a: Finished discovering subunits.");

    // --- Stage 3b: Perform Music/Audio Subunit PLUG INFO queries ---
    spdlog::debug("Stage 3b: Querying Subunit Plug Info...");
    if (auto result = querySubunitPlugInfo(); !result) {
        spdlog::warn("Stage 3b WARNING: Failed to query some subunit plug info: 0x{:x}", static_cast<int>(result.error()));
        // Non-fatal: continue
    }

    // --- Stage 4: Fetch and Parse Music Subunit Status Descriptor ---
    spdlog::debug("Stage 4: Fetching and Parsing Music Subunit Descriptor...");
    if (info_.hasMusicSubunit()) {
        if (auto result = fetchMusicSubunitStatusDescriptor(); !result) {
            spdlog::warn("Stage 4 WARNING: Could not fetch Music Subunit Status Descriptor: 0x{:x}", static_cast<int>(result.error()));
        } else {
            if (auto parseResult = parseMusicSubunitStatusDescriptor(result.value()); !parseResult) {
                spdlog::warn("Stage 4 WARNING: Could not parse Music Subunit Status Descriptor: 0x{:x}", static_cast<int>(parseResult.error()));
            } else {
                spdlog::info("Successfully parsed Music Subunit Descriptor Info Blocks.");
            }
        }
    }

    // --- Stage 5: Parse detailed plug information for subunits ---
    spdlog::debug("Stage 5: Parsing detailed plug information for subunits...");
    if (auto result = parseSubunitPlugDetails(); !result) {
        spdlog::warn("Stage 5 WARNING: Failed to parse some subunit plug details: 0x{:x}", static_cast<int>(result.error()));
        // Non-fatal: continue
    }

    spdlog::info("Device capability parsing finished successfully for GUID: 0x{:x}", device_->getGuid());
    return {};
}

//--------------------------------------------------------------------------
// DeviceParser::discoverUnitPlugs()
//--------------------------------------------------------------------------
std::expected<void, IOKitError> DeviceParser::discoverUnitPlugs() {
    spdlog::info("Discovering unit plugs...");

    std::vector<uint8_t> cmd = {
        kAVCStatusInquiryCommand,
        0xFF,
        0x02,
        0x00,
        0xFF, 0xFF, 0xFF, 0xFF
    };

    auto respResult = commandInterface_->sendCommand(cmd);
    if (!respResult) {
        spdlog::error("Failed to send unit plug discovery command.");
        return std::unexpected(respResult.error());
    }
    const auto& response = respResult.value();
    if (response.size() < 8 || (response[0] != kAVCImplementedStatus && response[0] != kAVCAcceptedStatus)) {
        spdlog::error("Discovery command returned unexpected status or insufficient bytes.");
        return std::unexpected(IOKitError(kIOReturnError));
    }

    info_.numIsoInPlugs_  = response[4];
    info_.numIsoOutPlugs_ = response[5];
    info_.numExtInPlugs_  = response[6];
    info_.numExtOutPlugs_ = response[7];

    spdlog::info("Discovered unit plugs: IsoIn = {}, IsoOut = {}, ExtIn = {}, ExtOut = {}",
                 info_.getNumIsoInputPlugs(), info_.getNumIsoOutputPlugs(),
                 info_.getNumExternalInputPlugs(), info_.getNumExternalOutputPlugs());

    return {};
}

//--------------------------------------------------------------------------
// DeviceParser::parseUnitIsoPlugs()
//--------------------------------------------------------------------------
std::expected<void, IOKitError> DeviceParser::parseUnitIsoPlugs() {
    spdlog::debug("Parsing Unit Iso Plugs (In: {}, Out: {})...",
                  info_.getNumIsoInputPlugs(), info_.getNumIsoOutputPlugs());
    
    info_.isoInputPlugs_.clear();
    info_.isoInputPlugs_.reserve(info_.getNumIsoInputPlugs());
    
    for (uint8_t i = 0; i < info_.getNumIsoInputPlugs(); ++i) {
        auto plugResult = parsePlugDetails(kUnitAddress, i, PlugDirection::Input, PlugUsage::Isochronous);
        if (plugResult) {
            info_.isoInputPlugs_.push_back(plugResult.value());
            spdlog::debug("Added Iso Input plug {}: {}",
                i, plugResult.value()->getCurrentStreamFormat() ?
                plugResult.value()->getCurrentStreamFormat()->toString() : "No format");
        } else {
            spdlog::warn("Failed to parse details for Iso In plug {}: 0x{:x}",
                i, static_cast<int>(plugResult.error()));
        }
    }
    
    // Similarly for Iso Output plugs.
    info_.isoOutputPlugs_.clear();
    info_.isoOutputPlugs_.reserve(info_.getNumIsoOutputPlugs());
    
    for (uint8_t i = 0; i < info_.getNumIsoOutputPlugs(); ++i) {
        auto plugResult = parsePlugDetails(kUnitAddress, i, PlugDirection::Output, PlugUsage::Isochronous);
        if (plugResult) {
            info_.isoOutputPlugs_.push_back(plugResult.value());
            spdlog::debug("Added Iso Output plug {}: {}",
                i, plugResult.value()->getCurrentStreamFormat() ?
                plugResult.value()->getCurrentStreamFormat()->toString() : "No format");
        } else {
            spdlog::warn("Failed to parse details for Iso Out plug {}: 0x{:x}",
                i, static_cast<int>(plugResult.error()));
        }
    }
    
    return {};
}

//--------------------------------------------------------------------------
// DeviceParser::parseUnitExternalPlugs()
//--------------------------------------------------------------------------
std::expected<void, IOKitError> DeviceParser::parseUnitExternalPlugs() {
    spdlog::debug("Parsing Unit External Plugs (In: {}, Out: {})...",
                  info_.getNumExternalInputPlugs(), info_.getNumExternalOutputPlugs());
    
    info_.externalInputPlugs_.clear();
    info_.externalInputPlugs_.reserve(info_.getNumExternalInputPlugs());
    
    for (uint8_t i = 0; i < info_.getNumExternalInputPlugs(); ++i) {
        uint8_t plugNum = 0x80 + i;
        auto plugResult = parsePlugDetails(kUnitAddress, plugNum, PlugDirection::Input, PlugUsage::External);
        if (plugResult) {
            info_.externalInputPlugs_.push_back(plugResult.value());
            spdlog::debug("Added External Input plug {}: {}",
                i, plugResult.value()->getCurrentStreamFormat() ?
                plugResult.value()->getCurrentStreamFormat()->toString() : "No format");
        } else {
            spdlog::warn("Failed to parse details for External In plug {}: 0x{:x}",
                i, static_cast<int>(plugResult.error()));
        }
    }
    
    info_.externalOutputPlugs_.clear();
    info_.externalOutputPlugs_.reserve(info_.getNumExternalOutputPlugs());
    
    for (uint8_t i = 0; i < info_.getNumExternalOutputPlugs(); ++i) {
        uint8_t plugNum = 0x80 + i;
        auto plugResult = parsePlugDetails(kUnitAddress, plugNum, PlugDirection::Output, PlugUsage::External);
        if (plugResult) {
            info_.externalOutputPlugs_.push_back(plugResult.value());
            spdlog::debug("Added External Output plug {}: {}",
                i, plugResult.value()->getCurrentStreamFormat() ?
                plugResult.value()->getCurrentStreamFormat()->toString() : "No format");
        } else {
            spdlog::warn("Failed to parse details for External Out plug {}: 0x{:x}",
                i, static_cast<int>(plugResult.error()));
        }
    }
    
    return {};
}

//--------------------------------------------------------------------------
// DeviceParser::parseMusicSubunitDetails()
//--------------------------------------------------------------------------
std::expected<void, IOKitError> DeviceParser::parseMusicSubunitDetails() {
    uint8_t subunitAddr = getSubunitAddress(SubunitType::Music, info_.musicSubunit_.getId());
    spdlog::debug("Parsing Music Subunit (Address 0x{:02x}) details...", subunitAddr);
    std::vector<uint8_t> cmd = {
         kAVCStatusInquiryCommand, 
         subunitAddr, 
         kAVCPlugInfoOpcode, 
         0x01, // Subfunction 1 for subunit plugs
         0xFF, 0xFF, 0xFF, 0xFF
    };

    auto respResult = commandInterface_->sendCommand(cmd);
    if (!respResult) {
        spdlog::error("Failed to retrieve Music Subunit information.");
        return std::unexpected(respResult.error());
    }
    const auto& response = respResult.value();
    if (response.size() < 8 || (response[0] != kAVCImplementedStatus && response[0] != kAVCAcceptedStatus)) { 
        spdlog::error("Invalid Music Subunit response status or size: {} bytes", response.size());
        return std::unexpected(IOKitError(kIOReturnError));
    }

    // Update music subunit plug counts in DeviceInfo (using value member access)
    info_.musicSubunit_.setMusicDestPlugCount(response[4]);
    info_.musicSubunit_.setMusicSourcePlugCount(response[5]);
    spdlog::info("Music Subunit plugs: Dest={}, Source={}",
                 info_.musicSubunit_.getMusicDestPlugCount(), 
                 info_.musicSubunit_.getMusicSourcePlugCount());

    // Parse Destination Plugs
    for (uint8_t i = 0; i < info_.musicSubunit_.getMusicDestPlugCount(); ++i) {
         auto plugResult = parsePlugDetails(subunitAddr, i, PlugDirection::Input, PlugUsage::MusicSubunit);
         if (plugResult) {
             info_.musicSubunit_.addMusicDestPlug(plugResult.value());
         } else {
             spdlog::warn("Failed to parse details for Music Dest plug {}: 0x{:x}", 
                          i, static_cast<int>(plugResult.error()));
         }
    }

    // Parse Source Plugs
    for (uint8_t i = 0; i < info_.musicSubunit_.getMusicSourcePlugCount(); ++i) {
         auto plugResult = parsePlugDetails(subunitAddr, i, PlugDirection::Output, PlugUsage::MusicSubunit);
         if (plugResult) {
             info_.musicSubunit_.addMusicSourcePlug(plugResult.value());
         } else {
             spdlog::warn("Failed to parse details for Music Source plug {}: 0x{:x}",
                          i, static_cast<int>(plugResult.error()));
         }
    }

    return {};
}

//--------------------------------------------------------------------------
// DeviceParser::parseAudioSubunitDetails()
//--------------------------------------------------------------------------
std::expected<void, IOKitError> DeviceParser::parseAudioSubunitDetails() {
    uint8_t subunitAddr = getSubunitAddress(SubunitType::Audio, info_.audioSubunit_.getId());
    spdlog::debug("Parsing Audio Subunit (Address 0x{:02x}) details...", subunitAddr);
    std::vector<uint8_t> cmd = {
         kAVCStatusInquiryCommand, 
         subunitAddr, 
         kAVCPlugInfoOpcode, 
         0x01,
         0xFF, 0xFF, 0xFF, 0xFF
    };

    auto respResult = commandInterface_->sendCommand(cmd);
    if (!respResult) {
         spdlog::error("Failed to retrieve Audio Subunit information.");
         return std::unexpected(respResult.error());
    }
    const auto& response = respResult.value();
    // Check status first
    if (response[0] == kAVCNotImplementedStatus) {
         spdlog::warn("Audio Subunit does not implement PLUG INFO command (Status=0x{:02x}). Cannot determine plug counts.", response[0]);
         // Return success, as not implementing this is allowed, but we can't parse plugs.
         return {}; // Indicate parsing finished, but no plugs found this way.
    } else if (response.size() < 8 || (response[0] != kAVCImplementedStatus && response[0] != kAVCAcceptedStatus)) {
         // Log error for other unexpected statuses or short responses
         spdlog::error("Unexpected Audio Subunit PLUG INFO response status (0x{:02x}) or insufficient size ({} bytes)", response[0], response.size());
         // Return an error indicating a problem with the response
         return std::unexpected(IOKitError(kIOReturnBadResponse));
    }

    // Update audio subunit plug counts in DeviceInfo
    info_.audioSubunit_.setAudioDestPlugCount(response[4]);
    info_.audioSubunit_.setAudioSourcePlugCount(response[5]);
    spdlog::info("Audio Subunit plugs: Dest={}, Source={}",
                 info_.audioSubunit_.getAudioDestPlugCount(), 
                 info_.audioSubunit_.getAudioSourcePlugCount());

    // Parse Destination Plugs
    for (uint8_t i = 0; i < info_.audioSubunit_.getAudioDestPlugCount(); ++i) {
         auto plugResult = parsePlugDetails(subunitAddr, i, PlugDirection::Input, PlugUsage::AudioSubunit);
         if (plugResult) {
             info_.audioSubunit_.addAudioDestPlug(plugResult.value());
         } else {
              spdlog::warn("Failed to parse details for Audio Dest plug {}: 0x{:x}",
                           i, static_cast<int>(plugResult.error()));
         }
    }

    // Parse Source Plugs
    for (uint8_t i = 0; i < info_.audioSubunit_.getAudioSourcePlugCount(); ++i) {
         auto plugResult = parsePlugDetails(subunitAddr, i, PlugDirection::Output, PlugUsage::AudioSubunit);
         if (plugResult) {
             info_.audioSubunit_.addAudioSourcePlug(plugResult.value());
         } else {
             spdlog::warn("Failed to parse details for Audio Source plug {}: 0x{:x}",
                           i, static_cast<int>(plugResult.error()));
         }
    }
    return {};
}

//--------------------------------------------------------------------------
// DeviceParser::parseStreamFormatResponse()
//--------------------------------------------------------------------------
std::expected<AudioStreamFormat, IOKitError> DeviceParser::parseStreamFormatResponse(
    const std::vector<uint8_t>& responseData, uint8_t generatingSubfunction)
{
    size_t headerSize = 0;
    size_t formatStartOffset = 0;
    if (generatingSubfunction == kAVCStreamFormatCurrentQuerySubfunction) {
        headerSize = 10;
        formatStartOffset = 10;
        spdlog::trace("Parsing C0 response, expecting format block at offset 10");
    } else if (generatingSubfunction == kAVCStreamFormatSupportedQuerySubfunction) {
        headerSize = 11;
        formatStartOffset = 11;
        spdlog::trace("Parsing C1 response, expecting format block at offset 11");
    } else {
        spdlog::error("parseStreamFormatResponse called with unknown generating subfunction: 0x{:02x}", generatingSubfunction);
        return std::unexpected(IOKitError(kIOReturnInternalError));
    }
    if (responseData.size() < formatStartOffset) {
         spdlog::error("Response too short ({}) for expected header offset ({})", responseData.size(), formatStartOffset);
         return std::unexpected(IOKitError(kIOReturnUnderrun));
    }
    if (responseData.size() == formatStartOffset) {
          spdlog::warn("Response contains only header, no format block data. Subfunction: 0x{:02x}", generatingSubfunction);
          return std::unexpected(IOKitError(kIOReturnUnderrun));
    }
    const uint8_t* fmt = responseData.data() + formatStartOffset;
    size_t fmtLength = responseData.size() - formatStartOffset;
    spdlog::trace("Calculated format block length: {} bytes", fmtLength);
    if (fmtLength < 2) {
         spdlog::error("Format block too short ({}) to determine type.", fmtLength);
         return std::unexpected(IOKitError(kIOReturnBadResponse));
    }
    if (fmt[0] == 0x90 && fmt[1] == 0x40) {
        if (fmtLength < 5) {
             spdlog::error("Compound AM824 block too short: {} bytes", fmtLength);
             return std::unexpected(IOKitError(kIOReturnUnderrun));
        }
        FormatType fmtType = FormatType::CompoundAM824;
        SampleRate sampleRate = SampleRate::Unknown;
        switch (fmt[2]) {
             case 0x00: sampleRate = SampleRate::SR_22050; break;
             case 0x01: sampleRate = SampleRate::SR_24000; break;
             case 0x02: sampleRate = SampleRate::SR_32000; break;
             case 0x03: sampleRate = SampleRate::SR_44100; break;
             case 0x04: sampleRate = SampleRate::SR_48000; break;
             case 0x05: sampleRate = SampleRate::SR_96000; break;
             case 0x06: sampleRate = SampleRate::SR_176400; break;
             case 0x07: sampleRate = SampleRate::SR_192000; break;
             case 0x0A: sampleRate = SampleRate::SR_88200; break;
             default:   sampleRate = SampleRate::Unknown; break;
         }
        bool sync = (fmt[3] & 0x04) != 0;
        uint8_t numFields = fmt[4];
        std::vector<ChannelFormatInfo> channels;
        size_t requiredLengthInBlock = 5 + (numFields * 2);
        if (fmtLength < requiredLengthInBlock) {
            spdlog::error("Insufficient format info fields: required {} bytes in block, got {}", requiredLengthInBlock, fmtLength);
            return std::unexpected(IOKitError(kIOReturnUnderrun));
        }
        size_t offset = 5;
        for (uint8_t i = 0; i < numFields; ++i) {
             if (offset + 1 >= fmtLength) {
                 spdlog::error("Format block ended unexpectedly while parsing field {}", i);
                 return std::unexpected(IOKitError(kIOReturnUnderrun));
             }
            ChannelFormatInfo cf;
            cf.channelCount = fmt[offset];
            cf.formatCode = static_cast<StreamFormatCode>(fmt[offset + 1]);
            channels.push_back(cf);
            offset += 2;
        }
        AudioStreamFormat format(fmtType, sampleRate, sync, std::move(channels));
        spdlog::info("Parsed compound AM824 stream format:\n{}", format.toString());
        return format;
    } else if (fmt[0] == 0x90 && fmt[1] == 0x00) {
        if (fmtLength == 6) {
            FormatType fmtType = FormatType::AM824;
            SampleRate sampleRate = SampleRate::Unknown;
            switch((fmt[4] & 0xF0) >> 4) {
                case 0x00: sampleRate = SampleRate::SR_22050; break;
                case 0x01: sampleRate = SampleRate::SR_24000; break;
                case 0x02: sampleRate = SampleRate::SR_32000; break;
                case 0x03: sampleRate = SampleRate::SR_44100; break;
                case 0x04: sampleRate = SampleRate::SR_48000; break;
                case 0x05: sampleRate = SampleRate::SR_96000; break;
                case 0x06: sampleRate = SampleRate::SR_176400; break;
                case 0x07: sampleRate = SampleRate::SR_192000; break;
                case 0x0A: sampleRate = SampleRate::SR_88200; break;
                default:   sampleRate = SampleRate::Unknown; break;
            }
            bool sync = false;
            StreamFormatCode formatCode = static_cast<StreamFormatCode>(fmt[2]);
            uint8_t channelCount = 2;
            ChannelFormatInfo cf(channelCount, formatCode);
            AudioStreamFormat format(fmtType, sampleRate, sync, {cf});
            spdlog::info("Parsed 6-byte single-stream AM824 format:\n{}", format.toString());
            return format;
        } else if (fmtLength == 3) {
            spdlog::debug("Handling 3-byte AM824 format (90 00 XX)");
            FormatType fmtType = FormatType::AM824;
            SampleRate sampleRate = SampleRate::DontCare;
            bool sync = false;
            StreamFormatCode formatCode = static_cast<StreamFormatCode>(fmt[2]);
            uint8_t channelCount = 2;
            ChannelFormatInfo cf(channelCount, formatCode);
            AudioStreamFormat format(fmtType, sampleRate, sync, {cf});
            spdlog::info("Parsed 3-byte single-stream AM824 format:\n{}", format.toString());
            return format;
        } else {
            spdlog::error("Unrecognized AM824 (90 00) format block length: {}", fmtLength);
            return std::unexpected(IOKitError(kIOReturnBadResponse));
        }
    } else {
        spdlog::error("Unrecognized format block start: {:02x} {:02x}...", fmt[0], fmt[1]);
        return std::unexpected(IOKitError(kIOReturnBadResponse));
    }
}

//--------------------------------------------------------------------------
// DeviceParser::fetchMusicSubunitStatusDescriptor()
//--------------------------------------------------------------------------
std::expected<std::vector<uint8_t>, IOKitError> DeviceParser::fetchMusicSubunitStatusDescriptor() {
    spdlog::debug("Fetching Music Subunit Status Descriptor...");
    uint8_t subunitAddr = getSubunitAddress(SubunitType::Music, info_.musicSubunit_.getId());
    return readDescriptor(
        subunitAddr,
        DescriptorSpecifierType::UnitSubunitIdentifier,
        {}
    );
}

//--------------------------------------------------------------------------
// DeviceParser::parseMusicSubunitStatusDescriptor()
//--------------------------------------------------------------------------
std::expected<void, IOKitError> DeviceParser::parseMusicSubunitStatusDescriptor(const std::vector<uint8_t>& descriptorData) {
    spdlog::debug("Parsing Music Subunit Identifier Descriptor content, {} bytes received", descriptorData.size());
    info_.musicSubunit_.clearParsedStatusInfoBlocks();
    info_.musicSubunit_.setStatusDescriptorData(descriptorData);
    if (descriptorData.size() < 8) {
        spdlog::warn("Music Subunit descriptor data too short ({}) for basic header fields. Cannot parse info blocks.", descriptorData.size());
        return {};
    }
    uint8_t sizeOfListId = descriptorData[3];
    uint16_t numberOfRootLists = (static_cast<uint16_t>(descriptorData[6]) << 8) | descriptorData[7];
    size_t offsetAfterRootLists = 8 + (numberOfRootLists * sizeOfListId);
    if (descriptorData.size() < offsetAfterRootLists + 2) {
        spdlog::warn("Descriptor data too short ({}) to read subunit_type_dependent_information_length at offset {}.",
                     descriptorData.size(), offsetAfterRootLists);
        return {};
    }
    uint16_t subunitInfoLength = (static_cast<uint16_t>(descriptorData[offsetAfterRootLists]) << 8) | descriptorData[offsetAfterRootLists + 1];
    size_t infoBlockStartOffset = offsetAfterRootLists + 2;
    size_t infoBlockEndOffset = infoBlockStartOffset + subunitInfoLength;
    if (subunitInfoLength == 0) {
         spdlog::debug("Subunit dependent info length is 0. No info blocks to parse.");
         return {};
    }
    if (infoBlockEndOffset > descriptorData.size()) {
        spdlog::error("Subunit dependent info length ({}) indicates end offset ({}) beyond actual descriptor data size ({}). Clamping parse range.",
                     subunitInfoLength, infoBlockEndOffset, descriptorData.size());
        infoBlockEndOffset = descriptorData.size();
    }
    if (infoBlockStartOffset >= infoBlockEndOffset) {
          spdlog::debug("Calculated info block area is empty or invalid (start={}, end={}). No info blocks to parse.", infoBlockStartOffset, infoBlockEndOffset);
          return {};
    }
    spdlog::debug("Parsing info blocks within Subunit Identifier Descriptor from offset {} to {}", infoBlockStartOffset, infoBlockEndOffset);
    size_t currentOffset = infoBlockStartOffset;
    while (currentOffset < infoBlockEndOffset) {
        if (currentOffset + 4 > infoBlockEndOffset) {
             spdlog::warn("Insufficient data remaining ({}) for info block header at offset {}. Stopping info block parse.",
                          infoBlockEndOffset - currentOffset, currentOffset);
             break;
        }
        std::vector<uint8_t> blockDataSlice(descriptorData.begin() + currentOffset,
                                            descriptorData.begin() + infoBlockEndOffset);
        std::shared_ptr<AVCInfoBlock> infoBlock;
        try {
            // Extract type from first two bytes of blockDataSlice
            if (blockDataSlice.size() < 2) {
                spdlog::error("Info block data too short to extract type at offset {}.", currentOffset);
                break;
            }
            uint16_t blockType = (static_cast<uint16_t>(blockDataSlice[0]) << 8) | blockDataSlice[1];
            infoBlock = std::make_shared<AVCInfoBlock>(blockType, std::move(blockDataSlice));
             if (infoBlock->getType() == 0xFFFF && infoBlock->getCompoundLength() == 0 && infoBlock->getRawData().size() < 4) {
                 spdlog::error("Failed to parse info block header at descriptor offset {}. Stopping.", currentOffset);
                 break;
             }
             size_t currentBlockTotalSize = infoBlock->getCompoundLength() + 2;
             if (currentOffset + currentBlockTotalSize > infoBlockEndOffset) {
                  spdlog::error("Parsed info block size ({}) at offset {} would exceed subunit info area end ({}). Corrupted data?",
                                currentBlockTotalSize, currentOffset, infoBlockEndOffset);
                  break;
             }
            info_.musicSubunit_.addParsedStatusInfoBlock(infoBlock);
            spdlog::debug("Added parsed info block (Type: 0x{:04X}, Total Size: {}) from descriptor offset {}",
                         infoBlock->getType(), currentBlockTotalSize, currentOffset);
            currentOffset += currentBlockTotalSize;
        } catch (const std::exception& e) {
            spdlog::error("Exception creating/parsing AVCInfoBlock at descriptor offset {}: {}", currentOffset, e.what());
            break;
        }
    }
    if (currentOffset != infoBlockEndOffset) {
        spdlog::warn("Finished parsing info blocks at offset {}, but expected end of subunit info area was {}.",
                     currentOffset, infoBlockEndOffset);
    } else {
        spdlog::debug("Successfully parsed info blocks up to expected end offset {}.", infoBlockEndOffset);
    }
    return {};
}

//--------------------------------------------------------------------------
// DeviceParser::parsePlugDetails()
//--------------------------------------------------------------------------
std::expected<std::shared_ptr<AudioPlug>, IOKitError> DeviceParser::parsePlugDetails(
    uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage) {
    
    spdlog::debug("Parsing details for plug: subunit=0x{:02x}, num={}, direction={}, usage={}",
                 subunitAddr, plugNum, 
                 (direction == PlugDirection::Input ? "Input" : "Output"),
                 static_cast<int>(usage));
    
    // Create the plug object
    auto plug = std::make_shared<AudioPlug>(subunitAddr, plugNum, direction, usage);
    
    // Query the current stream format for this plug
    auto formatResult = queryPlugStreamFormat(subunitAddr, plugNum, direction, usage);
    if (formatResult) {
         plug->setCurrentStreamFormat(formatResult.value());
         spdlog::debug("Successfully retrieved current stream format for plug");
    } else {
         spdlog::warn("Failed to retrieve current stream format for plug: 0x{:x}",
                      static_cast<int>(formatResult.error()));
    }
    
    // Query *all* supported stream formats
    auto supportedFormatsResult = querySupportedPlugStreamFormats(subunitAddr, plugNum, direction, usage);
    if (supportedFormatsResult) {
         const auto& formats = supportedFormatsResult.value();
         spdlog::debug("Retrieved {} supported format(s) for plug.", formats.size());
         for (const auto& format : formats) {
              plug->addSupportedStreamFormat(format);
              // Optional: Log each supported format added
              // spdlog::debug("  Added supported format: {}", format.toString());
         }
    } else {
        // Log the error, but don't necessarily stop parsing the rest of the device
        spdlog::warn("Failed to retrieve supported stream formats for plug: 0x{:x}",
                     static_cast<int>(supportedFormatsResult.error()));
    }
    // --- End NEW section ---

    // If this is a destination plug, query its signal source
    if (direction == PlugDirection::Input) {
         auto connInfoResult = querySignalSource(subunitAddr, plugNum, direction, usage);
         if (connInfoResult) {
              plug->setConnectionInfo(connInfoResult.value());
              spdlog::debug("Retrieved connection info for destination plug");
         } else {
              spdlog::warn("Failed to query connection info for plug: 0x{:x}", 
                           static_cast<int>(connInfoResult.error()));
         }
    }
    
    return plug;
}

//--------------------------------------------------------------------------
// DeviceParser::queryPlugStreamFormat()
//--------------------------------------------------------------------------
std::expected<AudioStreamFormat, IOKitError> DeviceParser::queryPlugStreamFormat(
    uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage) {
    spdlog::debug("Querying current stream format for plug: subunit=0x{:02x}, num={}, direction={}, usage={}",
                 subunitAddr, plugNum,
                 (direction == PlugDirection::Input ? "Input" : "Output"),
                 static_cast<int>(usage));
    std::vector<uint8_t> cmd = {
        kAVCStatusInquiryCommand,
        subunitAddr,
        streamFormatOpcode_,
        kAVCStreamFormatCurrentQuerySubfunction,
        0, 0, 0, 0, 0,
        0xFF, 0xFF
    };
    cmd.resize(11, 0xFF);
    if (subunitAddr == kUnitAddress) {
        cmd[4] = (direction == PlugDirection::Input) ? 0x00 : 0x01;
        cmd[5] = 0x00;
        cmd[6] = (plugNum < 0x80) ? 0x00 : 0x01;
        cmd[7] = plugNum;
        cmd[8] = 0xFF;
    } else {
        cmd[4] = (direction == PlugDirection::Input) ? 0x00 : 0x01;
        cmd[5] = 0x01;
        cmd[6] = plugNum;
        cmd[7] = 0xFF;
        cmd[8] = 0xFF;
    }
    cmd[2] = streamFormatOpcode_;
    auto respResult = commandInterface_->sendCommand(cmd);
    if (respResult && respResult.value()[0] == kAVCNotImplementedStatus && streamFormatOpcode_ == kStartingStreamFormatOpcode) {
         spdlog::debug("Stream format opcode 0x{:02x} (Current) not implemented, trying alternate 0x{:02x}",
                     streamFormatOpcode_, kAlternateStreamFormatOpcode);
         streamFormatOpcode_ = kAlternateStreamFormatOpcode;
         cmd[2] = streamFormatOpcode_;
         respResult = commandInterface_->sendCommand(cmd);
    }
    if (!respResult) {
         spdlog::warn("Command error when querying current stream format: 0x{:x}",
                      static_cast<int>(respResult.error()));
         return std::unexpected(respResult.error());
    }
    const auto& response = respResult.value();
    if (response[0] == kAVCImplementedStatus || response[0] == kAVCAcceptedStatus) {
         return parseStreamFormatResponse(response, kAVCStreamFormatCurrentQuerySubfunction);
    } else {
         spdlog::warn("Unexpected response status 0x{:02x} when querying current stream format",
                      response[0]);
         if (response[0] == kAVCRejectedStatus) {
            return std::unexpected(IOKitError(kIOReturnNotPermitted));
         }
         return std::unexpected(IOKitError(kIOReturnError));
    }
}

//--------------------------------------------------------------------------
// DeviceParser::querySupportedPlugStreamFormats()
//--------------------------------------------------------------------------
std::expected<std::vector<AudioStreamFormat>, IOKitError> DeviceParser::querySupportedPlugStreamFormats(
    uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage) {

    spdlog::debug("Querying ALL supported stream formats for plug: subunit=0x{:02x}, num={}, direction={}",
                 subunitAddr, plugNum, (direction == PlugDirection::Input ? "Input" : "Output"));

    std::vector<AudioStreamFormat> supportedFormats;
    uint8_t listIndex = 0;
    const uint8_t MAX_FORMAT_INDEX = 16;
    while (true) {
        if (listIndex > MAX_FORMAT_INDEX) {
            spdlog::warn("Reached maximum format index ({}) while querying supported formats. Stopping.", MAX_FORMAT_INDEX);
            break;
        }
        spdlog::debug("Querying supported format at index {}", listIndex);
        std::vector<uint8_t> cmd = {
            kAVCStatusInquiryCommand,
            subunitAddr,
            streamFormatOpcode_,
            kAVCStreamFormatSupportedQuerySubfunction,
            0, 0, 0, 0, 0,
            0xFF,
            listIndex
        };
        if (subunitAddr == kUnitAddress) {
            cmd[4] = (direction == PlugDirection::Input) ? 0x00 : 0x01;
            cmd[5] = 0x00;
            cmd[6] = (plugNum < 0x80) ? 0x00 : 0x01;
            cmd[7] = plugNum;
            cmd[8] = 0xFF;
        } else {
            cmd[4] = (direction == PlugDirection::Input) ? 0x00 : 0x01;
            cmd[5] = 0x01;
            cmd[6] = plugNum;
            cmd[7] = 0xFF;
            cmd[8] = 0xFF;
        }
        cmd[2] = streamFormatOpcode_;
        auto respResult = commandInterface_->sendCommand(cmd);
        bool retried = false;
        if (respResult && respResult.value()[0] == kAVCNotImplementedStatus && streamFormatOpcode_ == kStartingStreamFormatOpcode) {
            spdlog::debug("Stream format opcode 0x{:02x} (Supported List) not implemented, trying alternate 0x{:02x}",
                          streamFormatOpcode_, kAlternateStreamFormatOpcode);
            streamFormatOpcode_ = kAlternateStreamFormatOpcode;
            cmd[2] = streamFormatOpcode_;
            respResult = commandInterface_->sendCommand(cmd);
            retried = true;
        }
        if (!respResult) {
            spdlog::error("Command error querying supported format at index {}: 0x{:x}",
                         listIndex, static_cast<int>(respResult.error()));
            return std::unexpected(respResult.error());
        }
        const auto& response = respResult.value();
        if (response[0] == kAVCImplementedStatus || response[0] == kAVCAcceptedStatus) {
            auto formatResult = parseStreamFormatResponse(response, kAVCStreamFormatSupportedQuerySubfunction);
            if (formatResult) {
                spdlog::debug("Found supported format at index {}:\n{}", listIndex, formatResult.value().toString());
                supportedFormats.push_back(formatResult.value());
                listIndex++;
                continue;
            } else {
                spdlog::warn("Failed to parse successful response for format index {}. Stopping query.", listIndex);
                break;
            }
        } else {
             spdlog::debug("Query for supported format index {} returned status 0x{:02x}{} Assuming end of list.",
                          listIndex, response[0], retried ? " (after retry)." : ".");
             break;
        }
    }
    spdlog::info("Finished querying supported formats. Found {} formats for plug 0x{:02x}/{} {}",
                 supportedFormats.size(), subunitAddr, plugNum, (direction == PlugDirection::Input ? "Input" : "Output"));
    return supportedFormats;
}

//--------------------------------------------------------------------------
// DeviceParser::querySignalSource()
//--------------------------------------------------------------------------
std::expected<AudioPlug::ConnectionInfo, IOKitError> DeviceParser::querySignalSource(
    uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage)
{
    if (direction != PlugDirection::Input) {
        return std::unexpected(IOKitError(kIOReturnBadArgument));
    }
    spdlog::debug("Querying signal source for destination plug: subunit=0x{:02x}, num={}", subunitAddr, plugNum);
    std::vector<uint8_t> cmd = {
        kAVCStatusInquiryCommand,
        subunitAddr,
        kAVCSignalSourceOpcode,
        0xFF,
        plugNum,
        0xFF,
        0xFF,
        0xFF
    };
    spdlog::debug("Querying signal source using SIGNAL SOURCE STATUS (0x1A) for 0x{:02x}/{}", subunitAddr, plugNum);
    auto respResult = commandInterface_->sendCommand(cmd);
    if (!respResult) {
        spdlog::warn("Command error querying signal source for plug 0x{:02x}/{}: 0x{:x}",
                     subunitAddr, plugNum, static_cast<int>(respResult.error()));
        return std::unexpected(respResult.error());
    }
    const auto& response = respResult.value();
    if (response[0] == kAVCNotImplementedStatus) {
         spdlog::warn("Query signal source (0x1A) is NOT IMPLEMENTED for plug 0x{:02x}/{}.", subunitAddr, plugNum);
         return std::unexpected(IOKitError(kIOReturnUnsupported));
    }
    if (response[0] == kAVCRejectedStatus) {
         spdlog::warn("Query signal source (0x1A) was REJECTED for plug 0x{:02x}/{}.", subunitAddr, plugNum);
         return std::unexpected(IOKitError(kIOReturnNotPermitted));
    }
    if (response.size() < 8 || (response[0] != kAVCImplementedStatus && response[0] != kAVCAcceptedStatus)) {
         spdlog::warn("Invalid signal source response status: 0x{:02x} or size: {} for plug 0x{:02x}/{}",
                     static_cast<int>(response[0]), response.size(), subunitAddr, plugNum);
         return std::unexpected(IOKitError(kIOReturnBadResponse));
    }
    AudioPlug::ConnectionInfo connInfo;
    connInfo.sourceSubUnit = response[5];
    connInfo.sourcePlugNum = response[6];
    connInfo.sourcePlugStatus = response[7];
    if (connInfo.sourcePlugNum == 0xFE) {
        spdlog::debug("Plug 0x{:02x}/{} is not connected (source is Invalid FE).", subunitAddr, plugNum);
    } else {
        spdlog::debug("Plug 0x{:02x}/{} is connected to subunit 0x{:02x}, plug {} (status=0x{:02x})",
                    subunitAddr, plugNum, connInfo.sourceSubUnit, connInfo.sourcePlugNum, connInfo.sourcePlugStatus);
    }
    return connInfo;
}

//--------------------------------------------------------------------------
// DeviceParser::readDescriptor()
//--------------------------------------------------------------------------
std::expected<std::vector<uint8_t>, IOKitError> DeviceParser::readDescriptor(
    uint8_t subunitAddr,
    DescriptorSpecifierType descriptorSpecifierType,
    const std::vector<uint8_t>& descriptorSpecifierSpecificData)
{
    spdlog::debug("Reading descriptor: Subunit=0x{:02x}, SpecifierType=0x{:02x}",
                  subunitAddr, static_cast<uint8_t>(descriptorSpecifierType));

    // --- Build Specifiers ---
    std::vector<uint8_t> openSpecifierBytes;
    std::vector<uint8_t> readSpecifierBytes;
    std::vector<uint8_t> closeSpecifierBytes;

    if (descriptorSpecifierType == DescriptorSpecifierType::UnitSubunitIdentifier) {
        openSpecifierBytes = DescriptorUtils::buildDescriptorSpecifier(
            descriptorSpecifierType,
            DescriptorUtils::DEFAULT_SIZE_OF_LIST_ID,
            DescriptorUtils::DEFAULT_SIZE_OF_OBJECT_ID,
            DescriptorUtils::DEFAULT_SIZE_OF_ENTRY_POS
        );
        readSpecifierBytes = openSpecifierBytes;
        closeSpecifierBytes = openSpecifierBytes;
    } else {
        spdlog::error("readDescriptor: Building descriptor specifier for type 0x{:02x} not fully implemented.",
                      static_cast<uint8_t>(descriptorSpecifierType));
        return std::unexpected(IOKitError(kIOReturnUnsupported));
    }
    if (openSpecifierBytes.empty() || readSpecifierBytes.empty() || closeSpecifierBytes.empty()) {
        spdlog::error("readDescriptor: Failed to build necessary descriptor specifiers.");
        return std::unexpected(IOKitError(kIOReturnInternalError));
    }

    // --- 1. OPEN DESCRIPTOR ---
    std::vector<uint8_t> openCmd;
    openCmd.push_back(kAVCControlCommand);
    openCmd.push_back(subunitAddr);
    openCmd.push_back(kAVCOpenDescriptorOpcode);
    openCmd.insert(openCmd.end(), openSpecifierBytes.begin(), openSpecifierBytes.end());
    openCmd.push_back(0x01); // Subfunction: Read Open
    spdlog::info("Sending command: {:02x} {:02x} ...", openCmd[0], openCmd[1]);
    auto openRespResult = commandInterface_->sendCommand(openCmd);
    if (!openRespResult || openRespResult.value().empty() ||
        (openRespResult.value()[0] != kAVCAcceptedStatus && openRespResult.value()[0] != kAVCImplementedStatus)) {
        spdlog::error("readDescriptor: OPEN DESCRIPTOR failed. Status=0x{:02x}",
                      openRespResult ? static_cast<int>(openRespResult.value()[0]) : -1);
        return std::unexpected(openRespResult ? IOKitError(kIOReturnError) : openRespResult.error());
    }
    spdlog::debug("readDescriptor: OPEN DESCRIPTOR successful.");

    // --- 2. READ DESCRIPTOR (with Pagination) ---
    std::vector<uint8_t> descriptorData;
    uint32_t currentOffset = 0;
    bool readComplete = false;
    IOReturn lastReadStatus = kIOReturnSuccess;
    constexpr uint16_t MAX_READ_CHUNK = 490;
    int readAttempts = 0;
    const int MAX_READ_ATTEMPTS = 256;

    do {
        readAttempts++;
        if (readAttempts > MAX_READ_ATTEMPTS) {
            spdlog::error("readDescriptor: Exceeded maximum read attempts ({}), aborting.", MAX_READ_ATTEMPTS);
            lastReadStatus = kIOReturnOverrun;
            break;
        }
        std::vector<uint8_t> readCmd;
        readCmd.push_back(kAVCStatusInquiryCommand);
        readCmd.push_back(subunitAddr);
        readCmd.push_back(kAVCReadDescriptorOpcode);
        readCmd.insert(readCmd.end(), readSpecifierBytes.begin(), readSpecifierBytes.end());
        readCmd.push_back(0xFF); // read_result_status = FF
        uint16_t readChunkSize = (currentOffset == 0 && descriptorData.empty()) ? 0 : MAX_READ_CHUNK;
        readCmd.push_back(static_cast<uint8_t>(readChunkSize >> 8));
        readCmd.push_back(static_cast<uint8_t>(readChunkSize & 0xFF));
        readCmd.push_back(static_cast<uint8_t>(currentOffset >> 8));
        readCmd.push_back(static_cast<uint8_t>(currentOffset & 0xFF));
        auto readRespResult = commandInterface_->sendCommand(readCmd);
        if (!readRespResult || readRespResult.value().empty()) {
            spdlog::error("readDescriptor: READ DESCRIPTOR command failed at offset {}: 0x{:x}",
                          currentOffset, readRespResult ? kIOReturnError : static_cast<int>(readRespResult.error()));
            lastReadStatus = readRespResult ? kIOReturnError : static_cast<int>(readRespResult.error());
            break;
        }
        const auto& readResponse = readRespResult.value();
        if (readResponse.size() < 8) {
            spdlog::error("readDescriptor: READ DESCRIPTOR response too short ({}) at offset {}", readResponse.size(), currentOffset);
            lastReadStatus = kIOReturnUnderrun;
            break;
        }
        ParsedDescriptorSpecifier parsedSpecifier = DescriptorUtils::parseDescriptorSpecifier(
            readResponse.data() + 2, readResponse.size() - 2,
            DescriptorUtils::DEFAULT_SIZE_OF_LIST_ID,
            DescriptorUtils::DEFAULT_SIZE_OF_OBJECT_ID,
            DescriptorUtils::DEFAULT_SIZE_OF_ENTRY_POS
        );
        if (parsedSpecifier.consumedSize == 0) {
            spdlog::error("readDescriptor: Failed to parse returned descriptor specifier in response header at offset {}", currentOffset);
            lastReadStatus = kIOReturnBadResponse;
            break;
        }
        size_t responseHeaderSize = 2 + parsedSpecifier.consumedSize + 1 + 2 + 2;
        if (readResponse.size() < responseHeaderSize) {
            spdlog::error("readDescriptor: READ DESCRIPTOR response too short ({}) for calculated header size ({}) at offset {}",
                         readResponse.size(), responseHeaderSize, currentOffset);
            lastReadStatus = kIOReturnUnderrun;
            break;
        }
        uint8_t readResultStatus = readResponse[2 + parsedSpecifier.consumedSize];
        uint16_t bytesReadInChunk = static_cast<uint16_t>((readResponse[2 + parsedSpecifier.consumedSize + 1] << 8) |
                                                    readResponse[2 + parsedSpecifier.consumedSize + 2]);
        if (bytesReadInChunk > 0) {
            if (readResponse.size() < responseHeaderSize + bytesReadInChunk) {
                spdlog::error("readDescriptor: READ response data length mismatch. Claimed {}, Available {}.", bytesReadInChunk, readResponse.size() - responseHeaderSize);
                lastReadStatus = kIOReturnUnderrun;
                break;
            }
            if (descriptorData.empty() || readResultStatus == 0x11 || readResultStatus == 0x12) {
                descriptorData.insert(descriptorData.end(),
                                     readResponse.begin() + responseHeaderSize,
                                     readResponse.begin() + responseHeaderSize + bytesReadInChunk);
                currentOffset += bytesReadInChunk;
            } else if (readResultStatus == 0x10 && !descriptorData.empty()) {
                spdlog::debug("readDescriptor: Received 'Complete Read' (0x10) on subsequent read, assuming end.");
            }
        }
        if (readResultStatus == 0x10) {
            readComplete = true;
        } else if (readResultStatus == 0x11) {
            readComplete = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } else if (readResultStatus == 0x12) {
            readComplete = true;
            spdlog::debug("readDescriptor: Read past end of descriptor (Status 0x12). Read completed.");
        } else {
            spdlog::error("readDescriptor: Received error status 0x{:02x} during read at offset {}", readResultStatus, currentOffset);
            lastReadStatus = kIOReturnIOError;
            break;
        }
    } while (!readComplete);

close_descriptor: // Label for cleanup
    // --- 3. CLOSE DESCRIPTOR ---
    std::vector<uint8_t> closeCmd;
    closeCmd.push_back(kAVCControlCommand);
    closeCmd.push_back(subunitAddr);
    closeCmd.push_back(kAVCOpenDescriptorOpcode);
    closeCmd.insert(closeCmd.end(), closeSpecifierBytes.begin(), closeSpecifierBytes.end()); // Specifier data
    closeCmd.push_back(0x00); // Subfunction: Close
    spdlog::debug("Sending CLOSE command: {}", Helpers::formatHexBytes(closeCmd));
    auto closeRespResult = commandInterface_->sendCommand(closeCmd);
    if (!closeRespResult || closeRespResult.value().empty() ||
         (closeRespResult.value()[0] != kAVCAcceptedStatus && closeRespResult.value()[0] != kAVCImplementedStatus)) {
        spdlog::warn("readDescriptor: CLOSE DESCRIPTOR failed or returned unexpected status: 0x{:02x}",
                     closeRespResult ? static_cast<int>(closeRespResult.value()[0]) : -1);
    } else {
        spdlog::debug("readDescriptor: CLOSE DESCRIPTOR successful.");
    }
    if (lastReadStatus == kIOReturnSuccess) {
        spdlog::info("Successfully read descriptor (Specifier Type 0x{:02x}), size {} bytes",
                     static_cast<uint8_t>(descriptorSpecifierType), descriptorData.size());
        return descriptorData;
    } else {
        spdlog::error("Failed to read descriptor (Specifier Type 0x{:02x}), final status 0x{:x}",
                     static_cast<uint8_t>(descriptorSpecifierType), lastReadStatus);
        return std::unexpected(IOKitError(lastReadStatus));
    }
}

//--------------------------------------------------------------------------
// DeviceParser::sendStreamFormatCommand()
//--------------------------------------------------------------------------
std::expected<std::vector<uint8_t>, IOKitError> DeviceParser::sendStreamFormatCommand(std::vector<uint8_t>& command) {
    spdlog::debug("Sending stream format command, {} bytes", command.size());
    if (command.size() < 3) {
         spdlog::error("Invalid stream format command size: {} bytes", command.size());
         return std::unexpected(IOKitError(kIOReturnBadArgument));
    }
    command[2] = streamFormatOpcode_; // Always use current member opcode
    auto respResult = commandInterface_->sendCommand(command);
    return respResult;
}

//--------------------------------------------------------------------------
// DeviceParser::discoverSubunits()
//--------------------------------------------------------------------------
std::expected<void, IOKitError> DeviceParser::discoverSubunits() {
    spdlog::debug("Discovering subunits (types and IDs only)...");
    std::vector<uint8_t> cmd = {
        kAVCStatusInquiryCommand,
        0xFF, // Unit Address
        kAVCSubunitInfoOpcode, // 0x31
        0x07, // Page 7 (Maximum configuration)
        0xFF, 0xFF, 0xFF, 0xFF // Wildcards for response operands
    };
    auto respResult = commandInterface_->sendCommand(cmd);
    if (!respResult) {
        spdlog::error("Failed to send subunit info command.");
        return std::unexpected(respResult.error());
    }
    const auto& response = respResult.value();
    if (response.size() < 5 || (response[0] != kAVCImplementedStatus && response[0] != kAVCAcceptedStatus)) {
        spdlog::error("Subunit info command (page 7) returned unexpected status (0x{:02x}) or insufficient bytes ({}).", response.size() >=1 ? response[0] : 0xFF, response.size());
        return std::unexpected(IOKitError(kIOReturnError));
    }
    info_.hasMusicSubunit_ = false;
    info_.hasAudioSubunit_ = false;
    spdlog::debug("Parsing SUBUNIT INFO (Page 7) response:");
    for (size_t i = 4; i < response.size(); ++i) {
        uint8_t typeIdByte = response[i];
        if (typeIdByte == 0xFF) {
            spdlog::debug("Found end marker (FF) at index {}", i);
            break;
        }
        uint8_t subunitType = (typeIdByte >> 3);
        uint8_t subunitID = (typeIdByte & 0x07);
        spdlog::debug("  Found Subunit Entry: Type=0x{:02x}, ID={} (from byte 0x{:02x} at index {})",
                      subunitType, subunitID, typeIdByte, i);
        switch (static_cast<SubunitType>(subunitType)) {
            case SubunitType::Music:
                spdlog::debug("  Found Music Subunit (ID: {})", subunitID);
                if (!info_.hasMusicSubunit_) {
                    info_.hasMusicSubunit_ = true;
                    info_.musicSubunit_.setId(subunitID);
                    spdlog::info("Device has Music Subunit (ID: {})", subunitID);
                } else {
                    spdlog::warn("Ignoring additional Music Subunit found (Type=0x{:02x}, ID={}). Only one is processed.", subunitType, subunitID);
                }
                break;
            case SubunitType::Audio:
                spdlog::debug("  Found Audio Subunit (ID: {})", subunitID);
                if (!info_.hasAudioSubunit_) {
                    info_.hasAudioSubunit_ = true;
                    info_.audioSubunit_.setId(subunitID);
                    spdlog::info("Device has Audio Subunit (ID: {})", subunitID);
                } else {
                    spdlog::warn("Ignoring additional Audio Subunit found (Type=0x{:02x}, ID={}). Only one is processed.", subunitType, subunitID);
                }
                break;
            default:
                spdlog::debug("  Ignoring unknown or unsupported subunit type 0x{:02x}", subunitType);
                break;
        }
    }
    if (!info_.hasMusicSubunit_ && !info_.hasAudioSubunit_) {
        spdlog::info("No Music or Audio subunits detected in the SUBUNIT INFO response.");
    } else {
        if(info_.hasMusicSubunit_) spdlog::info("Confirmed Music Subunit presence.");
        if(info_.hasAudioSubunit_) spdlog::info("Confirmed Audio Subunit presence.");
    }
    return {};
}

//--------------------------------------------------------------------------
// DeviceParser::querySubunitPlugInfo()
//--------------------------------------------------------------------------
std::expected<void, IOKitError> DeviceParser::querySubunitPlugInfo() {
    spdlog::debug("Querying plug information for discovered subunits...");
    
    if (info_.hasMusicSubunit()) {
        spdlog::debug("Querying Music Subunit Plug Info...");
        uint8_t subunitAddr = getSubunitAddress(SubunitType::Music, info_.musicSubunit_.getId());
        std::vector<uint8_t> music_plug_cmd = {
            kAVCStatusInquiryCommand, 
            subunitAddr, 
            kAVCPlugInfoOpcode, 
            0x01, // Subfunction 1 for subunit plugs
            0xFF, 0xFF, 0xFF, 0xFF
        };
        
        auto musicPlugResp = commandInterface_->sendCommand(music_plug_cmd);
        if (musicPlugResp && (musicPlugResp.value()[0] == kAVCImplementedStatus || musicPlugResp.value()[0] == kAVCAcceptedStatus)) {
            info_.musicSubunit_.setMusicDestPlugCount(musicPlugResp.value()[4]);
            info_.musicSubunit_.setMusicSourcePlugCount(musicPlugResp.value()[5]);
            spdlog::info("Music Subunit plugs: Dest={}, Source={}",
                         info_.musicSubunit_.getMusicDestPlugCount(), 
                         info_.musicSubunit_.getMusicSourcePlugCount());
            
            // Query Signal Source for Music Dest Plugs (Mimic Apple's order)
            for (uint8_t i = 0; i < info_.musicSubunit_.getMusicDestPlugCount(); ++i) {
                auto connResult = querySignalSource(subunitAddr, i, PlugDirection::Input, PlugUsage::MusicSubunit);
                if (!connResult) {
                    spdlog::warn("Failed to query signal source for Music Dest plug {}: 0x{:x}", 
                                 i, static_cast<int>(connResult.error()));
                }
                // Ignore result for now, just sending the command might be enough
            }
        } else {
            spdlog::warn("Failed to get Music Subunit plug info.");
        }
    }
    
    if (info_.hasAudioSubunit()) {
        spdlog::debug("Querying Audio Subunit Plug Info...");
        uint8_t subunitAddr = getSubunitAddress(SubunitType::Audio, info_.audioSubunit_.getId());
        std::vector<uint8_t> audio_plug_cmd = {
            kAVCStatusInquiryCommand, 
            subunitAddr, 
            kAVCPlugInfoOpcode, 
            0x01, // Subfunction 1 for subunit plugs
            0xFF, 0xFF, 0xFF, 0xFF
        };
        
        auto audioPlugResp = commandInterface_->sendCommand(audio_plug_cmd);
        if (audioPlugResp) {
            if (audioPlugResp.value()[0] == kAVCNotImplementedStatus) {
                spdlog::warn("Audio Subunit does not implement PLUG INFO command (Status=0x{:02x}). Cannot determine plug counts.", audioPlugResp.value()[0]);
            } else if (audioPlugResp.value()[0] == kAVCImplementedStatus || audioPlugResp.value()[0] == kAVCAcceptedStatus) {
                info_.audioSubunit_.setAudioDestPlugCount(audioPlugResp.value()[4]);
                info_.audioSubunit_.setAudioSourcePlugCount(audioPlugResp.value()[5]);
                spdlog::info("Audio Subunit plugs: Dest={}, Source={}",
                            info_.audioSubunit_.getAudioDestPlugCount(), 
                            info_.audioSubunit_.getAudioSourcePlugCount());
                
                // Query Signal Source for Audio Dest Plugs
                for (uint8_t i = 0; i < info_.audioSubunit_.getAudioDestPlugCount(); ++i) {
                    auto connResult = querySignalSource(subunitAddr, i, PlugDirection::Input, PlugUsage::AudioSubunit);
                    if (!connResult) {
                        spdlog::warn("Failed to query signal source for Audio Dest plug {}: 0x{:x}", 
                                    i, static_cast<int>(connResult.error()));
                    }
                }
            } else {
                spdlog::warn("Unexpected Audio Subunit PLUG INFO response status: 0x{:02x}", audioPlugResp.value()[0]);
            }
        } else {
            spdlog::warn("Failed to send Audio Subunit PLUG INFO command: 0x{:x}", static_cast<int>(audioPlugResp.error()));
        }
    }
    
    return {};
}

//--------------------------------------------------------------------------
// DeviceParser::parseSubunitPlugDetails()
//--------------------------------------------------------------------------
std::expected<void, IOKitError> DeviceParser::parseSubunitPlugDetails() {
    spdlog::debug("Parsing plug details for all subunits...");
    
    if (info_.hasMusicSubunit()) {
        uint8_t subunitAddr = getSubunitAddress(SubunitType::Music, info_.musicSubunit_.getId());
        spdlog::debug("Parsing Music Subunit (Address 0x{:02x}) plug details...", subunitAddr);
        
        // Clear existing plugs before parsing (optional)
        info_.musicSubunit_.clearMusicSourcePlugs();
        info_.musicSubunit_.clearMusicDestPlugs();
        
        // Parse Destination Plugs
        for (uint8_t i = 0; i < info_.musicSubunit_.getMusicDestPlugCount(); ++i) {
            auto plugResult = parsePlugDetails(subunitAddr, i, PlugDirection::Input, PlugUsage::MusicSubunit);
            if (plugResult) {
                info_.musicSubunit_.addMusicDestPlug(plugResult.value());
                spdlog::debug("Added Music Dest plug {}: {}",
                            i, plugResult.value()->getCurrentStreamFormat() ?
                            plugResult.value()->getCurrentStreamFormat()->toString() : "No format");
            } else {
                spdlog::warn("Failed to parse details for Music Dest plug {}: 0x{:x}", 
                            i, static_cast<int>(plugResult.error()));
            }
        }

        // Parse Source Plugs
        for (uint8_t i = 0; i < info_.musicSubunit_.getMusicSourcePlugCount(); ++i) {
            auto plugResult = parsePlugDetails(subunitAddr, i, PlugDirection::Output, PlugUsage::MusicSubunit);
            if (plugResult) {
                info_.musicSubunit_.addMusicSourcePlug(plugResult.value());
                spdlog::debug("Added Music Source plug {}: {}",
                            i, plugResult.value()->getCurrentStreamFormat() ?
                            plugResult.value()->getCurrentStreamFormat()->toString() : "No format");
            } else {
                spdlog::warn("Failed to parse details for Music Source plug {}: 0x{:x}",
                            i, static_cast<int>(plugResult.error()));
            }
        }
    }
    
    if (info_.hasAudioSubunit()) {
        uint8_t subunitAddr = getSubunitAddress(SubunitType::Audio, info_.audioSubunit_.getId());
        spdlog::debug("Parsing Audio Subunit (Address 0x{:02x}) plug details...", subunitAddr);
        
        // Clear existing plugs before parsing
        info_.audioSubunit_.clearAudioSourcePlugs();
        info_.audioSubunit_.clearAudioDestPlugs();
        
        // Parse Destination Plugs
        for (uint8_t i = 0; i < info_.audioSubunit_.getAudioDestPlugCount(); ++i) {
            auto plugResult = parsePlugDetails(subunitAddr, i, PlugDirection::Input, PlugUsage::AudioSubunit);
            if (plugResult) {
                info_.audioSubunit_.addAudioDestPlug(plugResult.value());
                spdlog::debug("Added Audio Dest plug {}: {}",
                            i, plugResult.value()->getCurrentStreamFormat() ?
                            plugResult.value()->getCurrentStreamFormat()->toString() : "No format");
            } else {
                spdlog::warn("Failed to parse details for Audio Dest plug {}: 0x{:x}",
                            i, static_cast<int>(plugResult.error()));
            }
        }

        // Parse Source Plugs
        for (uint8_t i = 0; i < info_.audioSubunit_.getAudioSourcePlugCount(); ++i) {
            auto plugResult = parsePlugDetails(subunitAddr, i, PlugDirection::Output, PlugUsage::AudioSubunit);
            if (plugResult) {
                info_.audioSubunit_.addAudioSourcePlug(plugResult.value());
                spdlog::debug("Added Audio Source plug {}: {}",
                            i, plugResult.value()->getCurrentStreamFormat() ?
                            plugResult.value()->getCurrentStreamFormat()->toString() : "No format");
            } else {
                spdlog::warn("Failed to parse details for Audio Source plug {}: 0x{:x}",
                            i, static_cast<int>(plugResult.error()));
            }
        }
    }
    
    return {};
}


} // namespace FWA