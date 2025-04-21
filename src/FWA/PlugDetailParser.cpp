#include "FWA/PlugDetailParser.hpp"
#include "FWA/CommandInterface.h"
#include "FWA/AudioPlug.hpp"
#include "FWA/AudioStreamFormat.hpp"
#include "FWA/Enums.hpp"
#include "FWA/Helpers.h"
#include <IOKit/avc/IOFireWireAVCConsts.h>
#include <spdlog/spdlog.h>
#include <vector>
#include <memory>
#include <algorithm>
#include <stdexcept>

namespace FWA {

// todo: move to enums
constexpr uint8_t kAVCStreamFormatCurrentQuerySubfunction = 0xC0;
constexpr uint8_t kAVCStreamFormatSupportedQuerySubfunction = 0xC1;
constexpr uint8_t kStartingStreamFormatOpcode = 0xBF;
constexpr uint8_t kAlternateStreamFormatOpcode  = 0x2F;
constexpr uint8_t kUnitAddress = 0xFF;

PlugDetailParser::PlugDetailParser(CommandInterface* commandInterface)
    : commandInterface_(commandInterface), streamFormatOpcode_(kStartingStreamFormatOpcode)
{
    if (!commandInterface_) {
        spdlog::critical("PlugDetailParser: CommandInterface pointer is null.");
        throw std::runtime_error("PlugDetailParser requires a valid CommandInterface.");
    }
}

std::expected<std::shared_ptr<AudioPlug>, IOKitError> PlugDetailParser::parsePlugDetails(
    uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage)
{
    spdlog::debug("PlugDetailParser: Parsing details for plug: subunit=0x{:02x}, num={}, direction={}, usage={}",
                 subunitAddr, plugNum, (direction == PlugDirection::Input ? "Input" : "Output"), static_cast<int>(usage));
    auto plug = std::make_shared<AudioPlug>(subunitAddr, plugNum, direction, usage);
    auto formatResult = queryPlugStreamFormat(subunitAddr, plugNum, direction, usage);
    if (formatResult) {
        plug->setCurrentStreamFormat(formatResult.value());
        spdlog::debug("PlugDetailParser: Successfully retrieved current stream format for plug");
    } else {
        spdlog::warn("PlugDetailParser: Failed to retrieve current stream format for plug: 0x{:x}", static_cast<int>(formatResult.error()));
    }
    auto supportedFormatsResult = querySupportedPlugStreamFormats(subunitAddr, plugNum, direction, usage);
    if (supportedFormatsResult) {
        const auto& formats = supportedFormatsResult.value();
        spdlog::debug("PlugDetailParser: Retrieved {} supported format(s) for plug.", formats.size());
        for (const auto& format : formats) {
            plug->addSupportedStreamFormat(format);
        }
    } else {
        spdlog::warn("PlugDetailParser: Failed to retrieve supported stream formats for plug: 0x{:x}", static_cast<int>(supportedFormatsResult.error()));
    }
    if (direction == PlugDirection::Input) {
        auto connInfoResult = querySignalSource(subunitAddr, plugNum, direction, usage);
        if (connInfoResult) {
            plug->setConnectionInfo(connInfoResult.value());
        } else {
            spdlog::warn("PlugDetailParser: Failed to retrieve signal source for plug: 0x{:x}", static_cast<int>(connInfoResult.error()));
        }
    }
    return plug;
}

std::expected<AudioStreamFormat, IOKitError> PlugDetailParser::queryPlugStreamFormat(
    uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage)
{
    spdlog::debug("PlugDetailParser: Querying current stream format for plug: subunit=0x{:02x}, num={}, direction={}, usage={}",
                 subunitAddr, plugNum, (direction == PlugDirection::Input ? "Input" : "Output"), static_cast<int>(usage));
    std::vector<uint8_t> cmd;
    buildStreamFormatCommandBase(cmd, subunitAddr, plugNum, direction, kAVCStreamFormatCurrentQuerySubfunction);
    cmd[2] = streamFormatOpcode_;
    auto respResult = commandInterface_->sendCommand(cmd);
    if (respResult && !respResult.value().empty() && respResult.value()[0] == kAVCNotImplementedStatus && streamFormatOpcode_ == kStartingStreamFormatOpcode) {
        spdlog::debug("PlugDetailParser: Stream format opcode 0x{:02x} (Current) not implemented, trying alternate 0x{:02x}", streamFormatOpcode_, kAlternateStreamFormatOpcode);
        streamFormatOpcode_ = kAlternateStreamFormatOpcode;
        cmd[2] = streamFormatOpcode_;
        respResult = commandInterface_->sendCommand(cmd);
    }
    if (!respResult) {
        spdlog::warn("PlugDetailParser: Command error when querying current stream format: 0x{:x}", static_cast<int>(respResult.error()));
        return std::unexpected(respResult.error());
    }
    const auto& response = respResult.value();
    if (response[0] == kAVCImplementedStatus || response[0] == kAVCAcceptedStatus) {
        return parseStreamFormatResponse(response, kAVCStreamFormatCurrentQuerySubfunction);
    } else {
        spdlog::warn("PlugDetailParser: Unexpected response status 0x{:02x} when querying current stream format", response[0]);
        return std::unexpected(IOKitError(kIOReturnError));
    }
}

std::expected<std::vector<AudioStreamFormat>, IOKitError> PlugDetailParser::querySupportedPlugStreamFormats(
    uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage)
{
    spdlog::debug("PlugDetailParser: Querying ALL supported stream formats for plug: subunit=0x{:02x}, num={}, direction={}",
                 subunitAddr, plugNum, (direction == PlugDirection::Input ? "Input" : "Output"));
    std::vector<AudioStreamFormat> supportedFormats;
    uint8_t listIndex = 0;
    const uint8_t MAX_FORMAT_INDEX = 16;
    while (true) {
        if (listIndex > MAX_FORMAT_INDEX) {
            spdlog::warn("PlugDetailParser: Reached max supported format index {} for plug 0x{:02x}/{} {}", MAX_FORMAT_INDEX, subunitAddr, plugNum, (direction == PlugDirection::Input ? "Input" : "Output"));
            break;
        }
        std::vector<uint8_t> cmd;
        buildStreamFormatCommandBase(cmd, subunitAddr, plugNum, direction, kAVCStreamFormatSupportedQuerySubfunction, listIndex);
        cmd[2] = streamFormatOpcode_;
        auto respResult = commandInterface_->sendCommand(cmd);
        if (respResult && !respResult.value().empty() && respResult.value()[0] == kAVCNotImplementedStatus && streamFormatOpcode_ == kStartingStreamFormatOpcode) {
            spdlog::debug("PlugDetailParser: Stream format opcode 0x{:02x} (Supported) not implemented, trying alternate 0x{:02x}", streamFormatOpcode_, kAlternateStreamFormatOpcode);
            streamFormatOpcode_ = kAlternateStreamFormatOpcode;
            cmd[2] = streamFormatOpcode_;
            respResult = commandInterface_->sendCommand(cmd);
        }
        if (!respResult) {
            spdlog::warn("PlugDetailParser: Command error when querying supported stream format index {}: 0x{:x}", listIndex, static_cast<int>(respResult.error()));
            break;
        }
        const auto& response = respResult.value();
        if (response[0] == kAVCImplementedStatus || response[0] == kAVCAcceptedStatus) {
            auto formatResult = parseStreamFormatResponse(response, kAVCStreamFormatSupportedQuerySubfunction);
            if (formatResult) {
                supportedFormats.push_back(formatResult.value());
            } else {
                spdlog::warn("PlugDetailParser: Failed to parse supported stream format at index {}: 0x{:x}", listIndex, static_cast<int>(formatResult.error()));
                break;
            }
        } else {
            spdlog::debug("PlugDetailParser: No more supported formats at index {} (status=0x{:02x})", listIndex, response[0]);
            break;
        }
        ++listIndex;
    }
    spdlog::info("PlugDetailParser: Finished querying supported formats. Found {} formats for plug 0x{:02x}/{} {}", supportedFormats.size(), subunitAddr, plugNum, (direction == PlugDirection::Input ? "Input" : "Output"));
    return supportedFormats;
}

std::expected<AudioPlug::ConnectionInfo, IOKitError> PlugDetailParser::querySignalSource(
    uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage)
{
    if (direction != PlugDirection::Input) {
        return std::unexpected(IOKitError(kIOReturnBadArgument));
    }
    spdlog::debug("PlugDetailParser: Querying signal source for destination plug: subunit=0x{:02x}, num={}", subunitAddr, plugNum);
    std::vector<uint8_t> cmd = {
        kAVCStatusInquiryCommand,
        subunitAddr,
        0x1A, // kAVCSignalSourceOpcode
        0xFF,
        plugNum,
        0xFF,
        0xFF,
        0xFF
    };
    auto respResult = commandInterface_->sendCommand(cmd);
    if (!respResult) {
        spdlog::warn("PlugDetailParser: Command error querying signal source for plug 0x{:02x}/{}: 0x{:x}", subunitAddr, plugNum, static_cast<int>(respResult.error()));
        return std::unexpected(respResult.error());
    }
    const auto& response = respResult.value();
    if (response[0] == kAVCNotImplementedStatus) {
        spdlog::warn("PlugDetailParser: Query signal source (0x1A) is NOT IMPLEMENTED for plug 0x{:02x}/{}.", subunitAddr, plugNum);
        return std::unexpected(IOKitError(kIOReturnUnsupported));
    }
    if (response[0] == kAVCRejectedStatus) {
        spdlog::warn("PlugDetailParser: Query signal source (0x1A) was REJECTED for plug 0x{:02x}/{}.", subunitAddr, plugNum);
        return std::unexpected(IOKitError(kIOReturnNotPermitted));
    }
    if (response.size() < 8 || (response[0] != kAVCImplementedStatus && response[0] != kAVCAcceptedStatus)) {
        spdlog::warn("PlugDetailParser: Invalid signal source response status: 0x{:02x} or size: {} for plug 0x{:02x}/{}", static_cast<int>(response[0]), response.size(), subunitAddr, plugNum);
        return std::unexpected(IOKitError(kIOReturnBadResponse));
    }
    AudioPlug::ConnectionInfo connInfo;
    connInfo.sourceSubUnit = response[5];
    connInfo.sourcePlugNum = response[6];
    connInfo.sourcePlugStatus = response[7];
    if (connInfo.sourcePlugNum == 0xFE) {
        spdlog::debug("PlugDetailParser: Plug 0x{:02x}/{} is not connected (source is Invalid FE).", subunitAddr, plugNum);
    } else {
        spdlog::debug("PlugDetailParser: Plug 0x{:02x}/{} is connected to subunit 0x{:02x}, plug {} (status=0x{:02x})", subunitAddr, plugNum, connInfo.sourceSubUnit, connInfo.sourcePlugNum, connInfo.sourcePlugStatus);
    }
    return connInfo;
}

std::expected<AudioStreamFormat, IOKitError> PlugDetailParser::parseStreamFormatResponse(
    const std::vector<uint8_t>& responseData, uint8_t generatingSubfunction)
{
    size_t headerSize = 0;
    size_t formatStartOffset = 0;
    if (generatingSubfunction == kAVCStreamFormatCurrentQuerySubfunction) {
        headerSize = 10;
        formatStartOffset = 10;
        spdlog::trace("PlugDetailParser: Parsing C0 response, expecting format block at offset 10");
    } else if (generatingSubfunction == kAVCStreamFormatSupportedQuerySubfunction) {
        headerSize = 11;
        formatStartOffset = 11;
        spdlog::trace("PlugDetailParser: Parsing C1 response, expecting format block at offset 11");
    } else {
        spdlog::error("PlugDetailParser: parseStreamFormatResponse called with unknown generating subfunction: 0x{:02x}", generatingSubfunction);
        return std::unexpected(IOKitError(kIOReturnInternalError));
    }
    if (responseData.size() < formatStartOffset) {
         spdlog::error("PlugDetailParser: Response too short ({}) for expected header offset ({})", responseData.size(), formatStartOffset);
         return std::unexpected(IOKitError(kIOReturnUnderrun));
    }
    if (responseData.size() == formatStartOffset) {
          spdlog::warn("PlugDetailParser: Response contains only header, no format block data. Subfunction: 0x{:02x}", generatingSubfunction);
          return std::unexpected(IOKitError(kIOReturnUnderrun));
    }
    const uint8_t* fmt = responseData.data() + formatStartOffset;
    size_t fmtLength = responseData.size() - formatStartOffset;
    spdlog::trace("PlugDetailParser: Calculated format block length: {} bytes", fmtLength);
    if (fmtLength < 2) {
         spdlog::error("PlugDetailParser: Format block too short ({}) to determine type.", fmtLength);
         return std::unexpected(IOKitError(kIOReturnBadResponse));
    }
    if (fmt[0] == 0x90 && fmt[1] == 0x40) {
        if (fmtLength < 5) {
             spdlog::error("PlugDetailParser: Compound AM824 block too short: {} bytes", fmtLength);
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
            spdlog::error("PlugDetailParser: Insufficient format info fields: required {} bytes in block, got {}", requiredLengthInBlock, fmtLength);
            return std::unexpected(IOKitError(kIOReturnUnderrun));
        }
        size_t offset = 5;
        for (uint8_t i = 0; i < numFields; ++i) {
             if (offset + 1 >= fmtLength) {
                 spdlog::error("PlugDetailParser: Format block ended unexpectedly while parsing field {}", i);
                 return std::unexpected(IOKitError(kIOReturnUnderrun));
             }
            ChannelFormatInfo cf;
            cf.channelCount = fmt[offset];
            cf.formatCode = static_cast<StreamFormatCode>(fmt[offset + 1]);
            channels.push_back(cf);
            offset += 2;
        }
        AudioStreamFormat format(fmtType, sampleRate, sync, std::move(channels));
        spdlog::info("PlugDetailParser: Parsed compound AM824 stream format:\n{}", format.toString());
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
            spdlog::info("PlugDetailParser: Parsed 6-byte single-stream AM824 format:\n{}", format.toString());
            return format;
        } else if (fmtLength == 3) {
            spdlog::debug("PlugDetailParser: Handling 3-byte AM824 format (90 00 XX)");
            FormatType fmtType = FormatType::AM824;
            SampleRate sampleRate = SampleRate::DontCare;
            bool sync = false;
            StreamFormatCode formatCode = static_cast<StreamFormatCode>(fmt[2]);
            uint8_t channelCount = 2;
            ChannelFormatInfo cf(channelCount, formatCode);
            AudioStreamFormat format(fmtType, sampleRate, sync, {cf});
            spdlog::info("PlugDetailParser: Parsed 3-byte single-stream AM824 format:\n{}", format.toString());
            return format;
        } else {
            spdlog::error("PlugDetailParser: Unrecognized AM824 (90 00) format block length: {}", fmtLength);
            return std::unexpected(IOKitError(kIOReturnBadResponse));
        }
    } else {
        spdlog::error("PlugDetailParser: Unrecognized format block start: {:02x} {:02x}...", fmt[0], fmt[1]);
        return std::unexpected(IOKitError(kIOReturnBadResponse));
    }
}

