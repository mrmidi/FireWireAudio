#include "FWA/DeviceParser.hpp"
#include "FWA/CommandInterface.h"
#include "FWA/AudioDevice.h"
#include <IOKit/avc/IOFireWireAVCConsts.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <iomanip>

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

DeviceParser::DeviceParser(std::shared_ptr<AudioDevice> device)
    : device_(device), info_(device->info_)
{
    // Create subunit objects that will later be filled in.
    musicSubunit_ = std::make_shared<MusicSubunit>();
    audioSubunit_ = std::make_shared<AudioSubunit>();
}

//--------------------------------------------------------------------------
// DeviceParser::parse()
//--------------------------------------------------------------------------
std::expected<void, IOKitError> DeviceParser::parse() {
    spdlog::info("Parsing device capabilities for device: {}", device_->getDeviceName());

    // (A) First, discover the unit plugs and update AudioDevice's plug counts.
    if (auto discResult = discoverUnitPlugs(); !discResult) {
        spdlog::error("Failed to discover unit plugs.");
        return discResult;
    }

    // (B) If the device reports Isochronous Input plugs, perform further discovery.
    if (device_->numIsoInPlugs > 0) {
        if (auto isoResult = parseIsoInPlugs(); !isoResult) {
            spdlog::error("Failed to parse Isochronous Input plugs.");
            return isoResult;
        }
    }

    // If the device reports Isochronous Output plugs
    if (device_->numIsoOutPlugs > 0) {
        if (auto isoResult = parseIsoOutPlugs(); !isoResult) {
            spdlog::error("Failed to parse Isochronous Output plugs.");
            return isoResult;
        }
    }


    // (C) Continue with further discovery (e.g., Music Subunit and Audio Subunit)
    if (auto musicResult = parseMusicSubunit(); !musicResult) {
        spdlog::error("Failed to parse Music Subunit");
        return std::unexpected(musicResult.error());
    }

    if (auto audioResult = parseAudioSubunit(); !audioResult) {
        spdlog::error("Failed to parse Audio Subunit");
        return std::unexpected(audioResult.error());
    }

    return {};
}

//--------------------------------------------------------------------------
// DeviceParser::discoverUnitPlugs()
//--------------------------------------------------------------------------
std::expected<void, IOKitError> DeviceParser::discoverUnitPlugs() {
    spdlog::info("Discovering unit plugs...");

    // Build the discovery command (8 bytes):
    //   [0] : kAVCStatusInquiryCommand
    //   [1] : 0xFF (unit plug indicator)
    //   [2] : 0x02 (discovery command)
    //   [3] : 0x00 (reserved)
    //   [4-7]: 0xFF (wildcard)
    std::vector<uint8_t> cmd = {
        kAVCStatusInquiryCommand,
        0xFF,
        0x02,
        0x00,
        0xFF, 0xFF, 0xFF, 0xFF
    };

    auto respResult = device_->getCommandInterface()->sendCommand(cmd);
    if (!respResult) {
        spdlog::error("Failed to send unit plug discovery command.");
        return std::unexpected(respResult.error());
    }
    const auto& response = respResult.value();
    if (response.size() < 8 || response[0] != kAVCImplementedStatus) {
        spdlog::error("Discovery command returned unexpected status or insufficient bytes.");
        return std::unexpected(IOKitError(kIOReturnError));
    }

    // Update AudioDevice's private members.
    device_->numIsoInPlugs  = response[4];
    device_->numIsoOutPlugs = response[5];
    device_->numExtInPlugs  = response[6];
    device_->numExtOutPlugs = response[7];

    spdlog::info("Discovered unit plugs: IsoIn = {}, IsoOut = {}, ExtIn = {}, ExtOut = {}",
                 device_->numIsoInPlugs, device_->numIsoOutPlugs,
                 device_->numExtInPlugs, device_->numExtOutPlugs);

    return {};
}

