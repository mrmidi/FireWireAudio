#include "FWA/DeviceParser.hpp"
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
        spdlog::error("Stage 1 FAILED: Could not discover unit plugs: 0x{:x}",
                      static_cast<int>(result.error()));
        return result;
    }

    // --- Stage 2: Parse Unit Plug Details ---
    spdlog::debug("Stage 2: Parsing Unit Plug Details...");
    if (auto result = parseUnitIsoPlugs(); !result) {
         spdlog::error("Stage 2 FAILED: Could not parse unit Iso plugs: 0x{:x}",
                      static_cast<int>(result.error()));
         return result;
    }
    if (auto result = parseUnitExternalPlugs(); !result) {
         spdlog::error("Stage 2 FAILED: Could not parse unit External plugs: 0x{:x}",
                      static_cast<int>(result.error()));
         return result;
    }

    // --- Stage 3: Discover and Parse Subunits ---
    spdlog::debug("Stage 3: Discovering and Parsing Subunits...");
    if (auto result = discoverAndParseSubunits(); !result) {
         spdlog::error("Stage 3 FAILED: Could not discover or parse subunits: 0x{:x}",
                      static_cast<int>(result.error()));
         return result;
    }

    // --- Stage 4: Fetch and Parse Music Subunit Descriptor (if present) ---
    if (info_.hasMusicSubunit()) { // Use the flag in DeviceInfo
        spdlog::debug("Stage 4: Fetching and Parsing Music Subunit Status Descriptor...");
        if (auto result = fetchMusicSubunitStatusDescriptor(); !result) {
            spdlog::warn("Stage 4 WARNING: Could not fetch Music Subunit Status Descriptor: 0x{:x}",
                         static_cast<int>(result.error()));
        } else {
            // Parse the descriptor if we successfully fetched it
            if (auto parseResult = parseMusicSubunitStatusDescriptor(result.value()); !parseResult) {
                spdlog::warn("Stage 4 WARNING: Could not parse Music Subunit Status Descriptor: 0x{:x}",
                             static_cast<int>(parseResult.error()));
            }
        }
    }

    // --- Stage 5: Audio Subunit Specific Parsing (Deferred) ---
    if (info_.hasAudioSubunit()) { // Use the flag in DeviceInfo
         spdlog::debug("Stage 5: Audio Subunit detailed parsing deferred.");
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

    info_.numIsoInputPlugs_ = response[4];
    info_.numIsoOutputPlugs_ = response[5];
    info_.numExternalInputPlugs_ = response[6];
    info_.numExternalOutputPlugs_ = response[7];

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
    spdlog::debug("Parsing Music Subunit (Type 0x{:02x}) details...", static_cast<uint8_t>(SubunitType::Music));
    std::vector<uint8_t> cmd = {
         kAVCStatusInquiryCommand, 
         static_cast<uint8_t>(SubunitType::Music), 
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
         auto plugResult = parsePlugDetails(static_cast<uint8_t>(SubunitType::Music), i, PlugDirection::Input, PlugUsage::MusicSubunit);
         if (plugResult) {
             info_.musicSubunit_.addMusicDestPlug(plugResult.value());
         } else {
             spdlog::warn("Failed to parse details for Music Dest plug {}: 0x{:x}", 
                          i, static_cast<int>(plugResult.error()));
         }
    }

    // Parse Source Plugs
    for (uint8_t i = 0; i < info_.musicSubunit_.getMusicSourcePlugCount(); ++i) {
         auto plugResult = parsePlugDetails(static_cast<uint8_t>(SubunitType::Music), i, PlugDirection::Output, PlugUsage::MusicSubunit);
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
    spdlog::debug("Parsing Audio Subunit (Type 0x{:02x}) details...", static_cast<uint8_t>(SubunitType::Audio));
    std::vector<uint8_t> cmd = {
         kAVCStatusInquiryCommand, 
         static_cast<uint8_t>(SubunitType::Audio), 
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
    if (response.size() < 8 || (response[0] != kAVCImplementedStatus && response[0] != kAVCAcceptedStatus)) {
         spdlog::error("Invalid Audio Subunit response status or size: {} bytes", response.size());
         return std::unexpected(IOKitError(kIOReturnError));
    }

    // Update audio subunit plug counts in DeviceInfo
    info_.audioSubunit_.setAudioDestPlugCount(response[4]);
    info_.audioSubunit_.setAudioSourcePlugCount(response[5]);
    spdlog::info("Audio Subunit plugs: Dest={}, Source={}",
                 info_.audioSubunit_.getAudioDestPlugCount(), 
                 info_.audioSubunit_.getAudioSourcePlugCount());

    // Parse Destination Plugs
    for (uint8_t i = 0; i < info_.audioSubunit_.getAudioDestPlugCount(); ++i) {
         auto plugResult = parsePlugDetails(static_cast<uint8_t>(SubunitType::Audio), i, PlugDirection::Input, PlugUsage::AudioSubunit);
         if (plugResult) {
             info_.audioSubunit_.addAudioDestPlug(plugResult.value());
         } else {
              spdlog::warn("Failed to parse details for Audio Dest plug {}: 0x{:x}",
                           i, static_cast<int>(plugResult.error()));
         }
    }

    // Parse Source Plugs
    for (uint8_t i = 0; i < info_.audioSubunit_.getAudioSourcePlugCount(); ++i) {
         auto plugResult = parsePlugDetails(static_cast<uint8_t>(SubunitType::Audio), i, PlugDirection::Output, PlugUsage::AudioSubunit);
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
std::expected<AudioStreamFormat, IOKitError> DeviceParser::parseStreamFormatResponse(const std::vector<uint8_t>& responseData) {
    // Assume the first 10 bytes are a header; the format block starts at offset 10.
    if (responseData.size() < 10) {
         spdlog::error("Response too short: {} bytes", responseData.size());
         return std::unexpected(IOKitError(kIOReturnUnderrun));
    }
    size_t headerSize = 10;
    size_t fmtLength = responseData.size() - headerSize;
    if (fmtLength < 7) {
         spdlog::error("Stream format block too short: {} bytes", fmtLength);
         return std::unexpected(IOKitError(kIOReturnUnderrun));
    }
    const uint8_t* fmt = responseData.data() + headerSize;

    // Determine format type using bytes 0 and 1.
    FormatType fmtType = FormatType::Unknown;
    if (fmt[0] == 0x90 && fmt[1] == 0x40)
         fmtType = FormatType::CompoundAM824;
    else if (fmt[0] == 0x90 && fmt[1] == 0x00)
         fmtType = FormatType::AM824;

    // Map sample rate from fmt[2] (adjust mapping as needed).
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

    // Sync flag from fmt[3]: if bit 0x04 is set, then sync source.
    bool sync = (fmt[3] & 0x04) != 0;

    // Number of format info fields is in fmt[4].
    uint8_t numFields = fmt[4];
    std::vector<ChannelFormatInfo> channels;
    size_t requiredLength = 5 + (numFields * 2);
    if (fmtLength < requiredLength) {
         spdlog::error("Insufficient format info fields: required {} bytes, got {}", requiredLength, fmtLength);
         return std::unexpected(IOKitError(kIOReturnUnderrun));
    }
    size_t offset = 5;
    for (uint8_t i = 0; i < numFields; ++i) {
         ChannelFormatInfo cf;
         cf.channelCount = fmt[offset];
         cf.formatCode = static_cast<StreamFormatCode>(fmt[offset + 1]);
         channels.push_back(cf);
         offset += 2;
    }

    AudioStreamFormat format(fmtType, sampleRate, sync, channels);
    spdlog::info("Parsed stream format:\n{}", format.toString());
    return format;
}

//--------------------------------------------------------------------------
// DeviceParser::fetchMusicSubunitStatusDescriptor()
//--------------------------------------------------------------------------
std::expected<std::vector<uint8_t>, IOKitError> DeviceParser::fetchMusicSubunitStatusDescriptor() {
    spdlog::debug("Fetching Music Subunit Status Descriptor...");
    
    // This is just the descriptor type for the Music Subunit General Status Descriptor
    return readDescriptor(static_cast<uint8_t>(SubunitType::Music), 
                          static_cast<uint8_t>(InfoBlockType::GeneralMusicStatus));
}

//--------------------------------------------------------------------------
// DeviceParser::parseMusicSubunitStatusDescriptor()
//--------------------------------------------------------------------------
std::expected<void, IOKitError> DeviceParser::parseMusicSubunitStatusDescriptor(const std::vector<uint8_t>& descriptorData) {
    spdlog::debug("Parsing Music Subunit Status Descriptor, {} bytes", descriptorData.size());
    
    if (descriptorData.size() < 8) {
         spdlog::error("Music Subunit status descriptor too short: {} bytes", descriptorData.size());
         return std::unexpected(IOKitError(kIOReturnUnderrun));
    }
    
    // Store the raw descriptor data in the music subunit
    info_.musicSubunit_.setStatusDescriptorData(descriptorData);
    
    // (Additional parsing of fields could be added here.)
    
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
    
    // Query supported stream formats (if device supports this capability)
    if (auto supportedFormatsResult = querySupportedPlugStreamFormats(subunitAddr, plugNum, direction, usage); 
        supportedFormatsResult) {
         for (const auto& format : supportedFormatsResult.value()) {
              plug->addSupportedStreamFormat(format);
         }
         spdlog::debug("Retrieved {} supported format(s) for plug", supportedFormatsResult.value().size());
    }
    
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
    
    spdlog::debug("Querying stream format for plug: subunit=0x{:02x}, num={}, direction={}, usage={}",
                 subunitAddr, plugNum, 
                 (direction == PlugDirection::Input ? "Input" : "Output"),
                 static_cast<int>(usage));
    
    // Build the appropriate command to query the stream format
    std::vector<uint8_t> cmd = {
         kAVCStatusInquiryCommand,
         subunitAddr,
         streamFormatOpcode_,  // Use the opcode member variable (can be switched if needed)
         0xC0,
         static_cast<uint8_t>((direction == PlugDirection::Input) ? 0x00 : 0x01),  // Cast to uint8_t to avoid narrowing
         0x00,
         0x00,
         plugNum,
         0xFF, 0xFF
    };
    
    // Try with the primary opcode
    auto respResult = commandInterface_->sendCommand(cmd);
    
    // If not implemented, try with alternate opcode
    if (respResult && !respResult.error().empty() && 
         respResult.error()[0] == kAVCNotImplementedStatus) {
         spdlog::debug("Stream format opcode 0x{:02x} not implemented, trying alternate 0x{:02x}",
                     streamFormatOpcode_, kAlternateStreamFormatOpcode);
         
         cmd[2] = kAlternateStreamFormatOpcode;
         respResult = commandInterface_->sendCommand(cmd);
    }
    
    // If we have a response, check its success status
    if (respResult && !respResult.error().empty() &&
         (respResult.error()[0] == kAVCImplementedStatus || respResult.error()[0] == kAVCAcceptedStatus)) {
         return parseStreamFormatResponse(respResult.value());
    }
    
    // If we got here, we failed to get a valid stream format
    if (!respResult) {
         spdlog::warn("Command error when querying stream format: 0x{:x}", 
                      static_cast<int>(respResult.error()));
         return std::unexpected(respResult.error());
    } else {
         spdlog::warn("Unexpected response status 0x{:02x} when querying stream format", 
                      static_cast<int>(respResult.error()[0]));
         return std::unexpected(IOKitError(kIOReturnError));
    }
}

//--------------------------------------------------------------------------
// DeviceParser::querySupportedPlugStreamFormats()
//--------------------------------------------------------------------------
std::expected<std::vector<AudioStreamFormat>, IOKitError> DeviceParser::querySupportedPlugStreamFormats(
    uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage) {
    
    spdlog::debug("Querying supported stream formats for plug: subunit=0x{:02x}, num={}, direction={}",
                 subunitAddr, plugNum, (direction == PlugDirection::Input ? "Input" : "Output"));
    
    std::vector<AudioStreamFormat> supportedFormats;
    
    auto currentformatResult = queryPlugStreamFormat(subunitAddr, plugNum, direction, usage);
    if (currentformatResult) {
         supportedFormats.push_back(currentformatResult.value());
    }
    
    return supportedFormats;
}

//--------------------------------------------------------------------------
// DeviceParser::querySignalSource()
//--------------------------------------------------------------------------
std::expected<AudioPlug::ConnectionInfo, IOKitError> DeviceParser::querySignalSource(
    uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage) {
    
    spdlog::debug("Querying signal source for plug: subunit=0x{:02x}, num={}", subunitAddr, plugNum);
    
    // Build the query connection command
    std::vector<uint8_t> cmd = {
         kAVCStatusInquiryCommand,
         subunitAddr,
         kAVCConnectOpcode,
         0x00, // Status
         0x00, // Input plug
         plugNum,
         0xFF, // Output subunit (to be filled in by the device)
         0xFF  // Output plug (to be filled in by the device)
    };
    
    auto respResult = commandInterface_->sendCommand(cmd);
    if (!respResult) {
         spdlog::warn("Failed to query signal source for plug: 0x{:x}", 
                      static_cast<int>(respResult.error()));
         return std::unexpected(respResult.error());
    }
    
    const auto& response = respResult.value();
    if (response.size() < 8 || (response[0] != kAVCImplementedStatus && response[0] != kAVCAcceptedStatus)) {
         spdlog::warn("Invalid connection response status: 0x{:02x} or size: {}", 
                      static_cast<int>(response[0]), response.size());
         return std::unexpected(IOKitError(kIOReturnError));
    }
    
    AudioPlug::ConnectionInfo connInfo;
    connInfo.sourceSubUnit = response[6];
    connInfo.sourcePlugNum = response[7];
    connInfo.sourcePlugStatus = 0; // Default value
    
    spdlog::debug("Plug is connected to subunit 0x{:02x}, plug {}", 
                 connInfo.sourceSubUnit, connInfo.sourcePlugNum);
    
    return connInfo;
}

//--------------------------------------------------------------------------
// DeviceParser::readDescriptor()
//--------------------------------------------------------------------------
std::expected<std::vector<uint8_t>, IOKitError> DeviceParser::readDescriptor(
    uint8_t subunitAddr, uint8_t descriptorType) {
    
    spdlog::debug("Reading descriptor type 0x{:02x} from subunit 0x{:02x}", 
                 descriptorType, subunitAddr);
    
    std::vector<uint8_t> cmd = {
         kAVCStatusInquiryCommand,
         subunitAddr,
         kAVCReadDescriptorOpcode,
         0x00,
         static_cast<uint8_t>(descriptorType >> 8),
         static_cast<uint8_t>(descriptorType & 0xFF),
         0x00,
         0x00
    };
    
    auto respResult = commandInterface_->sendCommand(cmd);
    if (!respResult) {
         spdlog::warn("Failed to read descriptor: 0x{:x}", static_cast<int>(respResult.error()));
         return std::unexpected(respResult.error());
    }
    
    const auto& response = respResult.value();
    if (response.size() < 8 || response[0] != kAVCImplementedStatus) {
         spdlog::warn("Invalid descriptor response status: 0x{:02x} or size: {}", 
                      static_cast<int>(response[0]), response.size());
         return std::unexpected(IOKitError(kIOReturnError));
    }
    
    return response;
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
    
    auto respResult = commandInterface_->sendCommand(command);
    if (respResult && !respResult.error().empty() &&
        respResult.error()[0] == kAVCNotImplementedStatus) {
         uint8_t originalOpcode = command[2];
         uint8_t newOpcode = (originalOpcode == kStartingStreamFormatOpcode) ?
                             kAlternateStreamFormatOpcode : kStartingStreamFormatOpcode;
         spdlog::debug("Stream format opcode 0x{:02x} not implemented, trying alternate 0x{:02x}",
                      originalOpcode, newOpcode);
         command[2] = newOpcode;
         respResult = commandInterface_->sendCommand(command);
    }
    
    return respResult;
}

//--------------------------------------------------------------------------
// DeviceParser::discoverAndParseSubunits()
//--------------------------------------------------------------------------
std::expected<void, IOKitError> DeviceParser::discoverAndParseSubunits() {
    spdlog::debug("Discovering and parsing subunits...");
    
    std::vector<uint8_t> cmd = {
         kAVCStatusInquiryCommand,
         0xFF,
         0x31,
         0x07,
         0xFF, 0xFF, 0xFF, 0xFF
    };
    
    auto respResult = commandInterface_->sendCommand(cmd);
    if (!respResult) {
         spdlog::error("Failed to send subunit info command.");
         return std::unexpected(respResult.error());
    }
    const auto& response = respResult.value();
    if (response.size() < 8 || (response[0] != kAVCImplementedStatus && response[0] != kAVCAcceptedStatus)) {
         spdlog::error("Subunit info command returned unexpected status or insufficient bytes.");
         return std::unexpected(IOKitError(kIOReturnError));
    }
    
    // Check for Music Subunit (type 0x0C)
    info_.hasMusicSubunit_ = false;
    for (size_t i = 4; i < response.size() - 1; i += 2) {
         uint8_t subunitType = (response[i] >> 3);
         if (subunitType == static_cast<uint8_t>(SubunitType::Music)) {
              info_.hasMusicSubunit_ = true;
              spdlog::info("Device has Music Subunit");
              auto result = parseMusicSubunitDetails();
              if (!result) {
                  spdlog::warn("Failed to parse Music Subunit details: 0x{:x}", static_cast<int>(result.error()));
              }
              break;
         }
    }
    
    // Check for Audio Subunit (type 0x08)
    info_.hasAudioSubunit_ = false;
    for (size_t i = 4; i < response.size() - 1; i += 2) {
         uint8_t subunitType = (response[i] >> 3);
         if (subunitType == static_cast<uint8_t>(SubunitType::Audio)) {
              info_.hasAudioSubunit_ = true;
              spdlog::info("Device has Audio Subunit");
              auto result = parseAudioSubunitDetails();
              if (!result) {
                  spdlog::warn("Failed to parse Audio Subunit details: 0x{:x}", static_cast<int>(result.error()));
              }
              break;
         }
    }
    
    return {};
}

} // namespace FWA