void PlugDetailParser::buildStreamFormatCommandBase(
    std::vector<uint8_t>& cmd,
    uint8_t subunitAddr,
    uint8_t plugNum,
    PlugDirection direction,
    uint8_t subfunction,
    uint8_t listIndex)
{
    cmd.clear();
    cmd.push_back(kAVCStatusInquiryCommand);
    cmd.push_back(subunitAddr);
    cmd.push_back(streamFormatOpcode_); // Will be overwritten by caller if needed
    cmd.push_back(subfunction);
    if (subunitAddr == kUnitAddress) {
        cmd.push_back((direction == PlugDirection::Input) ? 0x00 : 0x01);
        cmd.push_back(0x00);
        cmd.push_back((plugNum < 0x80) ? 0x00 : 0x01);
        cmd.push_back(plugNum);
        cmd.push_back(0xFF);
    } else {
        cmd.push_back((direction == PlugDirection::Input) ? 0x00 : 0x01);
        cmd.push_back(0x01);
        cmd.push_back(plugNum);
        cmd.push_back(0xFF);
        cmd.push_back(0xFF);
    }
    if (subfunction == kAVCStreamFormatSupportedQuerySubfunction) {
        cmd.push_back(listIndex);
    }
    // Pad to at least 11 bytes for current, 12 for supported
    if (cmd.size() < 11) cmd.resize(11, 0xFF);
    if (subfunction == kAVCStreamFormatSupportedQuerySubfunction && cmd.size() < 12) cmd.resize(12, 0xFF);
}

} // namespace FWA
