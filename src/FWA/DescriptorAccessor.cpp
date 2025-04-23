#include "FWA/DescriptorAccessor.hpp"
#include "FWA/CommandInterface.h"
#include "FWA/DescriptorUtils.hpp"
#include "FWA/Enums.hpp"
#include "FWA/Helpers.h"
#include <IOKit/avc/IOFireWireAVCConsts.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <algorithm>

namespace FWA {

// Offsets within response frames (based on TA 2002013)
constexpr size_t RESP_STATUS_OFFSET = 0; // applies to most responses
constexpr size_t READ_RESP_READ_RESULT_OFFSET = 4;
constexpr size_t READ_RESP_LENGTH_OFFSET = 6;
constexpr size_t READ_RESP_DATA_OFFSET = 10;
constexpr size_t WRITE_DESC_RESP_SUBFUNC_OFFSET = 1; // For WRITE DESCRIPTOR status/response
constexpr size_t WRITE_IB_RESP_SUBFUNC_OFFSET = 1; // For WRITE INFO BLOCK status/response
constexpr size_t CREATE_RESP_LIST_ID_OFFSET = 3; // Offset for list_ID in response operand[3] onwards
constexpr size_t CREATE_RESP_ENTRY_POS_OFFSET = 1; // Offset for entry_pos in response operand[1] onwards

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
    spdlog::debug("DescriptorAccessor created. ListIDSize={}, ObjectIDSize={}, EntryPosSize={}",
                 sizeOfListId_, sizeOfObjectId_, sizeOfEntryPos_);
}

void DescriptorAccessor::updateDescriptorSizes(size_t sizeOfListId, size_t sizeOfObjectId, size_t sizeOfEntryPos) {
     sizeOfListId_ = sizeOfListId;
     sizeOfObjectId_ = sizeOfObjectId;
     sizeOfEntryPos_ = sizeOfEntryPos;
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
            return {};
        case kAVCRejectedStatus:
            spdlog::warn("{} command REJECTED by target.", commandName);
            return std::unexpected(IOKitError(kIOReturnNotPermitted));
        case kAVCNotImplementedStatus:
            spdlog::warn("{} command NOT IMPLEMENTED by target.", commandName);
            return std::unexpected(IOKitError(kIOReturnUnsupported));
        default:
            spdlog::error("{} command failed with unexpected AV/C status 0x{:02x}", commandName, avcStatus);
            return std::unexpected(IOKitError(kIOReturnBadResponse));
    }
}

std::expected<void, IOKitError> DescriptorAccessor::checkWriteDescriptorResponseSubfunction(
    const std::vector<uint8_t>& response,
    const char* commandName)
{
     if (response.size() <= WRITE_DESC_RESP_SUBFUNC_OFFSET) {
         spdlog::error("{} ACCEPTED response too short for subfunction code.", commandName);
         return std::unexpected(IOKitError(kIOReturnBadResponse));
     }
     uint8_t returnedSubfunction = response[WRITE_DESC_RESP_SUBFUNC_OFFSET];
     spdlog::debug("{} response subfunction: 0x{:02x}", commandName, returnedSubfunction);

     switch (returnedSubfunction >> 4) {
         case 0:
         case 1:
         case 3:
         case 4:
             return {};
         case 2:
             spdlog::error("{} failed: Invalid address/length or write prevented by target (Subfunction 0x{:02x})", commandName, returnedSubfunction);
             return std::unexpected(IOKitError(kIOReturnBadArgument));
         default:
             spdlog::error("{} failed with unexpected response subfunction 0x{:02x}", commandName, returnedSubfunction);
             return std::unexpected(IOKitError(kIOReturnBadResponse));
     }
}

