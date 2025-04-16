// src/FWA/AVCInfoBlock.cpp
#include "FWA/AVCInfoBlock.hpp"
#include "FWA/Helpers.h" // For hex formatting utility
#include "FWA/Enums.hpp"
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <algorithm> // For std::min
#include <spdlog/spdlog.h>

namespace FWA {

//--------------------------------------------------------------------------
// AVCInfoBlock Constructor (from raw data only)
//--------------------------------------------------------------------------
AVCInfoBlock::AVCInfoBlock(std::vector<uint8_t> rawData)
  : rawData_(std::move(rawData))
{
    if (rawData_.size() < 4) {
        // Cannot even determine type or length reliably
        spdlog::error("AVCInfoBlock constructed with insufficient raw data ({} bytes). Needs at least 4.", rawData_.size());
        type_ = 0xFFFF; // Mark as unknown
        compoundLength_ = 0;
        primaryFieldsLength_ = 0;
        // Consider throwing an exception if this state is unacceptable
        // throw std::runtime_error("AVCInfoBlock raw data too short for header.");
        return; // Exit early
    }
    // Proceed with parsing header info
    parseInternal();
}

//--------------------------------------------------------------------------
// AVCInfoBlock::parseInternal() - Called by constructor
//--------------------------------------------------------------------------
void AVCInfoBlock::parseInternal() {
    nestedBlocks_.clear();
    // Parse Primary fields first to populate optional members
    parsePrimaryFields();

    nestedBlocks_.clear(); // Ensure clean state

    if (rawData_.size() < 4) {
        spdlog::error("AVCInfoBlock::parseInternal: Raw data size ({}) too small for header.", rawData_.size());
        return; // Cannot parse header
    }

    // Read lengths and type from the first 6 bytes
    compoundLength_ = (static_cast<uint16_t>(rawData_[0]) << 8) | rawData_[1];
    type_ = (static_cast<uint16_t>(rawData_[2]) << 8) | rawData_[3];

    // Basic sanity check on compound length vs actual data size
    size_t expectedTotalSize = compoundLength_ + 2;
    if (rawData_.size() < expectedTotalSize) {
        spdlog::warn("AVCInfoBlock::parseInternal: Raw data size ({}) is less than indicated by compound length ({}). Type=0x{:04X}. Parsing truncated data.",
                     rawData_.size(), expectedTotalSize, type_);
        // Adjust compoundLength internally for bounds checking? Or just limit parsing?
        // Let's limit parsing based on actual data size.
    }

    if (rawData_.size() < 6) {
        spdlog::warn("AVCInfoBlock::parseInternal: Raw data size ({}) too small for primary_fields_length. Type=0x{:04X}", rawData_.size(), type_);
        primaryFieldsLength_ = 0; // Assume no primary fields
    } else {
        primaryFieldsLength_ = (static_cast<uint16_t>(rawData_[4]) << 8) | rawData_[5];
    }

    // Calculate end of primary fields, respecting actual data size
    size_t actualDataLimit = rawData_.size();
    size_t primaryFieldsEndOffset = 6 + primaryFieldsLength_;

    if (primaryFieldsEndOffset > actualDataLimit) {
        spdlog::error("AVCInfoBlock::parseInternal: Primary fields length ({}) exceeds block data size ({}). Type=0x{:04X}",
                     primaryFieldsLength_, actualDataLimit, type_);
        primaryFieldsEndOffset = actualDataLimit; // Limit parsing to actual data
    }

    // --- Parse Nested Blocks (Secondary Fields Area) ---
    size_t currentOffset = primaryFieldsEndOffset;
    while (currentOffset < actualDataLimit) {
        if (currentOffset + 2 > actualDataLimit) { // Need 2 bytes for nested compound length
            spdlog::warn("AVCInfoBlock::parseInternal: Truncated nested info block header found at offset {}. Type=0x{:04X}", currentOffset, type_);
            break; // Stop parsing nested blocks
        }

        uint16_t nestedCompoundLength = (static_cast<uint16_t>(rawData_[currentOffset]) << 8) | rawData_[currentOffset + 1];
        size_t nestedInfoBlockSize = nestedCompoundLength + 2;

        if (currentOffset + nestedInfoBlockSize > actualDataLimit) {
            spdlog::error("AVCInfoBlock::parseInternal: Nested info block size ({}) exceeds remaining data at offset {}. Type=0x{:04X}",
                         nestedInfoBlockSize, currentOffset, type_);
            break; // Stop parsing nested blocks
        }

        // Extract raw data for the nested block
        std::vector<uint8_t> nestedRawData(rawData_.begin() + currentOffset,
                                           rawData_.begin() + currentOffset + nestedInfoBlockSize);

        try {
            // Create and implicitly parse the nested block
            nestedBlocks_.push_back(std::make_shared<AVCInfoBlock>(std::move(nestedRawData)));
             spdlog::trace("Parsed nested info block of size {} at offset {}", nestedInfoBlockSize, currentOffset);
        } catch (const std::exception& e) {
             spdlog::error("AVCInfoBlock::parseInternal: Exception creating nested block at offset {}: {}", currentOffset, e.what());
             // Decide whether to continue parsing or stop
        }

        currentOffset += nestedInfoBlockSize; // Move to next potential block
    }

    // Optional final check
    if (currentOffset != actualDataLimit && currentOffset < expectedTotalSize) { // Check against original expected size too
         spdlog::warn("AVCInfoBlock::parseInternal: Parsing finished at offset {}, but expected end was {}. Type=0x{:04X}", currentOffset, expectedTotalSize, type_);
    } else if (currentOffset > actualDataLimit) {
         spdlog::error("AVCInfoBlock::parseInternal: Parsing went beyond actual data size. Offset={}, ActualSize={}. Type=0x{:04X}", currentOffset, actualDataLimit, type_);
    }
}


//--------------------------------------------------------------------------
// AVCInfoBlock::toString()
//--------------------------------------------------------------------------
std::string AVCInfoBlock::toString(uint32_t indent) const {
    std::ostringstream oss;
    std::string indentStr(indent * 2, ' ');

    oss << indentStr << "AVCInfoBlock:\n";
    oss << indentStr << "  Type: 0x" << std::hex << std::setw(4) << std::setfill('0') << type_ << "\n";
    oss << indentStr << "  Compound Length: " << std::dec << compoundLength_ << " (Total Size: " << compoundLength_ + 2 << ")\n";
    oss << indentStr << "  Primary Fields Length: " << std::dec << primaryFieldsLength_ << "\n";

    // Print Primary Fields
    if (primaryFieldsLength_ > 0) {
        const uint8_t* pData = getPrimaryFieldsDataPtr();
        if (pData) {
             oss << indentStr << "  Primary Fields: ";
             parsePrimaryFields(pData, primaryFieldsLength_, oss); // Call specific parser helper
             oss << "\n";
        } else {
            oss << indentStr << "  Primary Fields: (Error: Invalid length or data size)\n";
        }
    }

    // Print Nested Blocks
    if (!nestedBlocks_.empty()) {
        oss << indentStr << "  Nested Blocks (" << nestedBlocks_.size() << "):\n";
        for (const auto& nested : nestedBlocks_) {
            oss << nested->toString(indent + 1);
        }
    }

    return oss.str();
}

//--------------------------------------------------------------------------
// AVCInfoBlock::parsePrimaryFields() - Populates parsed optional members
//--------------------------------------------------------------------------
void AVCInfoBlock::parsePrimaryFields() {
    const uint8_t* primaryData = getPrimaryFieldsDataPtr();
    size_t length = primaryFieldsLength_;
    if (!primaryData || length == 0) {
        return;
    }
    switch(static_cast<InfoBlockType>(type_)) {
        case InfoBlockType::RawText: {
            std::string text(reinterpret_cast<const char*>(primaryData), length);
            parsed_rawText_ = std::move(text);
        } break;
        case InfoBlockType::Name:
            if (length >= 4) {
                 NameInfoData data;
                 data.nameDataReferenceType = primaryData[0];
                 data.nameDataAttributes = primaryData[1];
                 data.maximumNumberOfCharacters = (static_cast<uint16_t>(primaryData[2]) << 8) | primaryData[3];
                 parsed_nameInfo_ = data;
            } break;
        case InfoBlockType::GeneralMusicStatus:
            if (length >= 6) {
                GeneralMusicStatusData data;
                data.currentTransmitCapability = primaryData[0];
                data.currentReceiveCapability = primaryData[1];
                data.currentLatencyCapability = (static_cast<uint32_t>(primaryData[2]) << 24) |
                                                (static_cast<uint32_t>(primaryData[3]) << 16) |
                                                (static_cast<uint32_t>(primaryData[4]) << 8) |
                                                primaryData[5];
                parsed_generalMusicStatus_ = data;
            } break;
        case InfoBlockType::MusicOutputPlugStatus:
            if (length >= 1) {
                parsed_musicOutputPlugSourceCount_ = primaryData[0];
            } break;
        case InfoBlockType::SourcePlugStatus:
            if (length >= 1) {
                parsed_sourcePlugNumber_ = primaryData[0];
            } break;
        case InfoBlockType::AudioInfo:
            if (length >= 1) {
                parsed_audioStreamCount_ = primaryData[0];
            } break;
        case InfoBlockType::MidiInfo:
            if (length >= 1) {
                parsed_midiStreamCount_ = primaryData[0];
            } break;
        case InfoBlockType::SmpteTimeCodeInfo:
            if (length >= 1) {
                parsed_smpteActivity_ = primaryData[0];
            } break;
        case InfoBlockType::SampleCountInfo:
            if (length >= 1) {
                parsed_sampleCountActivity_ = primaryData[0];
            } break;
        case InfoBlockType::AudioSyncInfo:
            if (length >= 1) {
                parsed_audioSyncActivity_ = primaryData[0];
            } break;
        case InfoBlockType::RoutingStatus:
            if (length >= 4) {
                RoutingStatusData data;
                data.numberOfSubunitDestPlugs = primaryData[0];
                data.numberOfSubunitSourcePlugs = primaryData[1];
                data.numberOfMusicPlugs = (static_cast<uint16_t>(primaryData[2]) << 8) | primaryData[3];
                parsed_routingStatus_ = data;
            } break;
        case InfoBlockType::SubunitPlugInfo:
            if (length >= 8) {
                SubunitPlugInfoData data;
                data.subunitPlugId = primaryData[0];
                data.signalFormat = (static_cast<uint16_t>(primaryData[1]) << 8) | primaryData[2];
                data.plugType = primaryData[3];
                data.numberOfClusters = (static_cast<uint16_t>(primaryData[4]) << 8) | primaryData[5];
                data.numberOfChannels = (static_cast<uint16_t>(primaryData[6]) << 8) | primaryData[7];
                parsed_subunitPlugInfo_ = data;
            } break;
        case InfoBlockType::ClusterInfo:
            if (length >= 3) {
                ClusterInfoData data;
                data.streamFormat = primaryData[0];
                data.portType = primaryData[1];
                data.numberOfSignals = primaryData[2];
                size_t expectedMinLength = 3 + (data.numberOfSignals * 4);
                if (length >= expectedMinLength) {
                    data.signals.reserve(data.numberOfSignals);
                    for (uint8_t i = 0; i < data.numberOfSignals; ++i) {
                        size_t sigOffset = 3 + (i * 4);
                        ClusterSignalInfo sig;
                        sig.musicPlugId = (static_cast<uint16_t>(primaryData[sigOffset]) << 8) | primaryData[sigOffset + 1];
                        sig.streamPosition = primaryData[sigOffset + 2];
                        sig.streamLocation = primaryData[sigOffset + 3];
                        data.signals.push_back(sig);
                    }
                }
                parsed_clusterInfo_ = data;
            } break;
        case InfoBlockType::MusicPlugInfo:
            if (length >= 14) {
                MusicPlugInfoData data;
                data.musicPlugType = primaryData[0];
                data.musicPlugId = (static_cast<uint16_t>(primaryData[1]) << 8) | primaryData[2];
                data.routingSupport = primaryData[3];
                data.source.plugFunctionType = primaryData[4];
                data.source.plugId = primaryData[5];
                data.source.plugFunctionBlockId = primaryData[6];
                data.source.streamPosition = primaryData[7];
                data.source.streamLocation = primaryData[8];
                data.destination.plugFunctionType = primaryData[9];
                data.destination.plugId = primaryData[10];
                data.destination.plugFunctionBlockId = primaryData[11];
                data.destination.streamPosition = primaryData[12];
                data.destination.streamLocation = primaryData[13];
                parsed_musicPlugInfo_ = data;
            } break;
        default:
            break;
    }
}

// Legacy presentation helper for toString:
void AVCInfoBlock::parsePrimaryFields(const uint8_t* primaryData, size_t length, std::ostringstream& oss) const {
    oss << formatHex(primaryData, length);
    // (existing presentation logic follows...)
}
    // Default: just print hex dump for unknown/unparsed types
    oss << formatHex(primaryData, length);