//--------------------------------------------------------------------------
// DeviceParser::parseIsoInPlugs()
//--------------------------------------------------------------------------
std::expected<void, IOKitError> DeviceParser::parseIsoInPlugs() {
    spdlog::info("Parsing Isochronous Input Plugs...");
    // For each isochronous input plug (as reported in device_->numIsoInPlugs),
    // send a command to retrieve its current stream format.
    for (uint8_t i = 0; i < device_->numIsoInPlugs; ++i) {
        // Create an AudioPlug with direction Input and usage Isochronous.
        auto plug = std::make_shared<AudioPlug>(0xFF, i, PlugDirection::Input, PlugUsage::Isochronous);

        // Build the command to retrieve current stream format for an input plug.
        // Here we mimic the structure used for unit plugs, but adjust the flag for input.
        std::vector<uint8_t> cmdStream = {
            kAVCStatusInquiryCommand,
            0xFF,                         // Unit plug indicator.
            kStartingStreamFormatOpcode,  // Start with initial opcode.
            0xC0,
            0x00,                         // For input plug, flag 0x00.
            0x00,                         // Unit plug indicator.
            0x00,                         // Plug category (e.g. PCR for plugNum < 0x80).
            i,                            // Plug number.
            0xFF, 0xFF                    // Wildcard.
        };

        auto streamRespResult = device_->getCommandInterface()->sendCommand(cmdStream);
        if (streamRespResult) {
            // If the response indicates "not implemented" (kAVCNotImplementedStatus), try the alternate opcode.
            if (!streamRespResult.value().empty() &&
                streamRespResult.value()[0] == kAVCNotImplementedStatus)
            {
                spdlog::info("Opcode 0xBF not supported for IsoIn plug {} (response = 0x{:02x}); trying alternate opcode 0x2F",
                             i, streamRespResult.value()[0]);
                cmdStream[2] = kAlternateStreamFormatOpcode;
                streamRespResult = device_->getCommandInterface()->sendCommand(cmdStream);
            }
            auto formatResult = parseStreamFormat(streamRespResult.value());
            if (formatResult) {
                plug->setCurrentStreamFormat(formatResult.value());
            } else {
                spdlog::warn("Failed to parse stream format for IsoIn plug {}", i);
            }
        } else {
            spdlog::warn("Failed to get stream format for IsoIn plug {}", i);
        }

        

        if (device_->info_.musicSubunit) {
            device_->info_.musicSubunit->addIsoInputPlug(plug);
            spdlog::info("Added IsoIn plug {} to music subunit: {}", i,
                         plug->getCurrentStreamFormat() ? plug->getCurrentStreamFormat()->toString() : "None");
        }
    }
    return {};
}

//--------------------------------------------------------------------------
// DeviceParser::parseIsoOutPlugs()
//--------------------------------------------------------------------------
std::expected<void, IOKitError> DeviceParser::parseIsoOutPlugs() {
    spdlog::info("Parsing Isochronous Output Plugs...");
    // For each isochronous output plug (as reported in device_->numIsoOutPlugs),
    // send a command to retrieve its current stream format.
    for (uint8_t i = 0; i < device_->numIsoOutPlugs; ++i) {
        // Create an AudioPlug with direction Output and usage Isochronous.
        auto plug = std::make_shared<AudioPlug>(0xFF, i, PlugDirection::Output, PlugUsage::Isochronous);

        // Build the command to retrieve current stream format for an output plug.
        // Here we mimic the structure used for unit plugs, but adjust the flag for output.
        std::vector<uint8_t> cmdStream = {
            kAVCStatusInquiryCommand,
            0xFF,                         // Unit plug indicator.
            kStartingStreamFormatOpcode,  // Start with initial opcode.
            0xC0,
            0x01,                         // For output plug, flag 0x01.
            0x00,                         // Unit plug indicator.
            0x00,                         // Plug category (e.g. PCR for plugNum < 0x80).
            i,                            // Plug number.
            0xFF, 0xFF                    // Wildcard.
        };

        auto streamRespResult = device_->getCommandInterface()->sendCommand(cmdStream);
        if (streamRespResult) {
            // If the response indicates "not implemented" (kAVCNotImplementedStatus), try the alternate opcode.
            if (!streamRespResult.value().empty() &&
                streamRespResult.value()[0] == kAVCNotImplementedStatus)
            {
                spdlog::info("Opcode 0xBF not supported for IsoOut plug {} (response = 0x{:02x}); trying alternate opcode 0x2F",
                             i, streamRespResult.value()[0]);
                cmdStream[2] = kAlternateStreamFormatOpcode;
                streamRespResult = device_->getCommandInterface()->sendCommand(cmdStream);
            }
            auto formatResult = parseStreamFormat(streamRespResult.value());
            if (formatResult) {
                plug->setCurrentStreamFormat(formatResult.value());
            } else {
                spdlog::warn("Failed to parse stream format for IsoOut plug {}", i);
            }
        } else {
            spdlog::warn("Failed to get stream format for IsoOut plug {}", i);
        }
        if (device_->info_.musicSubunit) {
            device_->info_.musicSubunit->addIsoOutputPlug(plug);
            spdlog::info("Added IsoOut plug {} to music subunit: {}", i,
                         plug->getCurrentStreamFormat() ? plug->getCurrentStreamFormat()->toString() : "None");
        }
    }
    return {};
}


