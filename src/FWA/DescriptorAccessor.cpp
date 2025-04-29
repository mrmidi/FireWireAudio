#include "FWA/DescriptorAccessor.hpp"
#include "FWA/CommandInterface.h"
#include "FWA/DescriptorUtils.hpp"
#include "FWA/Enums.hpp" // Include specific AV/C constants if needed
#include "FWA/Helpers.h" // For formatHexBytes logging
#include <IOKit/avc/IOFireWireAVCConsts.h> // For kAVC... constants (can be removed if Enums.hpp is exhaustive)
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <algorithm> // for std::min

namespace FWA {

// Define response offsets locally for clarity within this file
namespace {
    constexpr size_t RESP_STATUS_OFFSET = 0;
    constexpr size_t READ_RESP_READ_RESULT_OFFSET = 4;
    constexpr size_t READ_RESP_LENGTH_OFFSET = 6;
    constexpr size_t READ_RESP_DATA_OFFSET = 10;
    constexpr size_t WRITE_DESC_RESP_SUBFUNC_OFFSET = 1;
    constexpr size_t WRITE_IB_RESP_SUBFUNC_OFFSET = 1; // Same offset for WRITE INFO BLOCK response subfunc
    constexpr size_t CREATE_RESP_LIST_ID_OFFSET = 3;
    constexpr size_t CREATE_RESP_ENTRY_POS_OFFSET = 1; // Relative to operand[1] start
    constexpr size_t READ_RESP_HEADER_SIZE = 10;
} // namespace

// --- Constructor ---
DescriptorAccessor::DescriptorAccessor(CommandInterface* commandInterface,
                                       size_t sizeOfListId,
                                       size_t sizeOfObjectId,
                                       size_t sizeOfEntryPos)
    : commandInterface_(commandInterface),
      sizeOfListId_(sizeOfListId),
      sizeOfObjectId_(sizeOfObjectId),
      sizeOfEntryPos_(sizeOfEntryPos)
{
    if (!commandInterface_) {
        spdlog::critical("DescriptorAccessor: CommandInterface pointer cannot be null.");
        throw std::runtime_error("DescriptorAccessor requires a valid CommandInterface.");
    }
    // Use defaults if 0 is passed, as 0 often means "not supported" or "unknown"
    if (sizeOfListId_ == 0) sizeOfListId_ = DescriptorUtils::DEFAULT_SIZE_OF_LIST_ID;
    // Object ID size *can* be 0 if not used, so don't replace it with default
    if (sizeOfEntryPos_ == 0) sizeOfEntryPos_ = DescriptorUtils::DEFAULT_SIZE_OF_ENTRY_POS;

    spdlog::debug("DescriptorAccessor created. ListIDSize={}, ObjectIDSize={}, EntryPosSize={}",
                 sizeOfListId_, sizeOfObjectId_, sizeOfEntryPos_);
}

// --- Update Sizes ---
void DescriptorAccessor::updateDescriptorSizes(size_t sizeOfListId, size_t sizeOfObjectId, size_t sizeOfEntryPos) {
     // Apply defaults if needed
     sizeOfListId_ = (sizeOfListId == 0) ? DescriptorUtils::DEFAULT_SIZE_OF_LIST_ID : sizeOfListId;
     sizeOfObjectId_ = sizeOfObjectId; // Keep 0 if specified
     sizeOfEntryPos_ = (sizeOfEntryPos == 0) ? DescriptorUtils::DEFAULT_SIZE_OF_ENTRY_POS : sizeOfEntryPos;

     spdlog::info("DescriptorAccessor sizes updated. ListIDSize={}, ObjectIDSize={}, EntryPosSize={}",
                  sizeOfListId_, sizeOfObjectId_, sizeOfEntryPos_);
}


// --- Internal Helpers ---

std::expected<void, IOKitError> DescriptorAccessor::checkStandardResponse(
    const std::expected<std::vector<uint8_t>, IOKitError>& result,
    const char* commandName)
{
    if (!result) {
        spdlog::warn("{} command failed at transport level: 0x{:x}", commandName, static_cast<int>(result.error()));
        return std::unexpected(result.error());
    }
    const auto& response = result.value();
    if (response.empty()) {
        spdlog::error("{} command returned empty response.", commandName);
        return std::unexpected(IOKitError(kIOReturnBadResponse));
    }
    uint8_t avcStatus = response[RESP_STATUS_OFFSET];
    spdlog::trace("{} response status: 0x{:02x}", commandName, avcStatus);

    switch (avcStatus) {
        case kAVCAcceptedStatus:
        case kAVCImplementedStatus:
        case kAVCInterimStatus:
            return {}; // Success or acceptable intermediate state
        case kAVCRejectedStatus:
            spdlog::warn("{} command REJECTED by target.", commandName);
            return std::unexpected(IOKitError(kIOReturnNotPermitted));
        case kAVCNotImplementedStatus:
            spdlog::warn("{} command NOT IMPLEMENTED by target.", commandName);
            return std::unexpected(IOKitError(kIOReturnUnsupported));
        default:
            spdlog::error("{} command failed with unexpected AV/C status 0x{:02x}", commandName, avcStatus);
            // Log more response bytes for debugging
            if (response.size() > 1) {
                 spdlog::error("  -> Response details (first few bytes): {}", Helpers::formatHexBytes(std::vector<uint8_t>(response.begin(), response.begin() + std::min((size_t)5, response.size()))));
            }
            return std::unexpected(IOKitError(kIOReturnBadResponse));
    }
}

std::expected<void, IOKitError> DescriptorAccessor::checkWriteDescriptorResponseSubfunction(
    const std::vector<uint8_t>& response,
    const char* commandName)
{
     if (response.size() <= WRITE_DESC_RESP_SUBFUNC_OFFSET) {
         spdlog::error("{} ACCEPTED response too short ({}) for subfunction code.", commandName, response.size());
         return std::unexpected(IOKitError(kIOReturnBadResponse));
     }
     uint8_t returnedSubfunction = response[WRITE_DESC_RESP_SUBFUNC_OFFSET];
     spdlog::debug("{} response subfunction: 0x{:02x}", commandName, returnedSubfunction);

     switch (returnedSubfunction >> 4) { // Check high nibble based on Table 0.41
         case 0: // x0h
         case 1: // x1h
         case 3: // x3h
         case 4: // x4h
             spdlog::trace("{} succeeded (Response Subfunction 0x{:02x})", commandName, returnedSubfunction);
             return {}; // Success variants
         case 2: // x2h - REJECTED
             spdlog::error("{} failed: Invalid address/length or write prevented by target (Response Subfunction 0x{:02x})", commandName, returnedSubfunction);
             return std::unexpected(IOKitError(kIOReturnNotPermitted)); // Changed error code
         default:
             spdlog::error("{} failed with unexpected response subfunction 0x{:02x}", commandName, returnedSubfunction);
             return std::unexpected(IOKitError(kIOReturnBadResponse));
     }
}

std::expected<void, IOKitError> DescriptorAccessor::checkWriteInfoBlockResponseSubfunction(
    const std::vector<uint8_t>& response,
    const char* commandName)
{
    // Based on Table 0.53 - Same logic as checkWriteDescriptorResponseSubfunction
    if (response.size() <= WRITE_IB_RESP_SUBFUNC_OFFSET) {
         spdlog::error("{} ACCEPTED response too short ({}) for subfunction code.", commandName, response.size());
         return std::unexpected(IOKitError(kIOReturnBadResponse));
     }
     uint8_t returnedSubfunction = response[WRITE_IB_RESP_SUBFUNC_OFFSET];
     spdlog::debug("{} response subfunction: 0x{:02x}", commandName, returnedSubfunction);

     switch (returnedSubfunction >> 4) {
         case 0: // x0h
         case 1: // x1h
         case 3: // x3h
         case 4: // x4h
             spdlog::trace("{} succeeded (Response Subfunction 0x{:02x})", commandName, returnedSubfunction);
             return {}; // Success variants
         case 2: // x2h - REJECTED
             spdlog::error("{} failed: Invalid address/length/path or write prevented (Response Subfunction 0x{:02x})", commandName, returnedSubfunction);
             return std::unexpected(IOKitError(kIOReturnNotPermitted)); // Changed error code
         default:
             spdlog::error("{} failed with unexpected response subfunction 0x{:02x}", commandName, returnedSubfunction);
             return std::unexpected(IOKitError(kIOReturnBadResponse));
     }
}

// --- Public Method Implementations ---

std::expected<void, IOKitError> DescriptorAccessor::openForRead(uint8_t targetAddr, const std::vector<uint8_t>& specifier) {
    std::vector<uint8_t> cmd = {
        kAVCControlCommand,
        targetAddr,
        kAVCOpenDescriptorOpcode // 0x08
    };
    cmd.insert(cmd.end(), specifier.begin(), specifier.end());
    cmd.push_back(kAVCOpenDescSubfuncReadOpen); // 0x01
    cmd.push_back(0x00); // Reserved

    spdlog::trace("Sending OPEN DESCRIPTOR (Read): {}", Helpers::formatHexBytes(cmd));
    auto result = commandInterface_->sendCommand(cmd);
    return checkStandardResponse(result, "OPEN DESCRIPTOR (Read)");
}

std::expected<void, IOKitError> DescriptorAccessor::openForWrite(uint8_t targetAddr, const std::vector<uint8_t>& specifier) {
    std::vector<uint8_t> cmd = {
        kAVCControlCommand,
        targetAddr,
        kAVCOpenDescriptorOpcode // 0x08
    };
    cmd.insert(cmd.end(), specifier.begin(), specifier.end());
    cmd.push_back(kAVCOpenDescSubfuncWriteOpen); // 0x03
    cmd.push_back(0x00); // Reserved

    spdlog::trace("Sending OPEN DESCRIPTOR (Write): {}", Helpers::formatHexBytes(cmd));
    auto result = commandInterface_->sendCommand(cmd);
    return checkStandardResponse(result, "OPEN DESCRIPTOR (Write)");
}

std::expected<void, IOKitError> DescriptorAccessor::close(uint8_t targetAddr, const std::vector<uint8_t>& specifier) {
     std::vector<uint8_t> cmd = {
        kAVCControlCommand,
        targetAddr,
        kAVCOpenDescriptorOpcode // 0x08
    };
    cmd.insert(cmd.end(), specifier.begin(), specifier.end());
    cmd.push_back(kAVCOpenDescSubfuncClose); // 0x00
    cmd.push_back(0x00); // Reserved

    spdlog::trace("Sending OPEN DESCRIPTOR (Close): {}", Helpers::formatHexBytes(cmd));
    auto result = commandInterface_->sendCommand(cmd);
    // Closing a non-open descriptor is often accepted, so standard check is usually fine.
    return checkStandardResponse(result, "OPEN DESCRIPTOR (Close)");
}

std::expected<std::vector<uint8_t>, IOKitError> DescriptorAccessor::read(
    uint8_t targetAddr, const std::vector<uint8_t>& specifier, uint16_t offset, uint16_t length)
{
    std::vector<uint8_t> accumulatedData;
    uint16_t currentReadOffset = offset;
    bool readComplete = false;
    int attempts = 0;
    bool readAll = (length == 0);
    uint16_t totalBytesRead = 0; // Tracks bytes accumulated successfully

    spdlog::debug("READ DESCRIPTOR: Starting read. Target=0x{:02x}, Offset={}, Length={}", targetAddr, offset, length);

    do {
        if (++attempts > MAX_READ_ATTEMPTS) {
            spdlog::error("READ DESCRIPTOR: Exceeded max read attempts ({}) for target 0x{:02x}, specifier {}", MAX_READ_ATTEMPTS, targetAddr, Helpers::formatHexBytes(specifier));
            return std::unexpected(IOKitError(kIOReturnTimeout));
        }

        uint16_t chunkSize = MAX_READ_CHUNK_SIZE;
        if (!readAll) {
            uint16_t remainingNeeded = length - totalBytesRead;
            if (remainingNeeded == 0) {
                 readComplete = true; // Already read everything requested
                 break;
            }
            chunkSize = std::min(chunkSize, remainingNeeded);
        }
        // If readAll, chunkSize remains MAX_READ_CHUNK_SIZE until device stops sending data

        std::vector<uint8_t> readCmd = {
            kAVCControlCommand, // Use STATUS CTYPE for READ
            targetAddr,
            kAVCReadDescriptorOpcode // 0x09
        };
        readCmd.insert(readCmd.end(), specifier.begin(), specifier.end());
        readCmd.push_back(0xFF); // read_result_status = FF (to request status)
        readCmd.push_back(0x00); // reserved
        readCmd.push_back(static_cast<uint8_t>(chunkSize >> 8));
        readCmd.push_back(static_cast<uint8_t>(chunkSize & 0xFF));
        readCmd.push_back(static_cast<uint8_t>(currentReadOffset >> 8));
        readCmd.push_back(static_cast<uint8_t>(currentReadOffset & 0xFF));

        spdlog::trace("Sending READ DESCRIPTOR (Attempt {}): Offset={}, ChunkSize={}, Cmd={}", attempts, currentReadOffset, chunkSize, Helpers::formatHexBytes(readCmd));

        auto readRespResult = commandInterface_->sendCommand(readCmd);
        auto stdCheck = checkStandardResponse(readRespResult, "READ DESCRIPTOR");
        if (!stdCheck) { return std::unexpected(stdCheck.error()); }

        const auto& response = readRespResult.value();
        if (response.size() < READ_RESP_HEADER_SIZE) {
             spdlog::error("READ DESCRIPTOR response too short ({}) for header.", response.size());
             return std::unexpected(IOKitError(kIOReturnBadResponse));
        }

        uint8_t readResultStatus = response[READ_RESP_READ_RESULT_OFFSET];
        uint16_t bytesReadInChunk = static_cast<uint16_t>((response[READ_RESP_LENGTH_OFFSET] << 8) | response[READ_RESP_LENGTH_OFFSET + 1]);
        size_t dataStart = READ_RESP_DATA_OFFSET;
        size_t bytesAvailableInResponse = (response.size() > dataStart) ? (response.size() - dataStart) : 0;
        size_t bytesToAppend = std::min(static_cast<size_t>(bytesReadInChunk), bytesAvailableInResponse);

        spdlog::trace("  Read Resp: Status=0x{:02x}, ReadResult=0x{:02x}, BytesInChunk={}, BytesAvail={}, BytesToAppend={}",
                      response[RESP_STATUS_OFFSET], readResultStatus, bytesReadInChunk, bytesAvailableInResponse, bytesToAppend);

        if (bytesToAppend > 0) {
            accumulatedData.insert(accumulatedData.end(), response.begin() + dataStart, response.begin() + dataStart + bytesToAppend);
            currentReadOffset += bytesToAppend; // Advance offset by bytes actually appended
            totalBytesRead += bytesToAppend;
        }

        // Check termination conditions
        switch (readResultStatus) {
            case kReadResultComplete:           // 0x10
            case kReadResultDataLengthTooLarge: // 0x12
                readComplete = true;
                spdlog::trace("  Read complete flag received (0x{:02x}).", readResultStatus);
                break;
            case kReadResultMoreData:           // 0x11
                if (bytesToAppend == 0 && bytesReadInChunk > 0) {
                     // Device reported more data, but sent an empty or short payload
                     spdlog::warn("  Received 'more data' (0x11) but 0 bytes appended (chunk said {}, available {}). Assuming communication issue or end.", bytesReadInChunk, bytesAvailableInResponse);
                     readComplete = true; // Treat as complete to avoid potential issues
                } else if (bytesToAppend == 0) {
                     // Device reported more data, but sent 0 bytes (might be end of data)
                     spdlog::debug("  Received 'more data' (0x11) but 0 bytes appended. Assuming end of data.");
                     readComplete = true;
                }
                // If bytes were appended, loop continues implicitly
                break;
            default:
                spdlog::error("  Unexpected read_result_status 0x{:02x}. Aborting read.", readResultStatus);
                return std::unexpected(IOKitError(kIOReturnBadResponse));
        }

        // Check if specific length requested has been met
        if (!readAll && totalBytesRead >= length) {
            readComplete = true;
            spdlog::trace("  Reached or exceeded requested read length ({})", length);
        }

    } while (!readComplete);

    // Trim excess if readAll=false and we slightly overshot due to chunk size
    if (!readAll && accumulatedData.size() > length) {
        spdlog::warn("READ DESCRIPTOR: Read more data ({}) than requested ({}). Trimming.", accumulatedData.size(), length);
        accumulatedData.resize(length);
    }

    spdlog::debug("READ DESCRIPTOR finished. Total bytes read: {}", accumulatedData.size());
    return accumulatedData;
}


std::expected<void, IOKitError> DescriptorAccessor::writePartialReplace(
    uint8_t targetAddr, const std::vector<uint8_t>& specifier, uint16_t offset,
    uint16_t originalLength, const std::vector<uint8_t>& replacementData, uint8_t groupTag)
{
    if (replacementData.size() > 0xFFFF) {
         spdlog::error("WRITE DESCRIPTOR (PartialReplace): Replacement data too large ({} bytes).", replacementData.size());
         return std::unexpected(IOKitError(kIOReturnBadArgument));
    }
    uint16_t replacementLength = static_cast<uint16_t>(replacementData.size());

    std::vector<uint8_t> cmd = {
        kAVCControlCommand,
        targetAddr,
        kAVCWriteDescriptorOpcode // 0x0A
    };
    cmd.insert(cmd.end(), specifier.begin(), specifier.end());
    cmd.push_back(kAVCWriteDescSubfuncPartialReplace); // 0x50
    cmd.push_back(groupTag);
    cmd.push_back(static_cast<uint8_t>(replacementLength >> 8));
    cmd.push_back(static_cast<uint8_t>(replacementLength & 0xFF));
    cmd.push_back(static_cast<uint8_t>(offset >> 8));
    cmd.push_back(static_cast<uint8_t>(offset & 0xFF));
    cmd.push_back(static_cast<uint8_t>(originalLength >> 8));
    cmd.push_back(static_cast<uint8_t>(originalLength & 0xFF));
    cmd.insert(cmd.end(), replacementData.begin(), replacementData.end());

    spdlog::trace("Sending WRITE DESCRIPTOR (PartialReplace): {}", Helpers::formatHexBytes(cmd));
    auto result = commandInterface_->sendCommand(cmd);

    auto stdCheck = checkStandardResponse(result, "WRITE DESCRIPTOR (PartialReplace)");
    if (!stdCheck) { return stdCheck; }

    return checkWriteDescriptorResponseSubfunction(result.value(), "WRITE DESCRIPTOR (PartialReplace)");
}

std::expected<void, IOKitError> DescriptorAccessor::deleteDescriptor(
    uint8_t targetAddr, const std::vector<uint8_t>& specifier, uint8_t groupTag)
{
    std::vector<uint8_t> cmd = {
        kAVCControlCommand,
        targetAddr,
        kAVCWriteDescriptorOpcode // 0x0A
    };
    cmd.insert(cmd.end(), specifier.begin(), specifier.end());
    cmd.push_back(kAVCWriteDescSubfuncDelete); // 0x40
    cmd.push_back(groupTag);
    // No data operands needed for delete

    spdlog::trace("Sending WRITE DESCRIPTOR (Delete): {}", Helpers::formatHexBytes(cmd));
    auto result = commandInterface_->sendCommand(cmd);

    auto stdCheck = checkStandardResponse(result, "WRITE DESCRIPTOR (Delete)");
    if (!stdCheck) { return stdCheck; }

    return checkWriteDescriptorResponseSubfunction(result.value(), "WRITE DESCRIPTOR (Delete)");
}

std::expected<CreateDescriptorResult, IOKitError> DescriptorAccessor::createDescriptor(
    uint8_t targetAddr, uint8_t subfunction,
    const std::vector<uint8_t>& specifierWhere,
    const std::vector<uint8_t>& specifierWhat)
{
     if (subfunction != kAVCCreateDescSubfuncListOrEntry && subfunction != kAVCCreateDescSubfuncEntryAndChild) {
         spdlog::error("CREATE DESCRIPTOR: Invalid subfunction 0x{:02x}", subfunction);
         return std::unexpected(IOKitError(kIOReturnBadArgument));
     }
     if (specifierWhere.empty() || specifierWhat.empty()) {
          spdlog::error("CREATE DESCRIPTOR: Specifiers cannot be empty.");
          return std::unexpected(IOKitError(kIOReturnBadArgument));
     }

    // --- Calculate expected sizes of specifiers from command ---
    // Use the accessor's current size settings
    size_t specifierWhereSize = DescriptorUtils::getDescriptorSpecifierExpectedSize(
        specifierWhere.data(), specifierWhere.size(),
        sizeOfListId_, sizeOfObjectId_, sizeOfEntryPos_);

    size_t specifierWhatSize = DescriptorUtils::getDescriptorSpecifierExpectedSize(
        specifierWhat.data(), specifierWhat.size(),
        sizeOfListId_, sizeOfObjectId_, sizeOfEntryPos_);

    if (specifierWhereSize == 0 || specifierWhatSize == 0) {
        spdlog::error("CREATE DESCRIPTOR: Could not determine size of input specifiers (where={}, what={}). Cannot parse response reliably.",
                      specifierWhereSize, specifierWhatSize);
        // Don't send the command if we can't parse the response
        return std::unexpected(IOKitError(kIOReturnInternalError));
    }
    spdlog::trace("CREATE DESCRIPTOR: Calculated command specifier sizes: where={}, what={}", specifierWhereSize, specifierWhatSize);


    // --- Build Command ---
    std::vector<uint8_t> cmd = {
        kAVCControlCommand,
        targetAddr,
        kAVCCreateDescriptorOpcode, // 0x0C
        0xFF, // result = FF
        subfunction,
        0x00 // reserved
    };
    cmd.insert(cmd.end(), specifierWhere.begin(), specifierWhere.end());
    cmd.insert(cmd.end(), specifierWhat.begin(), specifierWhat.end());

    spdlog::trace("Sending CREATE DESCRIPTOR: {}", Helpers::formatHexBytes(cmd));
    auto result = commandInterface_->sendCommand(cmd);

    // --- Check Response ---
    auto stdCheck = checkStandardResponse(result, "CREATE DESCRIPTOR");
    if (!stdCheck) {
        // Log the specifiers used during the failed attempt
        spdlog::warn(" -> Failed command used specifierWhere: {}", Helpers::formatHexBytes(specifierWhere));
        spdlog::warn(" -> Failed command used specifierWhat: {}", Helpers::formatHexBytes(specifierWhat));
        return std::unexpected(stdCheck.error());
    }

    const auto& response = result.value();
    CreateDescriptorResult createResult;

    // Base response structure: Ctype, Addr, Opcode, Result, Subfunc, Reserved | SpecW | SpecWhat | [ListID or EntryPos]
    // Offset where optional data starts: 6 + specifierWhereSize + specifierWhatSize
    size_t optionalDataOffset = 6 + specifierWhereSize + specifierWhatSize;
    spdlog::trace("CREATE DESCRIPTOR: Response size={}, Optional data offset calculated as {}", response.size(), optionalDataOffset);


    // --- Parse Optional Data based on Subfunction ---
    if (subfunction == kAVCCreateDescSubfuncListOrEntry) { // 0x00: Created list OR entry
        // Need to know if a list or entry was requested *based on specifierWhat*
        DescriptorSpecifierType whatType = static_cast<DescriptorSpecifierType>(specifierWhat[0]);

        if (whatType == DescriptorSpecifierType::ListByType) { // 0x11 -> Created a List
            if (sizeOfListId_ > 0 && response.size() >= optionalDataOffset + sizeOfListId_) {
                createResult.listId = DescriptorUtils::readBytes(&response[optionalDataOffset], sizeOfListId_);
                spdlog::debug("CREATE DESCRIPTOR (subfunc 00): Created list with ID {}", createResult.listId.value());
            } else {
                spdlog::warn("CREATE DESCRIPTOR (subfunc 00 - list): ACCEPTED response too short ({}) or list ID size is 0 to parse list ID at offset {}.", response.size(), optionalDataOffset);
                 // It's possible the target doesn't return it even on success in some cases? Return success but no ID.
            }
        } else { // Assume entry creation (e.g., type 0x20, 0x22)
            if (sizeOfEntryPos_ > 0 && response.size() >= optionalDataOffset + sizeOfEntryPos_) {
                createResult.entryPosition = DescriptorUtils::readBytes(&response[optionalDataOffset], sizeOfEntryPos_);
                spdlog::debug("CREATE DESCRIPTOR (subfunc 00): Created entry at position {}", createResult.entryPosition.value());
            } else {
                 spdlog::warn("CREATE DESCRIPTOR (subfunc 00 - entry): ACCEPTED response too short ({}) or entry pos size is 0 to parse entry position at offset {}.", response.size(), optionalDataOffset);
                 // Return success but no position.
            }
        }
    } else { // 0x01: Created entry AND child list
         // Expecting list_ID of the *child* list (Table 24)
         if (sizeOfListId_ > 0 && response.size() >= optionalDataOffset + sizeOfListId_) {
             createResult.listId = DescriptorUtils::readBytes(&response[optionalDataOffset], sizeOfListId_);
             spdlog::debug("CREATE DESCRIPTOR (subfunc 01): Created child list with ID {}", createResult.listId.value());
             // TODO: Table 24 is a bit unclear if the parent entry position is *also* returned.
             // If needed, it would likely precede the list_ID. For now, just parse list ID.
         } else {
             spdlog::warn("CREATE DESCRIPTOR (subfunc 01): ACCEPTED response too short ({}) or list ID size is 0 to parse child list ID at offset {}.", response.size(), optionalDataOffset);
             // Return success but no list ID.
         }
         // Note: We don't get the parent entry's position directly here according to a strict reading of Table 24, only the new list's ID.
    }

    return createResult;
}


// --- Info Block Methods ---

std::expected<std::vector<uint8_t>, IOKitError> DescriptorAccessor::readInfoBlock(
    uint8_t targetAddr, const std::vector<uint8_t>& path, uint16_t offset, uint16_t length)
{
    // --- Implementation unchanged, assumes 'path' is correctly built ---
    // (Uses the read loop logic which is already implemented)
    // ... (existing readInfoBlock implementation) ...
    std::vector<uint8_t> accumulatedData;
    uint16_t currentReadOffset = offset;
    bool readComplete = false;
    int attempts = 0;
    bool readAll = (length == 0);
    uint16_t totalBytesRead = 0;

    spdlog::debug("READ INFO BLOCK: Starting read. Target=0x{:02x}, Offset={}, Length={}, Path={}", targetAddr, offset, length, Helpers::formatHexBytes(path));

    if (path.empty()) {
        spdlog::error("READ INFO BLOCK: Reference path cannot be empty.");
        return std::unexpected(IOKitError(kIOReturnBadArgument));
    }

    do {
         if (++attempts > MAX_READ_ATTEMPTS) {
             spdlog::error("READ INFO BLOCK: Exceeded max read attempts ({})", MAX_READ_ATTEMPTS);
             return std::unexpected(IOKitError(kIOReturnTimeout));
         }

        uint16_t chunkSize = MAX_READ_CHUNK_SIZE;
         if (!readAll) {
             uint16_t remainingNeeded = length - totalBytesRead;
             if (remainingNeeded == 0) {
                 readComplete = true; break;
             }
             chunkSize = std::min(chunkSize, remainingNeeded);
        }

        std::vector<uint8_t> readCmd = {
            kAVCStatusInquiryCommand,
            targetAddr,
            kAVCReadInfoBlockOpcode // 0x06
        };
        readCmd.insert(readCmd.end(), path.begin(), path.end()); // Insert the path structure
        readCmd.push_back(0xFF); // read_result_status
        readCmd.push_back(0x00); // reserved
        readCmd.push_back(static_cast<uint8_t>(chunkSize >> 8));
        readCmd.push_back(static_cast<uint8_t>(chunkSize & 0xFF));
        readCmd.push_back(static_cast<uint8_t>(currentReadOffset >> 8));
        readCmd.push_back(static_cast<uint8_t>(currentReadOffset & 0xFF));

        spdlog::trace("Sending READ INFO BLOCK (Attempt {}): Offset={}, ChunkSize={}, Cmd={}", attempts, currentReadOffset, chunkSize, Helpers::formatHexBytes(readCmd));

        auto readRespResult = commandInterface_->sendCommand(readCmd);
        auto stdCheck = checkStandardResponse(readRespResult, "READ INFO BLOCK");
        if (!stdCheck) { return std::unexpected(stdCheck.error()); }

        const auto& response = readRespResult.value();
        if (response.size() < READ_RESP_HEADER_SIZE) { // Check standard header size
             spdlog::error("READ INFO BLOCK response too short ({}) for header.", response.size());
             return std::unexpected(IOKitError(kIOReturnBadResponse));
        }

        uint8_t readResultStatus = response[READ_RESP_READ_RESULT_OFFSET];
        uint16_t bytesReadInChunk = static_cast<uint16_t>((response[READ_RESP_LENGTH_OFFSET] << 8) | response[READ_RESP_LENGTH_OFFSET + 1]);
        size_t dataStart = READ_RESP_DATA_OFFSET;
        size_t bytesAvailableInResponse = (response.size() > dataStart) ? (response.size() - dataStart) : 0;
        size_t bytesToAppend = std::min(static_cast<size_t>(bytesReadInChunk), bytesAvailableInResponse);

        spdlog::trace("  Read IB Resp: Status=0x{:02x}, ReadResult=0x{:02x}, BytesInChunk={}, BytesAvail={}, BytesToAppend={}",
                       response[RESP_STATUS_OFFSET], readResultStatus, bytesReadInChunk, bytesAvailableInResponse, bytesToAppend);

        if (bytesToAppend > 0) {
            accumulatedData.insert(accumulatedData.end(), response.begin() + dataStart, response.begin() + dataStart + bytesToAppend);
            currentReadOffset += bytesToAppend;
            totalBytesRead += bytesToAppend;
        }

        // Check termination conditions (Table 49 mirrors Table 36)
        switch (readResultStatus) {
             case kReadResultComplete:           // 10h
             case kReadResultDataLengthTooLarge: // 12h
                 readComplete = true;
                 break;
             case kReadResultMoreData:           // 11h
                 if (bytesToAppend == 0) {
                     spdlog::warn("  Read IB received 'more data' but 0 bytes appended. Assuming completion.");
                     readComplete = true;
                 }
                 break;
             default:
                 spdlog::error("  Read IB unexpected read_result_status 0x{:02x}. Aborting.", readResultStatus);
                 return std::unexpected(IOKitError(kIOReturnBadResponse));
         }

         if (!readAll && totalBytesRead >= length) {
            readComplete = true;
         }

    } while (!readComplete);

     if (!readAll && accumulatedData.size() > length) {
        accumulatedData.resize(length);
     }

    spdlog::debug("READ INFO BLOCK finished. Total bytes read: {}", accumulatedData.size());
    return accumulatedData;
}

std::expected<void, IOKitError> DescriptorAccessor::writeInfoBlock(
    uint8_t targetAddr, const std::vector<uint8_t>& path, uint16_t offset,
    uint16_t originalLength, const std::vector<uint8_t>& replacementData, uint8_t groupTag)
{
     // --- Ensure 'path' is correctly built before calling ---
     if (path.empty()) {
        spdlog::error("WRITE INFO BLOCK: Reference path cannot be empty.");
        return std::unexpected(IOKitError(kIOReturnBadArgument));
     }
     if (replacementData.size() > 0xFFFF) {
        spdlog::error("WRITE INFO BLOCK: Replacement data too large ({} bytes).", replacementData.size());
        return std::unexpected(IOKitError(kIOReturnBadArgument));
     }
    uint16_t replacementLength = static_cast<uint16_t>(replacementData.size());

    std::vector<uint8_t> cmd = {
        kAVCControlCommand,
        targetAddr,
        kAVCWriteInfoBlockOpcode // 0x07
    };
    cmd.insert(cmd.end(), path.begin(), path.end());
    cmd.push_back(kAVCWriteInfoBlockSubfuncPartialReplace); // 0x50 (only valid subfunc)
    cmd.push_back(groupTag);
    cmd.push_back(static_cast<uint8_t>(replacementLength >> 8));
    cmd.push_back(static_cast<uint8_t>(replacementLength & 0xFF));
    cmd.push_back(static_cast<uint8_t>(offset >> 8));
    cmd.push_back(static_cast<uint8_t>(offset & 0xFF));
    cmd.push_back(static_cast<uint8_t>(originalLength >> 8));
    cmd.push_back(static_cast<uint8_t>(originalLength & 0xFF));
    cmd.insert(cmd.end(), replacementData.begin(), replacementData.end());

    spdlog::trace("Sending WRITE INFO BLOCK: {}", Helpers::formatHexBytes(cmd));
    auto result = commandInterface_->sendCommand(cmd);

    auto stdCheck = checkStandardResponse(result, "WRITE INFO BLOCK");
    if (!stdCheck) { return stdCheck; }

    return checkWriteInfoBlockResponseSubfunction(result.value(), "WRITE INFO BLOCK");
}

} // namespace FWA