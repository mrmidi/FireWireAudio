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
// AVCInfoBlock Constructor (from movable r-value - likely used by make_shared)
//--------------------------------------------------------------------------
AVCInfoBlock::AVCInfoBlock(std::vector<uint8_t> rawData)
  : rawData_(std::move(rawData)) // Take ownership via move
{
    // --- Perform parsing directly ---
    if (rawData_.size() < 4) {
        spdlog::error("AVCInfoBlock (move ctor): Insufficient raw data ({} bytes). Needs at least 4.", rawData_.size());
        type_ = static_cast<InfoBlockType>(0xFFFF); // Mark as unknown
        compoundLength_ = 0;
        primaryFieldsLength_ = 0;
        return;
    }
    compoundLength_ = (static_cast<uint16_t>(rawData_[0]) << 8) | rawData_[1];
    type_ = static_cast<InfoBlockType>((static_cast<uint16_t>(rawData_[2]) << 8) | rawData_[3]);
    if (rawData_.size() >= 6) {
        primaryFieldsLength_ = (static_cast<uint16_t>(rawData_[4]) << 8) | rawData_[5];
    } else {
        primaryFieldsLength_ = 0;
    }
    spdlog::debug("AVCInfoBlock (move ctor): Parsed header. Type=0x{:04X}, CompoundLength={}, PrimaryFieldsLength={}",
               static_cast<uint16_t>(type_), compoundLength_, primaryFieldsLength_);

    // Now parse primary fields and nested blocks based on parsed header
    parse(); // Call the separate parse method
}

