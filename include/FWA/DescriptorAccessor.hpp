#pragma once

#include "FWA/CommandInterface.h"
#include "FWA/DescriptorUtils.hpp" // For specifier types etc.
#include "FWA/Error.h"
#include <expected>
#include <vector>
#include <cstdint>
#include <optional>
#include <memory> // For std::shared_ptr in future state management

namespace FWA {

// Structure to hold results from CREATE DESCRIPTOR (Optional, can be expanded)
struct CreateDescriptorResult {
    std::optional<uint64_t> listId;         // Use uint64_t to handle variable size
    std::optional<uint64_t> entryPosition;  // Use uint64_t to handle variable size
    // Add other relevant fields from response if needed (e.g., object_ID if entry created?)
};

/**
 * @brief Provides methods to access AV/C descriptors and info blocks using standard commands.
 *
 * This class encapsulates the AV/C Descriptor Mechanism protocol (TA 2002013),
 * handling command building, sending (via CommandInterface), and basic response validation.
 */
class DescriptorAccessor {
public:
    explicit DescriptorAccessor(CommandInterface* commandInterface,
                                size_t sizeOfListId = DescriptorUtils::DEFAULT_SIZE_OF_LIST_ID,
                                size_t sizeOfObjectId = DescriptorUtils::DEFAULT_SIZE_OF_OBJECT_ID,
                                size_t sizeOfEntryPos = DescriptorUtils::DEFAULT_SIZE_OF_ENTRY_POS);
    ~DescriptorAccessor() = default;
    DescriptorAccessor(const DescriptorAccessor&) = delete;
    DescriptorAccessor& operator=(const DescriptorAccessor&) = delete;
    DescriptorAccessor(DescriptorAccessor&&) = delete;
    DescriptorAccessor& operator=(DescriptorAccessor&&) = delete;

    std::expected<void, IOKitError> openForRead(uint8_t targetAddr, const std::vector<uint8_t>& specifier);
    std::expected<void, IOKitError> openForWrite(uint8_t targetAddr, const std::vector<uint8_t>& specifier);
    std::expected<void, IOKitError> close(uint8_t targetAddr, const std::vector<uint8_t>& specifier);
    std::expected<std::vector<uint8_t>, IOKitError> read(uint8_t targetAddr, const std::vector<uint8_t>& specifier, uint16_t offset, uint16_t length);
    std::expected<void, IOKitError> writePartialReplace(uint8_t targetAddr, const std::vector<uint8_t>& specifier, uint16_t offset, uint16_t originalLength, const std::vector<uint8_t>& replacementData, uint8_t groupTag = 0x00);
    std::expected<void, IOKitError> deleteDescriptor(uint8_t targetAddr, const std::vector<uint8_t>& specifier, uint8_t groupTag = 0x00);
    std::expected<CreateDescriptorResult, IOKitError> createDescriptor(uint8_t targetAddr, uint8_t subfunction, const std::vector<uint8_t>& specifierWhere, const std::vector<uint8_t>& specifierWhat);
    std::expected<std::vector<uint8_t>, IOKitError> readInfoBlock(uint8_t targetAddr, const std::vector<uint8_t>& path, uint16_t offset, uint16_t length);
    std::expected<void, IOKitError> writeInfoBlock(uint8_t targetAddr, const std::vector<uint8_t>& path, uint16_t offset, uint16_t originalLength, const std::vector<uint8_t>& replacementData, uint8_t groupTag = 0x00);
    void updateDescriptorSizes(size_t sizeOfListId, size_t sizeOfObjectId, size_t sizeOfEntryPos);

private:
    CommandInterface* commandInterface_;
    size_t sizeOfListId_;
    size_t sizeOfObjectId_;
    size_t sizeOfEntryPos_;
    static constexpr uint16_t MAX_READ_CHUNK_SIZE = 256;
    static constexpr int MAX_READ_ATTEMPTS = 1024;
    std::expected<void, IOKitError> checkStandardResponse(
        const std::expected<std::vector<uint8_t>, IOKitError>& result,
        const char* commandName);
    std::expected<void, IOKitError> checkWriteDescriptorResponseSubfunction(
        const std::vector<uint8_t>& response,
        const char* commandName);
    std::expected<void, IOKitError> checkWriteInfoBlockResponseSubfunction(
        const std::vector<uint8_t>& response,
        const char* commandName);
};

} // namespace FWA