    // --- Add specific parsing based on type_ here ---
    switch(static_cast<InfoBlockType>(type_)) {
        case InfoBlockType::AudioInfo: // 0x8103
            if (length >= 1) {
                 oss << " (AudioInfo: NumAudioStreams=" << std::dec << static_cast<int>(primaryData[0]) << ")";
            } else {
                 oss << " (AudioInfo: Malformed - length=" << length << ")";
            }
            break;
        case InfoBlockType::MidiInfo: // 0x8104
            if (length >= 1) {
                 oss << " (MIDIInfo: NumMIDIStreams=" << std::dec << static_cast<int>(primaryData[0]) << ")";
            } else {
                 oss << " (MIDIInfo: Malformed - length=" << length << ")";
            }
            break;
        case InfoBlockType::SmpteTimeCodeInfo: // 0x8105
            if (length >= 1) {
                 oss << " (SMPTEInfo: Activity=0x" << std::hex << static_cast<int>(primaryData[0]) << ")";
            } else {
                 oss << " (SMPTEInfo: Malformed - length=" << length << ")";
            }
            break;
        case InfoBlockType::SampleCountInfo: // 0x8106
            if (length >= 1) {
                 oss << " (SampleCountInfo: Activity=0x" << std::hex << static_cast<int>(primaryData[0]) << ")";
            } else {
                 oss << " (SampleCountInfo: Malformed - length=" << length << ")";
            }
            break;
        case InfoBlockType::AudioSyncInfo: // 0x8107
            if (length >= 1) {
                 oss << " (AudioSyncInfo: Activity=0x" << std::hex << static_cast<int>(primaryData[0]) << ")";
            } else {
                 oss << " (AudioSyncInfo: Malformed - length=" << length << ")";
            }
            break;
        case InfoBlockType::RoutingStatus: // 0x8108
            if (length >= 4) {
                uint16_t numMusicPlugs = (static_cast<uint16_t>(primaryData[2]) << 8) | primaryData[3];
                oss << " (RoutingStatus: NumSubunitDest=" << std::dec << static_cast<int>(primaryData[0])
                    << ", NumSubunitSrc=" << static_cast<int>(primaryData[1])
                    << ", NumMusicPlugs=" << numMusicPlugs << ")";
            } else {
                 oss << " (RoutingStatus: Malformed - length=" << length << ")";
            }
            break;
        case InfoBlockType::SubunitPlugInfo: // 0x8109
            if (length >= 8) {
                uint16_t signalFormat = (static_cast<uint16_t>(primaryData[1]) << 8) | primaryData[2];
                uint16_t numClusters = (static_cast<uint16_t>(primaryData[4]) << 8) | primaryData[5];
                uint16_t numChannels = (static_cast<uint16_t>(primaryData[6]) << 8) | primaryData[7];
                oss << " (SubunitPlugInfo: PlugID=" << std::dec << static_cast<int>(primaryData[0])
                    << ", SigFmt=0x" << std::hex << signalFormat
                    << ", PlugType=0x" << static_cast<int>(primaryData[3])
                    << ", NumClusters=" << std::dec << numClusters
                    << ", NumChans=" << numChannels << ")";
            } else {
                 oss << " (SubunitPlugInfo: Malformed - length=" << length << ")";
            }
            break;
        case InfoBlockType::ClusterInfo: // 0x810A
            if (length >= 3) {
                 uint8_t numSignals = primaryData[2];
                 size_t expectedMinLength = 3 + (numSignals * 4);
                 oss << " (ClusterInfo: StreamFmt=0x" << std::hex << static_cast<int>(primaryData[0])
                     << ", PortType=0x" << static_cast<int>(primaryData[1])
                     << ", NumSignals=" << std::dec << static_cast<int>(numSignals);
                 if (length >= expectedMinLength) {
                     oss << " Signals:[";
                     for (uint8_t i = 0; i < numSignals; ++i) {
                         size_t sigOffset = 3 + (i * 4);
                         uint16_t musicPlugId = (static_cast<uint16_t>(primaryData[sigOffset]) << 8) | primaryData[sigOffset + 1];
                         oss << (i > 0 ? ", " : "")
                             << "{MusicPlug=0x" << std::hex << musicPlugId
                             << ", Pos=" << std::dec << static_cast<int>(primaryData[sigOffset + 2])
                             << ", Loc=" << static_cast<int>(primaryData[sigOffset + 3]) << "}";
                     }
                     oss << "]";
                 } else {
                      oss << " MalformedSignalData - length=" << length << ", needed=" << expectedMinLength;
                 }
                  oss << ")";
            } else {
                 oss << " (ClusterInfo: Malformed - length=" << length << ")";
            }
            break;
        case InfoBlockType::MusicPlugInfo: // 0x810B
            if (length >= 14) {
                 uint16_t musicPlugId = (static_cast<uint16_t>(primaryData[1]) << 8) | primaryData[2];
                 oss << " (MusicPlugInfo: Type=0x" << std::hex << static_cast<int>(primaryData[0])
                     << ", ID=0x" << musicPlugId
                     << ", Routing=0x" << static_cast<int>(primaryData[3])
                     << " Src:[FuncType=0x" << static_cast<int>(primaryData[4])
                     << ", PlugID=0x" << static_cast<int>(primaryData[5])
                     << ", BlockID=0x" << static_cast<int>(primaryData[6])
                     << ", StrPos=" << std::dec << static_cast<int>(primaryData[7])
                     << ", StrLoc=" << static_cast<int>(primaryData[8]) << "]"
                     << " Dst:[FuncType=0x" << std::hex << static_cast<int>(primaryData[9])
                     << ", PlugID=0x" << static_cast<int>(primaryData[10])
                     << ", BlockID=0x" << static_cast<int>(primaryData[11])
                     << ", StrPos=" << std::dec << static_cast<int>(primaryData[12])
                     << ", StrLoc=" << static_cast<int>(primaryData[13]) << "]"
                     << ")";
            } else {
                 oss << " (MusicPlugInfo: Malformed - length=" << length << ")";
            }
            break;
        case InfoBlockType::RawText:
             oss << " (Raw Text: \"";
             for (size_t i = 0; i < length; ++i) {
                 char c = primaryData[i];
                 oss << (isprint(c) ? c : '.');
             }
             oss << "\")";
             break;

        case InfoBlockType::Name:
            if (length >= 4) {
                 uint8_t refType = primaryData[0];
                 uint8_t attributes = primaryData[1];
                 uint16_t maxChars = (static_cast<uint16_t>(primaryData[2]) << 8) | primaryData[3];
                 oss << " (Name: RefType=0x" << std::hex << static_cast<int>(refType)
                     << ", Attrs=0x" << static_cast<int>(attributes)
                     << ", MaxChars=" << std::dec << maxChars << ")";
                 // Note: Nested blocks within the name block's primary fields (if any)
                 // would need separate handling here if the structure demands it,
                 // but typically they appear in the secondary fields area.
            } else {
                 oss << " (Name: Malformed - length=" << length << ")";
            }
             break;

        // --- Music Subunit Specific ---
        case InfoBlockType::GeneralMusicStatus:
             if (length >= 6) {
                 oss << " (GenMusicStatus: TxCap=0x" << std::hex << static_cast<int>(primaryData[0])
                     << ", RxCap=0x" << static_cast<int>(primaryData[1])
                     << ", LatCap=0x" << static_cast<int>(primaryData[2]) << static_cast<int>(primaryData[3])
                                       << static_cast<int>(primaryData[4]) << static_cast<int>(primaryData[5]) << ")";
             } else {
                 oss << " (GenMusicStatus: Malformed - length=" << length << ")";
             }
            break;
        case InfoBlockType::MusicOutputPlugStatus:
            if (length >= 1) {
                 oss << " (MusicOutPlugStatus: NumSrcPlugs=" << std::dec << static_cast<int>(primaryData[0]) << ")";
            } else {
                 oss << " (MusicOutPlugStatus: Malformed - length=" << length << ")";
            }
            break;
        case InfoBlockType::SourcePlugStatus:
             if (length >= 1) {
                 oss << " (SrcPlugStatus: SrcPlugNum=" << std::dec << static_cast<int>(primaryData[0]) << ")";
             } else {
                 oss << " (SrcPlugStatus: Malformed - length=" << length << ")";
             }
            break;
        // Add cases for 0x8103 through 0x810B based on TA 2001007 if needed...

        default:
            // No specific parsing for this type, hex dump already provided.
            break;
    }
}

//--------------------------------------------------------------------------
// AVCInfoBlock::formatHex() - Static Helper
//--------------------------------------------------------------------------
std::string AVCInfoBlock::formatHex(const uint8_t* data, size_t length) {
     std::ostringstream oss;
     oss << std::hex << std::uppercase << std::setfill('0');
     for (size_t i = 0; i < length; ++i) {
         oss << std::setw(2) << static_cast<int>(data[i]) << (((i + 1) % 16 == 0 && i + 1 < length) ? "\n    " : " "); // Newline every 16 bytes for readability
     }
     return oss.str();
 }


} // namespace FWA