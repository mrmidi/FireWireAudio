// MusicSubunitDescriptorParser.cpp
#include "FWA/MusicSubunitDescriptorParser.hpp"
#include "FWA/DescriptorAccessor.hpp"
#include "FWA/DescriptorUtils.hpp"
#include "FWA/MusicSubunit.hpp"
#include "FWA/Helpers.h"
#include <spdlog/spdlog.h>

namespace FWA {

MusicSubunitDescriptorParser::MusicSubunitDescriptorParser(DescriptorAccessor& descriptorAccessor)
    : descriptorAccessor_(descriptorAccessor)
{}

std::expected<void,IOKitError> MusicSubunitDescriptorParser::fetchAndParse(MusicSubunit& musicSubunit) {
    uint8_t subunitAddr = Helpers::getSubunitAddress(SubunitType::Music, musicSubunit.getId());
    // log entry
    spdlog::debug("MusicSubunitDescriptorParser: Fetching and parsing Music Subunit Descriptor for subunit address 0x{:02x}", subunitAddr);
    // ---- FIX: pull the real sizes from the accessor ----
    auto listIdSize     = descriptorAccessor_.getSizeOfListID();
    auto objectIdSize   = descriptorAccessor_.getSizeOfObjectID();
    auto positionSize   = descriptorAccessor_.getSizeOfObjectPosition();

    // ---- FIX: use the Identifier descriptor (0x00) specifier ----
    auto idDescSpecifier = DescriptorUtils::buildDescriptorSpecifier(
        DescriptorSpecifierType::UnitSubunitIdentifier,  // <–– 0x00
        listIdSize,
        objectIdSize,
        positionSize
    );

    if (idDescSpecifier.empty()) {
        spdlog::error("MusicSubunitDescriptorParser: failed to build IDENTIFIER specifier");
        return std::unexpected(IOKitError::InternalError);
    }

    auto descriptorDataResult = descriptorAccessor_.read(
        subunitAddr,
        idDescSpecifier,
        0,  // offset 0
        0   // length 0 = read the whole descriptor
    );

    if (!descriptorDataResult) {
        spdlog::warn("…GET_DESCRIPTOR failed: 0x{:02x}", int(descriptorDataResult.error()));
        if (descriptorDataResult.error() == IOKitError::Unsupported) {
            // it just doesn’t know how to talk our AV/C; not fatal
            return {};
        }
        return std::unexpected(descriptorDataResult.error());
    }

    // Now parse the *Identifier* descriptor, which contains 
    // music_subunit_dependent_information → general_capability.
    return this->parseMusicSubunitIdentifierDescriptor(
        descriptorDataResult.value(), 
        musicSubunit
    );
}

std::expected<void,IOKitError> MusicSubunitDescriptorParser::parseMusicSubunitIdentifierDescriptor(
    const std::vector<uint8_t>& descriptorData,
    MusicSubunit& musicSubunit)
{
    spdlog::debug("MSDP: Parsing Music Subunit Identifier Descriptor, {} bytes", descriptorData.size());
    if (descriptorData.size() < 8) {
        spdlog::error("MSDP: Identifier descriptor too short ({} bytes)", descriptorData.size());
        return std::unexpected(IOKitError::BadArgument);
    }
    size_t offset = 0;
    // Read descriptor_length
    uint16_t descriptorLength = static_cast<uint16_t>(descriptorData[offset]) << 8 |
                                descriptorData[offset+1];
    offset += 2;
    // Read generation_ID
    uint8_t genId = descriptorData[offset++];
    // Sizes
    uint8_t sizeListId = descriptorData[offset++];
    uint8_t sizeObjId  = descriptorData[offset++];
    uint8_t sizePos    = descriptorData[offset++];
    // Number of root lists
    uint16_t numRoots = static_cast<uint16_t>(descriptorData[offset]) << 8 |
                        descriptorData[offset+1];
    offset += 2;
    // Skip root_list_IDs
    offset += static_cast<size_t>(numRoots) * sizeListId;
    if (offset + 2 > descriptorData.size()) {
        spdlog::error("MSDP: Identifier descriptor missing dependent info length");
        return std::unexpected(IOKitError::BadArgument);
    }
    // Dependent info length
    uint16_t depLen = static_cast<uint16_t>(descriptorData[offset]) << 8 |
                      descriptorData[offset+1];
    offset += 2;
    if (offset + depLen > descriptorData.size()) {
        spdlog::error("MSDP: Dependent info length ({}) exceeds descriptor size ({})", depLen, descriptorData.size());
        return std::unexpected(IOKitError::BadArgument);
    }
    // Slice out the type-dependent info
    std::vector<uint8_t> depInfo(
        descriptorData.begin() + offset,
        descriptorData.begin() + offset + depLen
    );
    // TODO: parse depInfo into musicSubunit capabilities
    spdlog::warn("MSDP: parseMusicSubunitIdentifierDescriptor is not fully implemented");
    return {};
}

std::expected<void, IOKitError> MusicSubunitDescriptorParser::parseMusicSubunitStatusDescriptor(
    const std::vector<uint8_t>& descriptorData,
    MusicSubunit& musicSubunit)
{
    spdlog::debug("MusicSubunitDescriptorParser: Parsing Music Subunit Status Descriptor content, {} bytes received", descriptorData.size());
    musicSubunit.clearParsedStatusInfoBlocks();
    musicSubunit.setStatusDescriptorData(descriptorData); // Store raw data

    if (descriptorData.size() < 2) {
        spdlog::warn("MusicSubunitDescriptorParser: Descriptor data too short ({}) for info block length field.", descriptorData.size());
        return {}; // Nothing to parse
    }

    // Read the reported total length from header bytes 0-1
    uint16_t reportedTotalInfoBlocksLength = (static_cast<uint16_t>(descriptorData[0]) << 8) | descriptorData[1];
    spdlog::debug("MusicSubunitDescriptorParser: Reported total info block length (from bytes 0-1): {}", reportedTotalInfoBlocksLength);

    size_t infoBlockStartOffset = 2;
    size_t actualDataSize = descriptorData.size();
    size_t expectedTotalSizeBasedOnHeader = infoBlockStartOffset + reportedTotalInfoBlocksLength;

    // --- MODIFICATION: Always parse the full extent of the actual received data ---
    size_t effectiveEndOffset = actualDataSize;
    // --- END MODIFICATION ---

    // Log if the reported length differs from the actual received length, explaining the parse strategy
    if (expectedTotalSizeBasedOnHeader != actualDataSize) {
        spdlog::warn("MusicSubunitDescriptorParser: Header reported total info block length ({}) implies total size {}, but actual received size is {}. PARSING BASED ON ACTUAL RECEIVED SIZE ({} bytes).",
                     reportedTotalInfoBlocksLength,
                     expectedTotalSizeBasedOnHeader,
                     actualDataSize,
                     actualDataSize); // Explicitly state we use actualDataSize
    }

    // Check if the parsing area is valid after setting the end offset
    if (infoBlockStartOffset >= effectiveEndOffset) {
          spdlog::warn("MusicSubunitDescriptorParser: Calculated info block area is empty or invalid (start={}, end={}). No info blocks to parse.", infoBlockStartOffset, effectiveEndOffset);
          return {};
    }

    spdlog::debug("MusicSubunitDescriptorParser: Parsing info blocks from offset {} up to effective offset {}", infoBlockStartOffset, effectiveEndOffset);
    size_t currentOffset = infoBlockStartOffset;
    bool encounteredErrorInBlock = false; // Flag to track if we had issues

    while (currentOffset < effectiveEndOffset) {
        size_t blockStartOffset = currentOffset;
        encounteredErrorInBlock = false;

        // Check if there's enough data for at least the minimal header (4 bytes)
        if (currentOffset + 4 > effectiveEndOffset) {
            // Don't treat leftover bytes less than a header size as an error necessarily, just stop.
            if(currentOffset != effectiveEndOffset) { // Only log if there were actually leftover bytes
                 spdlog::warn("MusicSubunitDescriptorParser: Insufficient data remaining ({}) for minimal info block header at offset {}. Stopping info block parse.",
                              effectiveEndOffset - currentOffset, currentOffset);
            }
            break; // Stop parsing this container
        }

        // Read the compound length and calculate total size *claimed* by this block
        uint16_t blockCompoundLength = (static_cast<uint16_t>(descriptorData[currentOffset]) << 8) | descriptorData[currentOffset + 1];
        size_t claimedBlockTotalSize = blockCompoundLength + 2;
        size_t availableBytesForBlock = effectiveEndOffset - currentOffset;

        // --- Check for invalid claimed sizes ---
        if (claimedBlockTotalSize < 4 || blockCompoundLength == 0xFFFF) {
             spdlog::error("MusicSubunitDescriptorParser: Invalid block size ({}, compound_length=0x{:04X}) reported by block at offset {}. Attempting to skip minimum (4 bytes) and continue.",
                           claimedBlockTotalSize, blockCompoundLength, blockStartOffset);
             currentOffset += 4; // Try skipping minimal header
             encounteredErrorInBlock = true; // Mark error occurred
             continue; // Try next potential block
        }

        // Determine bytes to parse for this block, clamping to available data
        size_t bytesToParseForThisBlock = std::min(claimedBlockTotalSize, availableBytesForBlock);
        bool wasTruncated = (claimedBlockTotalSize > availableBytesForBlock);

        if (wasTruncated) {
            spdlog::warn("MusicSubunitDescriptorParser: Info block at offset {} claims size {} ({}+2), exceeding remaining {} bytes in descriptor. Parsing only available {} bytes.",
                         blockStartOffset, claimedBlockTotalSize, blockCompoundLength, availableBytesForBlock, bytesToParseForThisBlock);
             if (bytesToParseForThisBlock < 4) {
                 spdlog::error("MusicSubunitDescriptorParser: Not enough available bytes ({}) to parse even the header of the oversized block at offset {}. Stopping.", bytesToParseForThisBlock, blockStartOffset);
                 encounteredErrorInBlock = true; // Mark error
                 break; // Cannot continue parsing top-level blocks
             }
            // Note: Even if truncated, we still attempt to parse the available 'bytesToParseForThisBlock'.
             encounteredErrorInBlock = true; // Indicate something was unusual
        }

        // Extract the slice using the calculated (potentially clamped) size
        // BP HERE
        auto blockStartIter = descriptorData.begin() + blockStartOffset;
        auto blockEndIter = blockStartIter + bytesToParseForThisBlock; // Use clamped size
        std::vector<uint8_t> blockDataSlice(blockStartIter, blockEndIter);

        // --- Log Slice Info ---
        uint16_t sliceType = (blockDataSlice.size() >= 4) ? (static_cast<uint16_t>(blockDataSlice[2]) << 8) | blockDataSlice[3] : 0xFFFF;
        spdlog::trace("MSDParser: Slice Created: DescOffset={}, SliceSize={}, ClaimedSize={}, Type=0x{:04X}, Truncated={}",
                      blockStartOffset,
                      blockDataSlice.size(),
                      claimedBlockTotalSize,
                      sliceType,
                      wasTruncated);
        size_t previewLen = std::min(blockDataSlice.size(), static_cast<size_t>(20));
        std::vector<uint8_t> preview(blockDataSlice.begin(), blockDataSlice.begin() + previewLen);
        spdlog::trace("MSDParser: Slice Preview (first {}): {}", previewLen, Helpers::formatHexBytes(preview));

        try {
            // Create the info block. Its internal parser will handle the slice.
            // BP HERE
            auto infoBlock = std::make_shared<AVCInfoBlock>(std::move(blockDataSlice));
            musicSubunit.addParsedStatusInfoBlock(infoBlock);
            spdlog::debug("MusicSubunitDescriptorParser: Added parsed info block (Type: 0x{:04X}, Parsed Size: {}) from descriptor offset {}",
                         static_cast<uint16_t>(infoBlock->getType()), bytesToParseForThisBlock, blockStartOffset);
        } catch (const std::exception& e) {
            spdlog::error("MusicSubunitDescriptorParser: Exception constructing/parsing info block from slice at offset {}: {}", blockStartOffset, e.what());
            encounteredErrorInBlock = true;
            // Use the size we *attempted* to parse for skipping
            currentOffset += bytesToParseForThisBlock;
            spdlog::warn("MusicSubunitDescriptorParser: Attempting to continue after exception by skipping {} bytes.", bytesToParseForThisBlock);
            continue; // Try the next block
        } catch (...) {
            spdlog::error("MusicSubunitDescriptorParser: Unknown exception constructing/parsing info block from slice at offset {}.", blockStartOffset);
            encounteredErrorInBlock = true;
            currentOffset += bytesToParseForThisBlock;
            spdlog::warn("MusicSubunitDescriptorParser: Attempting to continue after unknown exception by skipping {} bytes.", bytesToParseForThisBlock);
            continue; // Try the next block
        }

        // --- Advancement Logic ---
        // Advance offset based on the size we actually processed/parsed for this block.
        // If the block claimed more than available (wasTruncated), we MUST stop parsing
        // further top-level blocks because alignment is lost.
        if (wasTruncated) {
            spdlog::warn("MusicSubunitDescriptorParser: Stopping parsing further top-level blocks because block at offset {} was truncated (claimed {}, parsed {}).",
                          blockStartOffset, claimedBlockTotalSize, bytesToParseForThisBlock);
             currentOffset = effectiveEndOffset; // Force loop exit
        } else {
             // If not truncated, advance by the size this block claimed (which fit within available space)
            currentOffset += claimedBlockTotalSize;
        }

    } // End while loop

    // Final check - compare where parsing stopped vs. the effective boundary
    if (currentOffset < effectiveEndOffset && !encounteredErrorInBlock) {
        spdlog::warn("MusicSubunitDescriptorParser: Parsing loop finished before reaching end offset {} (stopped at {}).",
                     effectiveEndOffset, currentOffset);
    } else if (currentOffset > effectiveEndOffset) {
        spdlog::error("MusicSubunitDescriptorParser: Parsing logic error: currentOffset {} somehow exceeded effectiveEndOffset {}.",
                      currentOffset, effectiveEndOffset);
    } else {
        spdlog::debug("MusicSubunitDescriptorParser: Parsing completed up to effective end offset {}.", effectiveEndOffset);
    }

    return {};
}


} // namespace FWA
