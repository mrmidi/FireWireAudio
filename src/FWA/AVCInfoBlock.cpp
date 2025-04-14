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
// AVCInfoBlock::parsePrimaryFields() - Placeholder/Example
//--------------------------------------------------------------------------
void AVCInfoBlock::parsePrimaryFields(const uint8_t* primaryData, size_t length, std::ostringstream& oss) const {
    // Default: just print hex dump for unknown/unparsed types
    oss << formatHex(primaryData, length);

    // --- Add specific parsing based on type_ here ---
    switch(static_cast<InfoBlockType>(type_)) {
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