std::expected<void, IOKitError> DescriptorAccessor::checkWriteInfoBlockResponseSubfunction(
    const std::vector<uint8_t>& response,
    const char* commandName)
{
    if (response.size() <= WRITE_IB_RESP_SUBFUNC_OFFSET) {
         spdlog::error("{} ACCEPTED response too short for subfunction code.", commandName);
         return std::unexpected(IOKitError(kIOReturnBadResponse));
     }
     uint8_t returnedSubfunction = response[WRITE_IB_RESP_SUBFUNC_OFFSET];
     spdlog::debug("{} response subfunction: 0x{:02x}", commandName, returnedSubfunction);

     switch (returnedSubfunction >> 4) {
         case 0:
         case 1:
         case 3:
         case 4:
             return {};
         case 2:
             spdlog::error("{} failed: Invalid address/length/path or write prevented (Subfunction 0x{:02x})", commandName, returnedSubfunction);
             return std::unexpected(IOKitError(kIOReturnBadArgument));
         default:
             spdlog::error("{} failed with unexpected response subfunction 0x{:02x}", commandName, returnedSubfunction);
             return std::unexpected(IOKitError(kIOReturnBadResponse));
     }
}

// --- Public Method Implementations ---

std::expected<void, IOKitError> DescriptorAccessor::openForRead(uint8_t targetAddr, const std::vector<uint8_t>& specifier) {
    std::vector<uint8_t> cmd;
    cmd.push_back(kAVCControlCommand);
    cmd.push_back(targetAddr);
    cmd.push_back(kAVCOpenDescriptorOpcode);
    cmd.insert(cmd.end(), specifier.begin(), specifier.end());
    cmd.push_back(0x01);
    spdlog::trace("Sending OPEN DESCRIPTOR (Read): {}", Helpers::formatHexBytes(cmd));
    auto result = commandInterface_->sendCommand(cmd);
    return checkStandardResponse(result, "OPEN DESCRIPTOR (Read)");
}

std::expected<void, IOKitError> DescriptorAccessor::openForWrite(uint8_t targetAddr, const std::vector<uint8_t>& specifier) {
    std::vector<uint8_t> cmd;
    cmd.push_back(kAVCControlCommand);
    cmd.push_back(targetAddr);
    cmd.push_back(kAVCOpenDescriptorOpcode);
    cmd.insert(cmd.end(), specifier.begin(), specifier.end());
    cmd.push_back(0x03);
    spdlog::trace("Sending OPEN DESCRIPTOR (Write): {}", Helpers::formatHexBytes(cmd));
    auto result = commandInterface_->sendCommand(cmd);
    return checkStandardResponse(result, "OPEN DESCRIPTOR (Write)");
}

