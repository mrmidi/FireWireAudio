#include "FWA/DescriptorReader.hpp"
#include "FWA/CommandInterface.h"
#include "FWA/Enums.hpp"
#include "FWA/DescriptorUtils.hpp"
#include "FWA/Helpers.h"
#include <spdlog/spdlog.h>
#include <IOKit/avc/IOFireWireAVCConsts.h>
#include <thread>
#include <chrono>
#include <vector>
#include <algorithm>
#include <stdexcept>

namespace FWA {

// Define constants used in parsing the READ DESCRIPTOR response
constexpr size_t READ_RESP_HEADER_SIZE = 10; // Minimum size for header fields
constexpr size_t READ_RESP_STATUS_OFFSET = 0;
constexpr size_t READ_RESP_READ_RESULT_OFFSET = 4;
constexpr size_t READ_RESP_LENGTH_OFFSET = 6;
constexpr size_t READ_RESP_DATA_OFFSET = 10;

DescriptorReader::DescriptorReader(CommandInterface* commandInterface)
    : commandInterface_(commandInterface)
{
    if (!commandInterface_) {
        spdlog::critical("DescriptorReader: CommandInterface pointer is null.");
        throw std::runtime_error("DescriptorReader requires a valid CommandInterface.");
    }
}

std::expected<std::vector<uint8_t>, IOKitError> DescriptorReader::readDescriptor(
    uint8_t subunitAddr,
    DescriptorSpecifierType descriptorSpecifierType,
    const std::vector<uint8_t>& /* descriptorSpecifierSpecificData */) // Param not used currently
{
    spdlog::debug("DescriptorReader: Reading descriptor: Subunit=0x{:02x}, SpecifierType=0x{:02x}",
                  subunitAddr, static_cast<uint8_t>(descriptorSpecifierType));

    // --- Build Specifiers (Simplified based on previous example) ---
    std::vector<uint8_t> descriptorSpecifierBytes;
    if (static_cast<uint8_t>(descriptorSpecifierType) == kMusicSubunitIdentifierSpecifier) {
        descriptorSpecifierBytes = {kMusicSubunitIdentifierSpecifier};
        spdlog::debug("DescriptorReader: Using 1-byte specifier 0x80.");
    } else if (descriptorSpecifierType == DescriptorSpecifierType::UnitSubunitIdentifier) {
        // This is likely incorrect based on spec, but needed for other descriptor types later
        descriptorSpecifierBytes = DescriptorUtils::buildDescriptorSpecifier(
            descriptorSpecifierType,
            DescriptorUtils::DEFAULT_SIZE_OF_LIST_ID,
            DescriptorUtils::DEFAULT_SIZE_OF_OBJECT_ID,
            DescriptorUtils::DEFAULT_SIZE_OF_ENTRY_POS
        );
        spdlog::warn("DescriptorReader: Using potentially incorrect specifier building for type 0x00.");
    } else {
        spdlog::error("DescriptorReader: Building descriptor specifier for type 0x{:02x} not implemented.",
                      static_cast<uint8_t>(descriptorSpecifierType));
        return std::unexpected(IOKitError(kIOReturnUnsupported));
    }
    if (descriptorSpecifierBytes.empty()) {
        spdlog::error("DescriptorReader: Failed to build necessary descriptor specifiers.");
        return std::unexpected(IOKitError(kIOReturnInternalError));
    }

    // --- 1. OPEN DESCRIPTOR ---
    bool descriptorOpened = false;
    IOKitError lastErrorStatus = IOKitError(kIOReturnSuccess); // Track overall status
    { // Scope for OPEN command
        std::vector<uint8_t> openCmd;
        openCmd.push_back(kAVCControlCommand);
        openCmd.push_back(subunitAddr);
        openCmd.push_back(kAVCOpenDescriptorOpcode);
        openCmd.insert(openCmd.end(), descriptorSpecifierBytes.begin(), descriptorSpecifierBytes.end());
        openCmd.push_back(0x01); // Subfunction: Read Open
        openCmd.push_back(0x00); // Reserved field
        spdlog::debug("DescriptorReader: Sending OPEN command: {}", Helpers::formatHexBytes(openCmd));

        auto openRespResult = commandInterface_->sendCommand(openCmd);
        if (!openRespResult) {
            spdlog::error("DescriptorReader: OPEN DESCRIPTOR command failed: 0x{:x}", static_cast<int>(openRespResult.error()));
            lastErrorStatus = openRespResult.error();
            // Cannot proceed without opening
        } else if (openRespResult.value().empty() ||
                   (openRespResult.value()[READ_RESP_STATUS_OFFSET] != kAVCAcceptedStatus &&
                    openRespResult.value()[READ_RESP_STATUS_OFFSET] != kAVCImplementedStatus))
        {
            spdlog::error("DescriptorReader: OPEN DESCRIPTOR failed. Status=0x{:02x}",
                          openRespResult.value().empty() ? 0xFF : openRespResult.value()[READ_RESP_STATUS_OFFSET]);
            // log hex response
            lastErrorStatus = IOKitError(kIOReturnNotOpen); // Specific error for failed open
            // Cannot proceed without opening
        } else {
            spdlog::debug("DescriptorReader: OPEN DESCRIPTOR successful. Status=0x{:02x}", openRespResult.value()[READ_RESP_STATUS_OFFSET]);
            descriptorOpened = true;
        }
    } // End OPEN scope

    // --- 2. Standard READ DESCRIPTOR Attempt (using read_result_status) ---
    std::vector<uint8_t> descriptorData;
    uint16_t currentOffset = 0;
    bool standardReadComplete = false;
    bool commandFailedDuringStdRead = false;
    uint16_t expectedInfoBlockLength = 0; // Length read from *within* the descriptor
    bool haveExpectedLength = false;
    int readAttempts = 0;
    const int MAX_READ_ATTEMPTS = 256; // Prevent infinite loops
    constexpr uint16_t MAX_READ_CHUNK = 128; // Max reasonable chunk size (0x80 in hex - mirrors apple's code, but not spec)
    size_t successfullyReadBytes = 0; // Track bytes copied *before* truncation/error
    bool readErrorOccurred = false;   // Track if any command failed

    if (descriptorOpened) {
        spdlog::debug("DescriptorReader: Starting standard read loop (based on read_result_status)...");
        do {
            if (++readAttempts > MAX_READ_ATTEMPTS) {
                spdlog::error("DescriptorReader: Exceeded max read attempts ({}) during standard read. Aborting.", MAX_READ_ATTEMPTS);
                lastErrorStatus = IOKitError(kIOReturnIOError);
                commandFailedDuringStdRead = true;
                readErrorOccurred = true;
                break;
            }

            std::vector<uint8_t> readCmd;
            readCmd.push_back(kAVCControlCommand);
            readCmd.push_back(subunitAddr);
            readCmd.push_back(kAVCReadDescriptorOpcode);
            readCmd.insert(readCmd.end(), descriptorSpecifierBytes.begin(), descriptorSpecifierBytes.end());
            readCmd.push_back(0xFF); // descriptor_id is typically ignored after OPEN
            readCmd.push_back(0x00); // reserved field
            uint16_t readChunkSize = MAX_READ_CHUNK;
            readCmd.push_back(static_cast<uint8_t>(readChunkSize >> 8));
            readCmd.push_back(static_cast<uint8_t>(readChunkSize & 0xFF));
            readCmd.push_back(static_cast<uint8_t>(currentOffset >> 8));
            readCmd.push_back(static_cast<uint8_t>(currentOffset & 0xFF));
            spdlog::debug("DescriptorReader: Sending standard READ (Attempt {}): {}", readAttempts, Helpers::formatHexBytes(readCmd));

            auto readRespResult = commandInterface_->sendCommand(readCmd);

            if (!readRespResult) {
                spdlog::error("DescriptorReader: Standard READ DESCRIPTOR command failed: 0x{:x}", static_cast<int>(readRespResult.error()));
                lastErrorStatus = readRespResult.error();
                commandFailedDuringStdRead = true;
                readErrorOccurred = true;
                break;
            }
            if (readRespResult.value().empty()) {
                spdlog::error("DescriptorReader: Standard READ DESCRIPTOR returned empty response.");
                lastErrorStatus = IOKitError(kIOReturnBadResponse);
                commandFailedDuringStdRead = true;
                readErrorOccurred = true;
                break;
            }

            const auto& readResponse = readRespResult.value();
            uint8_t responseStatus = readResponse[READ_RESP_STATUS_OFFSET];

            if (responseStatus != kAVCImplementedStatus && responseStatus != kAVCAcceptedStatus) {
                spdlog::error("DescriptorReader: Standard READ DESCRIPTOR failed. Status=0x{:02x}", responseStatus);
                lastErrorStatus = IOKitError(kIOReturnError); // Or map specific AVC status?
                commandFailedDuringStdRead = true;
                readErrorOccurred = true;
                break;
            }
            if (readResponse.size() < READ_RESP_HEADER_SIZE) {
                 spdlog::error("DescriptorReader: Standard READ response too short ({}) for header.", readResponse.size());
                 lastErrorStatus = IOKitError(kIOReturnBadResponse);
                 commandFailedDuringStdRead = true;
                 readErrorOccurred = true;
                 break;
            }

            uint8_t readResultStatus = readResponse[READ_RESP_READ_RESULT_OFFSET];
            uint16_t bytesReadInChunk = static_cast<uint16_t>((readResponse[READ_RESP_LENGTH_OFFSET] << 8) | readResponse[READ_RESP_LENGTH_OFFSET + 1]);

            // Try to get expected length from the *first* chunk containing data
            if (!haveExpectedLength && descriptorData.empty() && readResponse.size() >= READ_RESP_DATA_OFFSET + 2 && bytesReadInChunk >= 2) {
                expectedInfoBlockLength = (static_cast<uint16_t>(readResponse[READ_RESP_DATA_OFFSET]) << 8) | readResponse[READ_RESP_DATA_OFFSET + 1];
                haveExpectedLength = true;
                spdlog::info("DescriptorReader: Read expected info block length from descriptor header: {}", expectedInfoBlockLength);
            }

            // Append received data
            if (bytesReadInChunk > 0) {
                size_t dataStart = READ_RESP_DATA_OFFSET;
                size_t bytesAvailableInResponse = readResponse.size() - dataStart;
                size_t bytesToAppend = std::min(static_cast<size_t>(bytesReadInChunk), bytesAvailableInResponse);
                if (bytesToAppend < bytesReadInChunk) {
                     spdlog::warn("DescriptorReader: READ response buffer size ({}) is smaller than reported chunk size ({}). Appending only available bytes.", bytesAvailableInResponse, bytesReadInChunk);
                }
                descriptorData.insert(descriptorData.end(), readResponse.begin() + dataStart, readResponse.begin() + dataStart + bytesToAppend);
                spdlog::debug("DescriptorReader: Standard read appended {} bytes. Total size now: {}", bytesToAppend, descriptorData.size());
                currentOffset += bytesToAppend; // Advance offset by bytes actually appended
                successfullyReadBytes += bytesToAppend; // Only increment here
                // log new descriptorData hex values
                spdlog::trace("DescriptorReader: Current descriptorData: {}", Helpers::formatHexBytes(descriptorData));
            } else {
                spdlog::debug("DescriptorReader: Standard read received 0 bytes in this chunk.");
            }

            // Check termination based on read_result_status
            if (readResultStatus == kReadResultComplete || readResultStatus == 0x12) { // 0x12 = Data length too large
                standardReadComplete = true;
                spdlog::debug("DescriptorReader: Standard read loop termination flag set by read_result_status 0x{:02x}", readResultStatus);
            } else if (readResultStatus == kReadResultMoreData) {
                if (bytesReadInChunk == 0) {
                    spdlog::warn("DescriptorReader: Received 'more data' (0x11) status but read 0 bytes. Terminating standard read loop.");
                    standardReadComplete = true; // Treat as complete to prevent potential infinite loop
                }
                // Otherwise, loop continues implicitly
            } else {
                spdlog::error("DescriptorReader: Unexpected read_result_status 0x{:02x}. Terminating standard read loop.", readResultStatus);
                lastErrorStatus = IOKitError(kIOReturnError); // Consider this an error
                standardReadComplete = true;
            }

        } while (!standardReadComplete);
        spdlog::debug("DescriptorReader: Standard read loop finished.");
    } // End if (descriptorOpened) - Standard Read Attempt

    // --- 3. Fallback READ DESCRIPTOR Attempt (Length-Driven, Apple-like) ---
    bool fallbackAttempted = false;
    size_t expectedTotalDescriptorSize = haveExpectedLength ? static_cast<size_t>(expectedInfoBlockLength) + 2 : 0;

    // Condition for fallback: Standard read didn't fail outright, we have an expected length,
    // and the data we got is less than expected.
    bool fallbackNeeded = descriptorOpened && !commandFailedDuringStdRead && haveExpectedLength && descriptorData.size() < expectedTotalDescriptorSize;
    if (fallbackNeeded)
    {
        // BP HERE
        spdlog::warn("DescriptorReader: Discarding initial {} bytes from standard read and re-reading entire descriptor in fallback.", descriptorData.size());
        descriptorData.clear(); // <<<< CLEAR DATA
        currentOffset = 0;    // <<<< RESET OFFSET
        if (haveExpectedLength && expectedTotalDescriptorSize > 0) {
             try {
                 descriptorData.resize(expectedTotalDescriptorSize); // <<<< RESIZE TO FULL
                 spdlog::debug("DescriptorReader: Resized buffer for fallback read to {} bytes.", expectedTotalDescriptorSize);
             } catch (...) { // Catch exceptions
                 spdlog::error("DescriptorReader: Failed to resize for fallback.");
                 lastErrorStatus = IOKitError::NoMemory; // Or appropriate error
             }
        } else {
             spdlog::error("DescriptorReader: Cannot fallback without expected length.");
             lastErrorStatus = IOKitError::BadArgument; // Or appropriate error
        }

        if(lastErrorStatus == IOKitError::Success) {
             spdlog::debug("DescriptorReader: Starting fallback read loop (length-driven from offset 0)...");
             readAttempts = 0;
             while (currentOffset < expectedTotalDescriptorSize) {
                 if (++readAttempts > MAX_READ_ATTEMPTS) {
                    spdlog::error("DescriptorReader: Exceeded max read attempts ({}) during fallback read. Aborting.", MAX_READ_ATTEMPTS);
                    lastErrorStatus = IOKitError(kIOReturnIOError);
                    readErrorOccurred = true;
                    break;
                }

                std::vector<uint8_t> readCmd;
                readCmd.push_back(kAVCControlCommand);
                readCmd.push_back(subunitAddr);
                readCmd.push_back(kAVCReadDescriptorOpcode);
                readCmd.insert(readCmd.end(), descriptorSpecifierBytes.begin(), descriptorSpecifierBytes.end());
                readCmd.push_back(0xFF); // descriptor_id
                readCmd.push_back(0x00); // Reserved field
                // Calculate remaining bytes needed, up to MAX_READ_CHUNK
                uint16_t bytesRemaining = static_cast<uint16_t>(expectedTotalDescriptorSize - currentOffset);
                uint16_t readChunkSize = std::min(bytesRemaining, MAX_READ_CHUNK);
                readCmd.push_back(static_cast<uint8_t>(readChunkSize >> 8));
                readCmd.push_back(static_cast<uint8_t>(readChunkSize & 0xFF));
                readCmd.push_back(static_cast<uint8_t>(currentOffset >> 8));
                readCmd.push_back(static_cast<uint8_t>(currentOffset & 0xFF));
                spdlog::debug("DescriptorReader: Sending fallback READ (Attempt {}): {}", readAttempts, Helpers::formatHexBytes(readCmd));

                auto readRespResult = commandInterface_->sendCommand(readCmd);

                if (!readRespResult || readRespResult.value().empty()) {
                    spdlog::error("DescriptorReader: Fallback READ DESCRIPTOR command failed or empty response: 0x{:x}",
                                  static_cast<int>(readRespResult ? readRespResult.error() : IOKitError::IOError));
                    lastErrorStatus = readRespResult ? readRespResult.error() : IOKitError::IOError;
                    readErrorOccurred = true;
                    break;
                }
                const auto& readResponse = readRespResult.value();
                uint8_t responseStatus = readResponse[READ_RESP_STATUS_OFFSET];

                if (responseStatus != kAVCImplementedStatus && responseStatus != kAVCAcceptedStatus) {
                    spdlog::error("DescriptorReader: Fallback READ DESCRIPTOR failed. Status=0x{:02x}", responseStatus);
                    lastErrorStatus = IOKitError(kIOReturnError);
                    readErrorOccurred = true;
                    break;
                }
                if (readResponse.size() < READ_RESP_HEADER_SIZE) {
                    spdlog::error("DescriptorReader: Fallback READ response too short ({}) for header.", readResponse.size());
                    lastErrorStatus = IOKitError(kIOReturnBadResponse);
                    readErrorOccurred = true;
                    break;
                }

                // Calculate actual bytes received in payload
                uint16_t bytesReadInChunk = static_cast<uint16_t>((readResponse[READ_RESP_LENGTH_OFFSET] << 8) | readResponse[READ_RESP_LENGTH_OFFSET + 1]);
                size_t dataStart = READ_RESP_DATA_OFFSET;
                size_t bytesAvailableInResponse = readResponse.size() - dataStart;
                size_t bytesToCopy = std::min({static_cast<size_t>(bytesReadInChunk),
                                               bytesAvailableInResponse,
                                               expectedTotalDescriptorSize - currentOffset}); // Clamp copy

                if (bytesToCopy == 0 && bytesReadInChunk > 0) {
                    // Device reported bytes but buffer has no space or response was short
                    spdlog::warn("DescriptorReader: Fallback - Calculated 0 bytes to copy despite device reporting {}. Response size {}.", bytesReadInChunk, readResponse.size());
                    // Decide whether to break or continue based on response status
                } else if (bytesToCopy == 0) {
                    // Normal end or zero byte chunk read
                    // Break if not expecting more data
                }

                if (bytesToCopy > 0) {
                    // Copy data into the correct offset
                    std::copy(readResponse.begin() + dataStart,
                              readResponse.begin() + dataStart + bytesToCopy,
                              descriptorData.begin() + currentOffset); // Copy to correct absolute offset
                    spdlog::debug("DescriptorReader: Fallback copied {} bytes to offset {}. Total size now: {}", bytesToCopy, currentOffset, currentOffset + bytesToCopy);
                    currentOffset += bytesToCopy; // Advance offset
                }

                // Check for truncation condition ONLY if header length was used
                bool deviceSentTooMuch = (bytesAvailableInResponse > bytesToCopy) && (currentOffset == expectedTotalDescriptorSize);
                if (deviceSentTooMuch) {
                    spdlog::error("DescriptorReader: Fallback read received more data than needed in final chunk ({} vs {} needed). Truncation occurred implicitly.", bytesAvailableInResponse, bytesToCopy);
                    // We already copied the right amount, just log and break.
                    break;
                }

                // If the device stops sending data prematurely in fallback
                if (bytesReadInChunk == 0 || bytesToCopy == 0) {
                    spdlog::warn("DescriptorReader: Fallback read loop stopped prematurely at offset {} (expected {}). Device may not have sent all data.", currentOffset, expectedTotalDescriptorSize);
                    readErrorOccurred = true; // Treat as an error/incomplete read
                    lastErrorStatus = IOKitError(kIOReturnUnderrun);
                    break;
                }

            } // End Fallback While Loop
            // LOG THE DESCRIPTOR DATA
            spdlog::debug("DescriptorReader: Fallback read completed. Total size: {}", descriptorData.size());
            spdlog::debug("DescriptorReader: Fallback read data: {}", Helpers::formatHexBytes(descriptorData));
        }
    }

    // --- 4. CLOSE DESCRIPTOR ---
    if (descriptorOpened) {
        std::vector<uint8_t> closeCmd;
        closeCmd.push_back(kAVCControlCommand);
        closeCmd.push_back(subunitAddr);
        closeCmd.push_back(kAVCOpenDescriptorOpcode);
        closeCmd.insert(closeCmd.end(), descriptorSpecifierBytes.begin(), descriptorSpecifierBytes.end());
        closeCmd.push_back(0x00); // Subfunction: Close
        spdlog::debug("DescriptorReader: Sending CLOSE command: {}", Helpers::formatHexBytes(closeCmd));
        auto closeRespResult = commandInterface_->sendCommand(closeCmd);
        if (!closeRespResult || closeRespResult.value().empty() ||
            (closeRespResult.value()[READ_RESP_STATUS_OFFSET] != kAVCAcceptedStatus &&
             closeRespResult.value()[READ_RESP_STATUS_OFFSET] != kAVCImplementedStatus))
        {
            spdlog::warn("DescriptorReader: CLOSE DESCRIPTOR failed or returned unexpected status: 0x{:02x}",
                         closeRespResult ? (closeRespResult.value().empty() ? 0xFF : closeRespResult.value()[READ_RESP_STATUS_OFFSET]) : -1);
            // Don't override lastErrorStatus if closing failed, the read error is more important
        } else {
            spdlog::debug("DescriptorReader: CLOSE DESCRIPTOR successful. Status=0x{:02x}", closeRespResult.value()[READ_RESP_STATUS_OFFSET]);
        }
    }

    // --- FINAL RESULT & TRUNCATION ---
    if (readErrorOccurred || !descriptorOpened) { // If any command failed during read
        spdlog::error("DescriptorReader: Failed to read descriptor due to command errors, final status 0x{:x}", static_cast<int>(lastErrorStatus));
        return std::unexpected(lastErrorStatus);
    }

    // Now, adjust size based on what was successfully read vs header expectation
    size_t finalReportedSize = descriptorData.size(); // Size after potential fallback/truncation

    if (haveExpectedLength) {
        if (successfullyReadBytes < expectedTotalDescriptorSize) {
            // Device sent LESS than header claimed (The Apple case: 456 vs 464)
            // BP HERE
            spdlog::warn("DescriptorReader: Final received data size ({}) is LESS than expected size ({}) from header. Using received size.",
                         successfullyReadBytes, expectedTotalDescriptorSize);
            // No resize needed, descriptorData already has the smaller size.
        } else if (successfullyReadBytes > expectedTotalDescriptorSize) {
            // Fallback truncated oversized packet (Our C++ case: 464 vs 464, but based on truncating a larger response)
             spdlog::warn("DescriptorReader: Final data size ({}) seems larger than expected ({}) likely due to truncated final packet. Resizing down to expected size.",
                          successfullyReadBytes, expectedTotalDescriptorSize);
             descriptorData.resize(expectedTotalDescriptorSize);
             successfullyReadBytes = expectedTotalDescriptorSize; // We aimed for this
        } else {
             // Sizes match
        }
    } else {
        // Header length wasn't read, trust whatever we got
    }

    // Final log before returning
    if (haveExpectedLength && successfullyReadBytes != expectedTotalDescriptorSize) {
         spdlog::info("DescriptorReader: Successfully read descriptor (Specifier Type 0x{:02x}). Final reported size {} bytes (Header expected {}). Returning {} bytes.",
                     static_cast<uint8_t>(descriptorSpecifierType), finalReportedSize, expectedTotalDescriptorSize, successfullyReadBytes);
    } else {
         spdlog::info("DescriptorReader: Successfully read descriptor (Specifier Type 0x{:02x}). Final size {} bytes.",
                      static_cast<uint8_t>(descriptorSpecifierType), successfullyReadBytes);
    }

    // Resize down if needed (e.g., if header said 464 but we only got 456)
    if (descriptorData.size() > successfullyReadBytes) {
         descriptorData.resize(successfullyReadBytes);
         spdlog::debug("DescriptorReader: Final resize to {} bytes.", successfullyReadBytes);
    }

    return descriptorData; // Return the (potentially resized) vector
}

} // namespace FWA