//--------------------------------------------------------------------------
// AVCInfoBlock::parse() - Renamed from parseInternal
//--------------------------------------------------------------------------
void AVCInfoBlock::parse() {
    // DEBUGGING INFO
        spdlog::debug("AVCInfoBlock::parse: Instance Data Check: Size={}, Type=0x{:04X}, CompLen={}, PrimLen={}",
                  rawData_.size(),
                  static_cast<uint16_t>(type_),
                  compoundLength_,
                  primaryFieldsLength_);
    size_t previewLen = std::min(rawData_.size(), static_cast<size_t>(20));
    // BP HERE
    if (previewLen > 0) { // Only create preview if there's data
        std::vector<uint8_t> preview(rawData_.begin(), rawData_.begin() + previewLen);
        spdlog::debug("AVCInfoBlock::parse: Instance Preview (first {}): {}", previewLen, Helpers::formatHexBytes(preview));
    } else {
        spdlog::debug("AVCInfoBlock::parse: Instance Preview: (No data)");
    }

    spdlog::debug("AVCInfoBlock::parse: Starting parse for Type=0x{:04X}, CompLen={}, PrimLen={}",
               static_cast<uint16_t>(type_), compoundLength_, primaryFieldsLength_);
    nestedBlocks_.clear();

    if (type_ == static_cast<InfoBlockType>(0xFFFF)) {
         spdlog::warn("AVCInfoBlock::parse: Skipping parse due to invalid initial state (likely from construction error).");
         return;
    }

    size_t actualDataLimit = rawData_.size(); // The actual size of the buffer we *have* for this block
    size_t expectedTotalSizeBasedOnHeader = compoundLength_ + 2;

    // Validate primaryFieldsLength against the actual data we have for THIS block
    if (actualDataLimit < 6) {
        if (primaryFieldsLength_ > 0) {
            spdlog::warn("AVCInfoBlock::parse: primaryFieldsLength_ is {} but only {} bytes available. Resetting primaryFieldsLength_ to 0. Type=0x{:04X}",
                         primaryFieldsLength_, actualDataLimit, static_cast<uint16_t>(type_));
            primaryFieldsLength_ = 0;
        }
    } else if (6 + primaryFieldsLength_ > actualDataLimit) {
        spdlog::warn("AVCInfoBlock::parse: Claimed primary fields length ({}) exceeds available data ({} bytes total). Parsing only available primary fields. Type=0x{:04X}",
                     primaryFieldsLength_, actualDataLimit, static_cast<uint16_t>(type_));
        // Adjust effective length for parsing, keep original for comparison
        // primaryFieldsLength_ is kept as reported by header for logic below
    }

    size_t primaryFieldsEndOffset = std::min(static_cast<size_t>(6 + primaryFieldsLength_), actualDataLimit);
    spdlog::debug("  Parse Check: Primary fields data ends at effective offset {}", primaryFieldsEndOffset);

    // --- Parse Primary fields ---
    parsePrimaryFields(); // This should now internally respect boundaries via getPrimaryFieldsDataPtr()

    // --- Memory Check Log ---
    spdlog::debug("  Memory Check before nested loop: Size={}, Type=0x{:04X}", rawData_.size(), static_cast<uint16_t>(type_));
    size_t checkPreviewLen = std::min(rawData_.size(), static_cast<size_t>(30)); // Check more bytes
    if (checkPreviewLen > 0) {
        std::vector<uint8_t> checkPreview(rawData_.begin(), rawData_.begin() + checkPreviewLen);
        spdlog::debug("  Memory Check Preview (first {}): {}", checkPreviewLen, Helpers::formatHexBytes(checkPreview));
    }
    // --- End Memory Check ---

    // --- Parse Nested Blocks (Secondary Fields Area) ---
    size_t currentOffset = primaryFieldsEndOffset; // Start after primary fields
    spdlog::debug("  Starting nested block parse at offset {}", currentOffset);

    // Determine the effective limit for parsing nested blocks within *this* block
    size_t parseLimit = actualDataLimit; // Start with the actual buffer size
    if (expectedTotalSizeBasedOnHeader < actualDataLimit) {
         spdlog::warn("AVCInfoBlock::parse: This block's compound_length ({}) indicates total size {} which is *less* than available data ({}). Limiting nested parse to indicated length.",
                      compoundLength_, expectedTotalSizeBasedOnHeader, actualDataLimit);
         parseLimit = expectedTotalSizeBasedOnHeader;
    } else if (expectedTotalSizeBasedOnHeader > actualDataLimit) {
         spdlog::warn("AVCInfoBlock::parse: This block's compound_length ({}) indicates total size {} which *exceeds* available data ({}). Limiting nested parse to available data.",
                       compoundLength_, expectedTotalSizeBasedOnHeader, actualDataLimit);
         // parseLimit is already actualDataLimit
    }
    spdlog::debug("  Parsing nested blocks up to effective limit: {}", parseLimit);

    bool encounteredErrorInNested = false; // Track errors during nested parsing

    while (currentOffset < parseLimit) {
        size_t nestedBlockStartOffset = currentOffset;

        if (currentOffset + 4 > parseLimit) {
            // Don't log as error if it's just the normal end of parsing
            if (currentOffset != parseLimit) {
                spdlog::warn("  Not enough bytes ({}) left for nested block header at offset {}. Stopping nested parse within Type=0x{:04X}.",
                             parseLimit - currentOffset, currentOffset, static_cast<uint16_t>(type_));
            }
            break; // Normal or truncated end
        }
        // BP HERE
        uint16_t nestedCompoundLength = (static_cast<uint16_t>(rawData_[currentOffset]) << 8) | rawData_[currentOffset + 1];
        size_t claimedNestedBlockSize = nestedCompoundLength + 2;
        size_t availableBytesForNestedBlock = parseLimit - currentOffset;


        // --- LOG DEBUGGING INFO ---
        spdlog::debug("  Nested loop: currentOffset={}, Reading header bytes rawData_[{}] = 0x{:02X}, rawData_[{}] = 0x{:02X}",
              currentOffset,
              currentOffset, rawData_[currentOffset],
              currentOffset + 1, rawData_[currentOffset + 1]);

        // --- Check for invalid claimed size ---
        if (claimedNestedBlockSize < 4 || nestedCompoundLength == 0xFFFF) {
            spdlog::error("AVCInfoBlock::parse: Invalid nested block size ({}, compound_length=0x{:04X}) reported by block at offset {} within Type=0x{:04X}. Stopping nested parse for this parent.",
                          claimedNestedBlockSize, nestedCompoundLength, nestedBlockStartOffset, static_cast<uint16_t>(type_));
            encounteredErrorInNested = true;
        
            // break; // Stop parsing nested blocks for this parent
        }

        size_t bytesToParseForNestedBlock = std::min(claimedNestedBlockSize, availableBytesForNestedBlock);
        bool wasTruncated = (claimedNestedBlockSize > availableBytesForNestedBlock);

        if (wasTruncated) {
            spdlog::warn("AVCInfoBlock::parse: Nested block at offset {} (within Type=0x{:04X}) claims size {} ({}+2), exceeding remaining {} bytes. Parsing only available {} bytes.",
                         nestedBlockStartOffset, static_cast<uint16_t>(type_), claimedNestedBlockSize, nestedCompoundLength, availableBytesForNestedBlock, bytesToParseForNestedBlock);
             if (bytesToParseForNestedBlock < 4) {
                 spdlog::error("AVCInfoBlock::parse: Not enough available bytes ({}) to parse even the header of the oversized nested block at offset {}. Stopping nested parse.", bytesToParseForNestedBlock, nestedBlockStartOffset);
                 encounteredErrorInNested = true;
                 break; // Stop parsing nested blocks for this parent
             }
            encounteredErrorInNested = true; // Mark that this block had issues
        }

        // --- Manual Copy Instead of Slice Constructor ---
        std::vector<uint8_t> nestedRawData;
        nestedRawData.reserve(bytesToParseForNestedBlock); // Pre-allocate
        spdlog::debug("  Parent Parse: Manually copying {} bytes starting at parent offset {}", bytesToParseForNestedBlock, nestedBlockStartOffset);
        for (size_t i = 0; i < bytesToParseForNestedBlock; ++i) {
            if (nestedBlockStartOffset + i >= rawData_.size()) {
                spdlog::error("  Parent Parse: Manual copy boundary error! i={}, offset={}, rawData_.size()={}", i, nestedBlockStartOffset, rawData_.size());
                break;
            }
            nestedRawData.push_back(rawData_[nestedBlockStartOffset + i]);
        }
        // --- End Manual Copy ---

        // Log the manually copied data
        spdlog::debug("  Parent Parse: Verifying MANUAL nestedRawData before move (Size={})", nestedRawData.size());
        size_t nestedPreviewLen = std::min(nestedRawData.size(), static_cast<size_t>(20));
        if (nestedPreviewLen > 0) {
            std::vector<uint8_t> nestedPreview(nestedRawData.begin(), nestedRawData.begin() + nestedPreviewLen);
            spdlog::debug("  Parent Parse: MANUAL nestedRawData Preview (first {}): {}", nestedPreviewLen, Helpers::formatHexBytes(nestedPreview));
        }

        try {
            nestedBlocks_.push_back(std::make_shared<AVCInfoBlock>(std::move(nestedRawData)));
            uint16_t nestedType = static_cast<uint16_t>(nestedBlocks_.back()->getType());
            spdlog::debug("    Successfully created nested block (Type=0x{:04X}, Parsed Size: {}) from parent offset {}",
                          nestedType, bytesToParseForNestedBlock, nestedBlockStartOffset);
        } catch (const std::exception& e) {
            spdlog::error("AVCInfoBlock::parse: Exception creating nested block from slice at offset {}: {}", nestedBlockStartOffset, e.what());
            encounteredErrorInNested = true;
            currentOffset += bytesToParseForNestedBlock;
            spdlog::warn("AVCInfoBlock::parse: Attempting to continue after exception by skipping {} bytes.", bytesToParseForNestedBlock);
            continue; // Try next potential sibling
        } catch (...) {
             spdlog::error("AVCInfoBlock::parse: Unknown exception constructing/parsing nested block from slice at offset {}.", nestedBlockStartOffset);
             encounteredErrorInNested = true;
             currentOffset += bytesToParseForNestedBlock;
             spdlog::warn("AVCInfoBlock::parse: Attempting to continue after unknown exception by skipping {} bytes.", bytesToParseForNestedBlock);
             continue; // Try next potential sibling
        }

        // --- Advancement Logic ---
        // Advance offset based on the size parsed/available for *this* nested block
        currentOffset += bytesToParseForNestedBlock;

        // If the nested block *claimed* more than was available, stop processing siblings.
        if (wasTruncated) {
            spdlog::warn("AVCInfoBlock::parse: Stopping parsing further nested blocks within Type=0x{:04X} because nested block at offset {} was truncated (claimed {}, parsed {}).",
                         static_cast<uint16_t>(type_), nestedBlockStartOffset, claimedNestedBlockSize, bytesToParseForNestedBlock);
            break; // Cannot reliably find next sibling
        }
    } // End while loop

    // Final logging checks
    if (currentOffset < parseLimit && !encounteredErrorInNested) {
         spdlog::warn("AVCInfoBlock::parse: Nested parsing loop for Type=0x{:04X} finished unexpectedly at offset {} (expected end {}).",
                      static_cast<uint16_t>(type_), currentOffset, parseLimit);
    } else if (currentOffset > parseLimit) {
        spdlog::error("AVCInfoBlock::parse: Nested parsing logic error: currentOffset {} somehow exceeded parseLimit {}.",
                      currentOffset, parseLimit);
    } else {
        spdlog::debug("  Finished nested block parse loop for Type=0x{:04X}. Final offset: {}", static_cast<uint16_t>(type_), currentOffset);
    }
}