std::expected<void, IOKitError> DescriptorAccessor::close(uint8_t targetAddr, const std::vector<uint8_t>& specifier) {
    std::vector<uint8_t> cmd;
    cmd.push_back(kAVCControlCommand);
    cmd.push_back(targetAddr);
    cmd.push_back(kAVCOpenDescriptorOpcode);
    cmd.insert(cmd.end(), specifier.begin(), specifier.end());
    cmd.push_back(0x00);
    spdlog::trace("Sending OPEN DESCRIPTOR (Close): {}", Helpers::formatHexBytes(cmd));
    auto result = commandInterface_->sendCommand(cmd);
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
    uint16_t totalBytesRead = 0;
    spdlog::debug("READ DESCRIPTOR: Starting read. Target=0x{:02x}, Offset={}, Length={}", targetAddr, offset, length);
    do {
        if (++attempts > MAX_READ_ATTEMPTS) {
            spdlog::error("READ DESCRIPTOR: Exceeded max read attempts ({})", MAX_READ_ATTEMPTS);
            return std::unexpected(IOKitError(kIOReturnTimeout));
        }
        uint16_t chunkSize = MAX_READ_CHUNK_SIZE;
        if (!readAll && (length - totalBytesRead) < chunkSize) {
            chunkSize = length - totalBytesRead;
        }
        if (chunkSize == 0 && !readAll) {
             readComplete = true;
             break;
        }
        std::vector<uint8_t> readCmd;
        readCmd.push_back(kAVCStatusInquiryCommand);
        readCmd.push_back(targetAddr);
        readCmd.push_back(kAVCReadDescriptorOpcode);
        readCmd.insert(readCmd.end(), specifier.begin(), specifier.end());
        readCmd.push_back(0xFF);
        readCmd.push_back(0x00);
        readCmd.push_back(static_cast<uint8_t>(chunkSize >> 8));
        readCmd.push_back(static_cast<uint8_t>(chunkSize & 0xFF));
        readCmd.push_back(static_cast<uint8_t>(currentReadOffset >> 8));
        readCmd.push_back(static_cast<uint8_t>(currentReadOffset & 0xFF));
        spdlog::trace("Sending READ DESCRIPTOR (Attempt {}): Offset={}, ChunkSize={}, Cmd={}", attempts, currentReadOffset, chunkSize, Helpers::formatHexBytes(readCmd));
        auto readRespResult = commandInterface_->sendCommand(readCmd);
        auto stdCheck = checkStandardResponse(readRespResult, "READ DESCRIPTOR");
        if (!stdCheck) { return std::unexpected(stdCheck.error()); }
        const auto& response = readRespResult.value();
        if (response.size() < READ_RESP_DATA_OFFSET) {
             spdlog::error("READ DESCRIPTOR response too short ({}) for header.", response.size());
             return std::unexpected(IOKitError(kIOReturnBadResponse));
        }
        uint8_t readResultStatus = response[READ_RESP_READ_RESULT_OFFSET];
        uint16_t bytesReadInChunk = static_cast<uint16_t>((response[READ_RESP_LENGTH_OFFSET] << 8) | response[READ_RESP_LENGTH_OFFSET + 1]);
        size_t dataStart = READ_RESP_DATA_OFFSET;
        size_t bytesAvailableInResponse = response.size() - dataStart;
        size_t bytesToAppend = std::min(static_cast<size_t>(bytesReadInChunk), bytesAvailableInResponse);
        spdlog::trace("  Read Resp: Status=0x{:02x}, ReadResult=0x{:02x}, BytesInChunk={}, BytesAvail={}, BytesToAppend={}",
                      response[RESP_STATUS_OFFSET], readResultStatus, bytesReadInChunk, bytesAvailableInResponse, bytesToAppend);
        if (bytesToAppend > 0) {
            accumulatedData.insert(accumulatedData.end(), response.begin() + dataStart, response.begin() + dataStart + bytesToAppend);
            currentReadOffset += bytesToAppend;
            totalBytesRead += bytesToAppend;
        }
        if (readResultStatus == 0x10) {
            readComplete = true;
            spdlog::trace("  Read complete flag received.");
        } else if (readResultStatus == 0x11) {
            if (bytesToAppend == 0) {
                 spdlog::warn("  Received 'more data' but 0 bytes appended. Assuming completion.");
                 readComplete = true;
            }
        } else if (readResultStatus == 0x12) {
             readComplete = true;
             spdlog::trace("  Read complete (data length too large) flag received.");
        }
        else {
            spdlog::error("  Unexpected read_result_status 0x{:02x}. Aborting read.", readResultStatus);
            return std::unexpected(IOKitError(kIOReturnBadResponse));
        }
        if (!readAll && totalBytesRead >= length) {
            readComplete = true;
            spdlog::trace("  Reached requested read length ({})", length);
        }
    } while (!readComplete);
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
    std::vector<uint8_t> cmd;
    cmd.push_back(kAVCControlCommand);
    cmd.push_back(targetAddr);
    cmd.push_back(kAVCWriteDescriptorOpcode);
    cmd.insert(cmd.end(), specifier.begin(), specifier.end());
    cmd.push_back(0x50);
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
    std::vector<uint8_t> cmd;
    cmd.push_back(kAVCControlCommand);
    cmd.push_back(targetAddr);
    cmd.push_back(kAVCWriteDescriptorOpcode);
    cmd.insert(cmd.end(), specifier.begin(), specifier.end());
    cmd.push_back(0x40);
    cmd.push_back(groupTag);
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
     if (subfunction != 0x00 && subfunction != 0x01) {
         spdlog::error("CREATE DESCRIPTOR: Invalid subfunction 0x{:02x}", subfunction);
         return std::unexpected(IOKitError(kIOReturnBadArgument));
     }
    std::vector<uint8_t> cmd;
    cmd.push_back(kAVCControlCommand);
    cmd.push_back(targetAddr);
    cmd.push_back(kAVCCreateDescriptorOpcode);
    cmd.push_back(0xFF);
    cmd.push_back(subfunction);
    cmd.push_back(0x00);
    cmd.insert(cmd.end(), specifierWhere.begin(), specifierWhere.end());
    cmd.insert(cmd.end(), specifierWhat.begin(), specifierWhat.end());
    spdlog::trace("Sending CREATE DESCRIPTOR: {}", Helpers::formatHexBytes(cmd));
    auto result = commandInterface_->sendCommand(cmd);
    auto stdCheck = checkStandardResponse(result, "CREATE DESCRIPTOR");
     if (!stdCheck) { return std::unexpected(stdCheck.error()); }
     const auto& response = result.value();
     CreateDescriptorResult createResult;
     if (subfunction == 0x00) {
         if (response.size() > CREATE_RESP_ENTRY_POS_OFFSET + sizeOfEntryPos_) {
              createResult.entryPosition = DescriptorUtils::readBytes(&response[CREATE_RESP_ENTRY_POS_OFFSET], sizeOfEntryPos_);
              spdlog::debug("CREATE DESCRIPTOR (subfunc 00): Created entry at position {}", createResult.entryPosition.value());
         } else {
              spdlog::warn("CREATE DESCRIPTOR (subfunc 00): ACCEPTED response too short to parse entry position.");
         }
     } else {
          if (response.size() > CREATE_RESP_LIST_ID_OFFSET + sizeOfListId_) {
               createResult.listId = DescriptorUtils::readBytes(&response[CREATE_RESP_LIST_ID_OFFSET], sizeOfListId_);
               spdlog::debug("CREATE DESCRIPTOR (subfunc 01): Created child list with ID {}", createResult.listId.value());
               if (response.size() > CREATE_RESP_ENTRY_POS_OFFSET + sizeOfEntryPos_) {
                    createResult.entryPosition = DescriptorUtils::readBytes(&response[CREATE_RESP_ENTRY_POS_OFFSET], sizeOfEntryPos_);
                    spdlog::debug("  -> Associated entry created at position {}", createResult.entryPosition.value());
               }
          } else {
                spdlog::warn("CREATE DESCRIPTOR (subfunc 01): ACCEPTED response too short to parse list ID.");
          }
     }
     return createResult;
}