//--------------------------------------------------------------------------
// DeviceParser::parseMusicSubunit()
//--------------------------------------------------------------------------
std::expected<void, IOKitError> DeviceParser::parseMusicSubunit() {
    spdlog::info("Parsing Music Subunit Information...");

    // (For demonstration, we use a command to query music subunit plugs.)
    std::vector<uint8_t> command = {
        kAVCStatusInquiryCommand,
        kMusicSubunitSubUnitID,       // Music subunit ID: 0x60.
        0x02,                         // Discovery command identifier.
        0x00,
        0xFF, 0xFF, 0xFF, 0xFF
    };

    auto responseResult = device_->getCommandInterface()->sendCommand(command);
    if (!responseResult) {
        spdlog::error("Failed to retrieve Music Subunit information.");
        return std::unexpected(responseResult.error());
    }
    const auto& response = responseResult.value();
    if (response.size() < 6) {
        spdlog::error("Invalid Music Subunit response size: {} bytes", response.size());
        return std::unexpected(IOKitError(kIOReturnError));
    }

    // Assume response[4] is number of destination plugs, response[5] is number of source plugs.
    uint8_t numDest = response[4];
    uint8_t numSource = response[5];
    spdlog::info("Music Subunit Destination Plugs: {}", numDest);
    spdlog::info("Music Subunit Source Plugs: {}", numSource);
    musicSubunit_->setMusicDestPlugCount(numDest);
    musicSubunit_->setMusicSourcePlugCount(numSource);

    // For each destination plug in the Music Subunit, send a command to retrieve its stream format.
    for (uint8_t i = 0; i < numDest; ++i) {
        auto plug = std::make_shared<AudioPlug>(0xFF, i, PlugDirection::Output, PlugUsage::MusicSubunit);
        std::vector<uint8_t> cmdStream = {
            kAVCStatusInquiryCommand,
            0xFF,                         // For unit plugs, use 0xFF.
            kStartingStreamFormatOpcode,  // Start with initial opcode.
            0xC0,
            0x01,                         // For destination plug, flag = 0x01.
            0x00,
            0x00,
            i,
            0xFF, 0xFF
        };
        auto streamRespResult = device_->getCommandInterface()->sendCommand(cmdStream);
        if (streamRespResult) {
            if (!streamRespResult.value().empty() &&
                streamRespResult.value()[0] == kAVCNotImplementedStatus)
            {
                spdlog::info("Opcode 0xBF not supported for Music plug {} (response=0x{:02x}); switching opcode",
                             i, streamRespResult.value()[0]);
                cmdStream[2] = kAlternateStreamFormatOpcode;
                streamRespResult = device_->getCommandInterface()->sendCommand(cmdStream);
            }
            auto formatResult = parseStreamFormat(streamRespResult.value());
            if (formatResult) {
                plug->setCurrentStreamFormat(formatResult.value());
            } else {
                spdlog::warn("Failed to parse stream format for Music plug {}", i);
            }
        } else {
            spdlog::warn("Failed to get stream format for Music plug {}", i);
        }
        musicSubunit_->addMusicDestPlug(plug);
    }
    return {};
}

//--------------------------------------------------------------------------
// DeviceParser::parseAudioSubunit()
//--------------------------------------------------------------------------
std::expected<void, IOKitError> DeviceParser::parseAudioSubunit() {
    spdlog::info("Parsing Audio Subunit Information...");
    // For demonstration, simply create one audio destination plug.
    audioSubunit_->setAudioDestPlugCount(1);
    audioSubunit_->setAudioSourcePlugCount(1);

    auto plug = std::make_shared<AudioPlug>(0x08, 0, PlugDirection::Output, PlugUsage::AudioSubunit);
    audioSubunit_->addAudioDestPlug(plug);
    return {};
}

//--------------------------------------------------------------------------
// DeviceParser::parseStreamFormat()
//--------------------------------------------------------------------------
std::expected<AudioStreamFormat, IOKitError> DeviceParser::parseStreamFormat(const std::vector<uint8_t>& response) {
    // Assume the first 10 bytes are a header; the format block starts at offset 10.
    if (response.size() < 10) {
        spdlog::error("Response too short: {} bytes", response.size());
        return std::unexpected(IOKitError(kIOReturnUnderrun));
    }
    size_t headerSize = 10;
    size_t fmtLength = response.size() - headerSize;
    if (fmtLength < 7) {
        spdlog::error("Stream format block too short: {} bytes", fmtLength);
        return std::unexpected(IOKitError(kIOReturnUnderrun));
    }
    const uint8_t* fmt = response.data() + headerSize;

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
    std::vector<ChannelFormat> channels;
    size_t requiredLength = 5 + (numFields * 2);
    if (fmtLength < requiredLength) {
        spdlog::error("Insufficient format info fields: required {} bytes, got {}", requiredLength, fmtLength);
        return std::unexpected(IOKitError(kIOReturnUnderrun));
    }
    size_t offset = 5;
    for (uint8_t i = 0; i < numFields; ++i) {
        ChannelFormat cf;
        cf.channelCount = fmt[offset];
        cf.formatCode = fmt[offset + 1];
        channels.push_back(cf);
        offset += 2;
    }

    AudioStreamFormat format(fmtType, sampleRate, sync, channels);
    spdlog::info("Parsed stream format:\n{}", format.toString());
    return format;
}

} // namespace FWA