//--------------------------------------------------------------------------
// AVCInfoBlock::toString()
//--------------------------------------------------------------------------
std::string AVCInfoBlock::toString(uint32_t indent) const {
    std::ostringstream oss;
    std::string indentStr(indent * 2, ' ');

    oss << indentStr << "AVCInfoBlock:\n";
    oss << indentStr << "  Type: 0x" << std::hex << std::setw(4) << std::setfill('0') << static_cast<uint16_t>(type_) << "\n";
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
            spdlog::debug("AVCInfoBlock::parsePrimaryFields: Entering Parse SubunitPlugInfo (0x8109) with length {}", length);
            if (length >= 8) {
                SubunitPlugInfoData data;
                data.subunitPlugId = primaryData[0];
                data.signalFormat = (static_cast<uint16_t>(primaryData[1]) << 8) | primaryData[2];
                data.plugType = primaryData[3];
                data.numberOfClusters = (static_cast<uint16_t>(primaryData[4]) << 8) | primaryData[5];
                data.numberOfChannels = (static_cast<uint16_t>(primaryData[6]) << 8) | primaryData[7];
                parsed_subunitPlugInfo_ = data;
                // Debugging output
                spdlog::debug("AVCInfoBlock::parsePrimaryFields: Parsed SubunitPlugInfo: PlugId=0x{:02X}, SignalFormat=0x{:04X}, PlugType=0x{:02X}, Clusters={}, Channels={}",
                              data.subunitPlugId, data.signalFormat, data.plugType, data.numberOfClusters, data.numberOfChannels);
                // RAW hex dump of the data
                spdlog::debug("AVCInfoBlock::parsePrimaryFields: Raw data: {}", formatHex(primaryData, length));
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
                     << ", BlockID=0x" << static_cast<uint16_t>(primaryData[11])
                     << ", StrPos=" << std::dec << static_cast<int>(primaryData[12])
                     << ", StrLoc=" << static_cast<int>(primaryData[13]) << "]"
                     << ")";
            } else {
                 oss << " (MusicPlugInfo: Malformed - length=" << length << ")";
            }
            break;
        default:
            // No specific parsing for this type, hex dump already provided.
            break;
    }
}

//--------------------------------------------------------------------------
// AVCInfoBlock::formatHex() - Static Helper
//--------------------------------------------------------------------------
std::string AVCInfoBlock::formatHex(const uint8_t* data, size_t length) const {
     std::ostringstream oss;
     oss << std::hex << std::uppercase << std::setfill('0');
     for (size_t i = 0; i < length; ++i) {
         oss << std::setw(2) << static_cast<int>(data[i]) << (((i + 1) % 16 == 0 && i + 1 < length) ? "\n    " : " "); // Newline every 16 bytes for readability
     }
     return oss.str();
 }


} // namespace FWA