std::expected<std::vector<uint8_t>, IOKitError> DescriptorAccessor::readInfoBlock(
    uint8_t targetAddr, const std::vector<uint8_t>& path, uint16_t offset, uint16_t length)
{
    std::vector<uint8_t> accumulatedData;
    uint16_t currentReadOffset = offset;
    bool readComplete = false;
    int attempts = 0;
    bool readAll = (length == 0);
    uint16_t totalBytesRead = 0;
    spdlog::debug("READ INFO BLOCK: Starting read. Target=0x{:02x}, Offset={}, Length={}", targetAddr, offset, length);
    do {
         if (++attempts > MAX_READ_ATTEMPTS) {
             spdlog::error("READ INFO BLOCK: Exceeded max read attempts ({})", MAX_READ_ATTEMPTS);
             return std::unexpected(IOKitError(kIOReturnTimeout));
         }
        uint16_t chunkSize = MAX_READ_CHUNK_SIZE;
         if (!readAll && (length - totalBytesRead) < chunkSize) {
             chunkSize = length - totalBytesRead;
         }
         if (chunkSize == 0 && !readAll) {
             readComplete = true;
             break;
         }
        std::vector<uint8_t> readCmd;
        readCmd.push_back(kAVCStatusInquiryCommand);
        readCmd.push_back(targetAddr);
        readCmd.push_back(kAVCReadInfoBlockOpcode);
        readCmd.insert(readCmd.end(), path.begin(), path.end());
        readCmd.push_back(0xFF);
        readCmd.push_back(0x00);
        readCmd.push_back(static_cast<uint8_t>(chunkSize >> 8));
        readCmd.push_back(static_cast<uint8_t>(chunkSize & 0xFF));
        readCmd.push_back(static_cast<uint8_t>(currentReadOffset >> 8));
        readCmd.push_back(static_cast<uint8_t>(currentReadOffset & 0xFF));
        spdlog::trace("Sending READ INFO BLOCK (Attempt {}): Offset={}, ChunkSize={}, Cmd={}", attempts, currentReadOffset, chunkSize, Helpers::formatHexBytes(readCmd));
        auto readRespResult = commandInterface_->sendCommand(readCmd);
        auto stdCheck = checkStandardResponse(readRespResult, "READ INFO BLOCK");
        if (!stdCheck) { return std::unexpected(stdCheck.error()); }
        const auto& response = readRespResult.value();
        if (response.size() < READ_RESP_DATA_OFFSET) {
             spdlog::error("READ INFO BLOCK response too short ({}) for header.", response.size());
             return std::unexpected(IOKitError(kIOReturnBadResponse));
        }
        uint8_t readResultStatus = response[READ_RESP_READ_RESULT_OFFSET];
        uint16_t bytesReadInChunk = static_cast<uint16_t>((response[READ_RESP_LENGTH_OFFSET] << 8) | response[READ_RESP_LENGTH_OFFSET + 1]);
        size_t dataStart = READ_RESP_DATA_OFFSET;
        size_t bytesAvailableInResponse = response.size() - dataStart;
        size_t bytesToAppend = std::min(static_cast<size_t>(bytesReadInChunk), bytesAvailableInResponse);
        spdlog::trace("  Read IB Resp: Status=0x{:02x}, ReadResult=0x{:02x}, BytesInChunk={}, BytesAvail={}, BytesToAppend={}",
                      response[RESP_STATUS_OFFSET], readResultStatus, bytesReadInChunk, bytesAvailableInResponse, bytesToAppend);
        if (bytesToAppend > 0) {
            accumulatedData.insert(accumulatedData.end(), response.begin() + dataStart, response.begin() + dataStart + bytesToAppend);
            currentReadOffset += bytesToAppend;
            totalBytesRead += bytesToAppend;
        }
        if (readResultStatus == 0x10) {
            readComplete = true;
        } else if (readResultStatus == 0x11) {
            if (bytesToAppend == 0) {
                 readComplete = true;
                 spdlog::warn("  Received 'more data' but 0 bytes appended. Assuming completion.");
            }
        } else if (readResultStatus == 0x12) {
            readComplete = true;
        } else {
            spdlog::error("  Unexpected read_result_status 0x{:02x}. Aborting read.", readResultStatus);
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
     if (replacementData.size() > 0xFFFF) {
         spdlog::error("WRITE INFO BLOCK: Replacement data too large ({} bytes).", replacementData.size());
         return std::unexpected(IOKitError(kIOReturnBadArgument));
     }
    uint16_t replacementLength = static_cast<uint16_t>(replacementData.size());
    std::vector<uint8_t> cmd;
    cmd.push_back(kAVCControlCommand);
    cmd.push_back(targetAddr);
    cmd.push_back(kAVCWriteInfoBlockOpcode);
    cmd.insert(cmd.end(), path.begin(), path.end());
    cmd.push_back(0x50);
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
