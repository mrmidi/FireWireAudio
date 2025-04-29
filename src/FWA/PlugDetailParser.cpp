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
#include "FWA/Error.h"

namespace FWA {

constexpr uint8_t kUnitAddress = 0xFF;

PlugDetailParser::PlugDetailParser(CommandInterface* commandInterface)
    : commandInterface_(commandInterface), streamFormatOpcode_(kAVCStreamFormatOpcodePrimary)
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
        spdlog::debug("PlugDetailParser: Trying standard connection query (0x1A) for plug 0x{:02x}/{}", subunitAddr, plugNum);
        auto signalSourceResult = querySignalSource(subunitAddr, plugNum, direction, usage);
        if (signalSourceResult) {
            plug->setConnectionInfo(signalSourceResult.value());
            if (signalSourceResult.value().sourcePlugNum != 0xFE) {
                spdlog::info("PlugDetailParser: Plug 0x{:02x}/{} Connection (via 0x1A): Source SU=0x{:02x}, Plug={}",
                    subunitAddr, plugNum, signalSourceResult.value().sourceSubUnit, signalSourceResult.value().sourcePlugNum);
            } else {
                spdlog::info("PlugDetailParser: Plug 0x{:02x}/{} Connection (via 0x1A): Not Connected (Source=0xFE)", subunitAddr, plugNum);
            }
        } else {
            spdlog::warn("PlugDetailParser: Standard connection query (0x1A) failed for plug 0x{:02x}/{}: 0x{:x}",
                subunitAddr, plugNum, static_cast<int>(signalSourceResult.error()));
            // Fallback: Only for MusicSubunit and if 0x1A is unsupported
            if (signalSourceResult.error() == IOKitError::Unsupported && usage == PlugUsage::MusicSubunit) {
                spdlog::warn("PlugDetailParser: Standard query (0x1A) unsupported for Music plug, trying TA query (0x40 status)...");
                uint16_t musicPlugID = plugNum;
                uint8_t musicPlugType = 0x00;
                if (plug->getCurrentStreamFormat() && !plug->getCurrentStreamFormat()->getChannelFormats().empty()) {
                    switch(plug->getCurrentStreamFormat()->getChannelFormats()[0].formatCode) {
                        case StreamFormatCode::MidiConf:      musicPlugType = 0x01; break;
                        case StreamFormatCode::SMPTETimeCode: musicPlugType = 0x02; break;
                        case StreamFormatCode::SampleCount:   musicPlugType = 0x03; break;
                        case StreamFormatCode::SyncStream:    musicPlugType = 0x80; break;
                        default:                              musicPlugType = 0x00; break;
                    }
                }
                auto destConnResult = queryMusicInputPlugConnection_TA(subunitAddr, musicPlugType, musicPlugID);
                if (destConnResult) {
                    // --- Logging before setting ---
                    const auto& destInfo = destConnResult.value();
                    spdlog::debug("PlugDetailParser: Fallback 0x40 successful for Music Plug 0x{:02x}/{}. Got DestPlugID={}, StreamPos0={}, StreamPos1={}. Calling setDestConnectionInfo...",
                                 subunitAddr, plugNum, destInfo.destSubunitPlugId, destInfo.streamPosition0, destInfo.streamPosition1);
                    // --- End Logging ---
                    plug->setDestConnectionInfo(destInfo); // Use the correct setter
                    if (destConnResult.value().destSubunitPlugId != 0xFF) {
                        spdlog::info("PlugDetailParser: Plug 0x{:02x}/{} Connection (via 0x40 fallback): Dest SU Plug={}",
                            subunitAddr, plugNum, destConnResult.value().destSubunitPlugId);
                    } else {
                        spdlog::info("PlugDetailParser: Plug 0x{:02x}/{} Connection (via 0x40 fallback): Not Connected (Dest=0xFF)", subunitAddr, plugNum);
                    }
                } else if (destConnResult.error() == IOKitError::NotFound) {
                    spdlog::info("PlugDetailParser: Fallback query (0x40) confirmed no connection for Music plug 0x{:02x}/{}.", subunitAddr, plugNum);
                } else {
                    spdlog::warn("PlugDetailParser: Fallback connection query (0x40) also failed for Music plug 0x{:02x}/{}: 0x{:x}",
                        subunitAddr, plugNum, static_cast<int>(destConnResult.error()));
                }
            }
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
    // --- Add Logging BEFORE building ---
    spdlog::trace(" -> Building command for Current Stream Format (Subfunction 0x{:02x})", kAVCStreamFormatCurrentQuerySubfunction);
    // ---------------------------------
    buildStreamFormatCommandBase(cmd, subunitAddr, plugNum, direction, kAVCStreamFormatCurrentQuerySubfunction);
    cmd[2] = streamFormatOpcode_;
    // --- Add Logging AFTER building ---
    spdlog::trace(" -> Built initial command (opcode 0x{:02x}): {}", streamFormatOpcode_, Helpers::formatHexBytes(cmd));
    // --------------------------------
    auto respResult = commandInterface_->sendCommand(cmd);
    if (respResult && !respResult.value().empty() && respResult.value()[0] == kAVCNotImplementedStatus && streamFormatOpcode_ == kAVCStreamFormatOpcodePrimary) {
        spdlog::debug("PlugDetailParser: Stream format opcode 0x{:02x} (Current) not implemented, trying alternate 0x{:02x}", streamFormatOpcode_, kAVCStreamFormatOpcodeAlternate);
        streamFormatOpcode_ = kAVCStreamFormatOpcodeAlternate;
        cmd[2] = streamFormatOpcode_;
        // --- Add Logging for Fallback ---
        spdlog::trace(" -> Built fallback command (opcode 0x{:02x}): {}", streamFormatOpcode_, Helpers::formatHexBytes(cmd));
        // ------------------------------
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
    const uint8_t MAX_FORMAT_INDEX = 16; // Reduced for faster testing if needed
    bool triedFallback = false; // Track if we already tried the fallback opcode in this loop

    while (true) {
        if (listIndex > MAX_FORMAT_INDEX) {
            spdlog::warn("PlugDetailParser: Reached max supported format index {} for plug 0x{:02x}/{} {}", MAX_FORMAT_INDEX, subunitAddr, plugNum, (direction == PlugDirection::Input ? "Input" : "Output"));
            break;
        }
        std::vector<uint8_t> cmd;
        // --- Add Logging BEFORE building ---
        spdlog::trace(" -> Building command for Supported Stream Format (Subfunction 0x{:02x}, Index {})", kAVCStreamFormatSupportedQuerySubfunction, listIndex);
        // ---------------------------------
        buildStreamFormatCommandBase(cmd, subunitAddr, plugNum, direction, kAVCStreamFormatSupportedQuerySubfunction, listIndex);
        cmd[2] = streamFormatOpcode_;
        // --- Add Logging AFTER building ---
        spdlog::trace(" -> Built initial command (opcode 0x{:02x}): {}", streamFormatOpcode_, Helpers::formatHexBytes(cmd));
        // --------------------------------
        auto respResult = commandInterface_->sendCommand(cmd);
        // Fallback logic for Supported Formats Query
        if (respResult && !respResult.value().empty() && respResult.value()[0] == kAVCNotImplementedStatus && streamFormatOpcode_ == kAVCStreamFormatOpcodePrimary && !triedFallback) {
            spdlog::debug("PlugDetailParser: Stream format opcode 0x{:02x} (Supported) not implemented, trying alternate 0x{:02x}", streamFormatOpcode_, kAVCStreamFormatOpcodeAlternate);
            streamFormatOpcode_ = kAVCStreamFormatOpcodeAlternate; // Switch opcode for subsequent iterations too
            triedFallback = true; // Mark that we tried it
            cmd[2] = streamFormatOpcode_; // Update opcode byte
            // --- Add Logging for Fallback ---
            spdlog::trace(" -> Built fallback command (opcode 0x{:02x}): {}", streamFormatOpcode_, Helpers::formatHexBytes(cmd));
            // ------------------------------
            respResult = commandInterface_->sendCommand(cmd); // Re-send with new opcode
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
    // Reset opcode back to default *after* finishing the loop for this plug,
    // in case another plug needs the default first.
    streamFormatOpcode_ = kAVCStreamFormatOpcodePrimary;
    spdlog::info("PlugDetailParser: Finished querying supported formats. Found {} formats for plug 0x{:02x}/{} {}", supportedFormats.size(), subunitAddr, plugNum, (direction == PlugDirection::Input ? "Input" : "Output"));
    return supportedFormats;
}

std::expected<AudioPlug::ConnectionInfo, IOKitError> PlugDetailParser::querySignalSource(
    uint8_t subunitAddr, uint8_t plugNum, PlugDirection direction, PlugUsage usage)
{
    if (direction != PlugDirection::Input) {
        // This should ideally not happen if called correctly, but good to check.
        spdlog::error("PlugDetailParser::querySignalSource called for non-input plug.");
        return std::unexpected(IOKitError(kIOReturnBadArgument));
    }
    spdlog::debug("PlugDetailParser: Querying signal source for destination plug: subunit=0x{:02x}, num={}", subunitAddr, plugNum);
    // Define the SIGNAL SOURCE command (0x1A) - inquiry type
    std::vector<uint8_t> cmd = {
        kAVCStatusInquiryCommand, // 0x01 (Status)
        subunitAddr,              // Target subunit (or 0xFF for unit)
        kAVCDestinationPlugConfigureOpcode, // Opcode: SIGNAL SOURCE
        0xFF,                     // Operand[0]: Subunit_type = N/A for inquiry
        plugNum,                  // Operand[1]: Destination plug ID
        0xFF,                     // Operand[2]: Source Subunit Type = N/A (response field)
        0xFF,                     // Operand[3]: Source Plug ID = N/A (response field)
        0xFF                      // Operand[4]: Source Plug Status = N/A (response field)
    };
    // --- Add Logging BEFORE sending ---
    spdlog::trace(" -> Built Signal Source command (0x1A): {}", Helpers::formatHexBytes(cmd));
    // ----------------------------------
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
    spdlog::trace("  -> buildStreamFormatCommandBase called with: subunit=0x{:02x}, plugNum=0x{:02x}, direction={}, subfunc=0x{:02x}, listIndex={}",
                  subunitAddr, plugNum, (direction == PlugDirection::Input ? "Input" : "Output"), subfunction, (listIndex == 0xFF ? "N/A" : std::to_string(listIndex)));

    cmd.clear();
    cmd.push_back(kAVCStatusInquiryCommand);
    cmd.push_back(subunitAddr);
    cmd.push_back(streamFormatOpcode_); // Use the member variable (BF or 2F)
    cmd.push_back(subfunction);         // e.g., C0 or C1

    if (subunitAddr == kUnitAddress) { // Unit plugs (PCRs and External)
        cmd.push_back((direction == PlugDirection::Input) ? 0x00 : 0x01); // operand[0]: plug_direction
        uint8_t plugType = (plugNum < 0x80) ? 0x00 : 0x01; // 0=Iso, 1=External
        cmd.push_back(plugType);                                          // operand[1]: plug_type
        cmd.push_back(plugType);                                          // operand[2]: plug_type (repeated)
        cmd.push_back(plugNum);                                           // operand[3]: plug_ID
        cmd.push_back(0xFF);                                              // operand[4]: format_info_label
    } else { // Subunit plugs (Music/Audio)
        cmd.push_back((direction == PlugDirection::Input) ? 0x00 : 0x01); // operand[0]: plug_direction
        cmd.push_back(0x01);                                              // operand[1]: plug_type (1 = Subunit plug)
        cmd.push_back(plugNum);                                           // operand[2]: subunit_plug_ID
        cmd.push_back(0xFF);                                              // operand[3]: format_info_label
        cmd.push_back(0xFF);                                              // operand[4]: reserved
    }

    if (subfunction == FWA::kAVCStreamFormatSupportedQuerySubfunction) {
        if (listIndex == 0xFF) {
            spdlog::warn("buildStreamFormatCommandBase: listIndex needed for subfunction 0xC1 but not provided (using FF).");
            listIndex = 0xFF;
        }
        if (subunitAddr == kUnitAddress) {
            cmd.push_back(0xFF);      // operand[5]: reserved
            cmd.push_back(listIndex); // operand[6]: list_index
        } else {
            cmd.push_back(listIndex); // operand[5]: list_index
        }
    }

    size_t minLength = (subfunction == FWA::kAVCStreamFormatSupportedQuerySubfunction) ? 12 : 11;
    if (cmd.size() < minLength) {
        cmd.resize(minLength, 0xFF);
    }

    spdlog::trace("  -> buildStreamFormatCommandBase constructed ({} bytes): {}", cmd.size(), Helpers::formatHexBytes(cmd));
}

std::expected<AudioPlug::DestPlugConnectionInfo, IOKitError> PlugDetailParser::queryMusicInputPlugConnection_TA(
    uint8_t musicSubunitAddr,
    uint8_t musicPlugType,
    uint16_t musicPlugID)
{
    spdlog::debug("PlugDetailParser: Querying Music plug connection (0x40 status) for plug type=0x{:02x}, ID={}", musicPlugType, musicPlugID);
    std::vector<uint8_t> cmd;
    cmd.push_back(kAVCStatusInquiryCommand);
    cmd.push_back(musicSubunitAddr);
    cmd.push_back(kAVCDestinationPlugConfigureOpcode); // 0x40
    cmd.push_back(1);    // number_of_subcommands = 1
    cmd.push_back(0xFF); // result_status = FF
    cmd.push_back(0xFF); // number_of_completed_subcommands = FF
    // Subcommand[0]
    cmd.push_back(0xFF); // subcommand[0]: result_status = FF
    cmd.push_back(musicPlugType);
    cmd.push_back(static_cast<uint8_t>(musicPlugID >> 8));
    cmd.push_back(static_cast<uint8_t>(musicPlugID & 0xFF));
    cmd.push_back(0xFF); // subcommand[4]: subunit_plug_ID = FF
    cmd.push_back(0xFF); // subcommand[5]: stream_position[0] = FF
    cmd.push_back(0xFF); // subcommand[6]: stream_position[1] = FF
    spdlog::trace(" -> Built Dest Plug Configure Status command (0x40): {}", Helpers::formatHexBytes(cmd));
    auto respResult = commandInterface_->sendCommand(cmd);
    if (!respResult) {
        spdlog::warn("PlugDetailParser: Command error querying music plug connection (0x40) for plug 0x{:04x}: 0x{:x}", musicPlugID, static_cast<int>(respResult.error()));
        return std::unexpected(respResult.error());
    }
    const auto& response = respResult.value();
    if (response.size() < 13) {
         spdlog::warn("PlugDetailParser: Invalid Dest Plug Configure Status (0x40) response size: {} (expected >= 13) for plug 0x{:04x}", response.size(), musicPlugID);
         return std::unexpected(IOKitError::IOError);
    }
    uint8_t subcmdResultStatus = response[6];
    spdlog::debug("  -> Dest Plug Configure Status (0x40) subcmdResultStatus = 0x{:02x}", subcmdResultStatus);

    // --- FIX: Only parse connection details if status is OK (0x00) ---
    if (subcmdResultStatus == kAVCDestPlugResultStatusOK) { // 0x00
        AudioPlug::DestPlugConnectionInfo destInfo;
        destInfo.destSubunitPlugId = response[10];
        destInfo.streamPosition0 = response[11];
        destInfo.streamPosition1 = response[12];
        spdlog::debug("PlugDetailParser: Music plug type=0x{:02x}, ID=0x{:04x} connected to dest_subunit_plug={} (stream_pos=[{}, {}]). Storing Dest Info.",
                      musicPlugType, musicPlugID, destInfo.destSubunitPlugId, destInfo.streamPosition0, destInfo.streamPosition1);
        return destInfo;
    }
    // --- End Fix ---
    else if (subcmdResultStatus == kAVCDestPlugResultNoConnection) { // 0x01 (No Connection)
        spdlog::debug("PlugDetailParser: Music plug 0x{:04x} has no connection (Status 0x01).", musicPlugID);
        return std::unexpected(IOKitError::NotFound); // Return NotFound for "No Connection"
    } else if (subcmdResultStatus == kAVCDestPlugResultMusicPlugNotExist) { // 0x03
         spdlog::warn("PlugDetailParser: Dest Plug Configure Status (0x40) reports Music Plug 0x{:04x} does not exist (Status 0x03).", musicPlugID);
         return std::unexpected(IOKitError::NotFound);
    } else if (subcmdResultStatus == kAVCDestPlugResultSubunitPlugNotExist) { // 0x04
         spdlog::warn("PlugDetailParser: Dest Plug Configure Status (0x40) reports Subunit Plug does not exist (Status 0x04).", musicPlugID);
         return std::unexpected(IOKitError::NotFound);
    } else { // Any other error in subcommand
         spdlog::warn("PlugDetailParser: Dest Plug Configure Status (0x40) failed for plug 0x{:04x} with subcommand status 0x{:02x}.", musicPlugID, subcmdResultStatus);
         return std::unexpected(IOKitError::Error);
    }
}

} // namespace FWA
