#pragma once

#include "FWA/DescriptorSpecifier.hpp" // Include the data structures and enum
#include "FWA/Enums.hpp"               // Include Enums where DescriptorSpecifierType is defined
#include <cstdint>
#include <vector>
#include <optional>
#include <variant> // Include variant as it's part of ParsedDescriptorSpecifier
#include <string>  // Include string

namespace FWA {

/**
 * @brief Utility class providing static methods for handling AV/C descriptor specifiers.
 */
class DescriptorUtils {
public:
    // --- Default sizes from spec (used if dynamic sizes are 0) ---
    static constexpr size_t DEFAULT_SIZE_OF_LIST_ID = 2;
    static constexpr size_t DEFAULT_SIZE_OF_OBJECT_ID = 0; // Default is not supported
    static constexpr size_t DEFAULT_SIZE_OF_ENTRY_POS = 2;

    /**
     * @brief Builds the descriptor specifier byte sequence.
     * @param type The type of the specifier.
     * @param sizeOfListId Size in bytes for list IDs (obtained from target).
     * @param sizeOfObjectId Size in bytes for object IDs (obtained from target).
     * @param sizeOfEntryPos Size in bytes for entry positions (obtained from target).
     * @param listId Optional list ID.
     * @param objectId Optional object ID.
     * @param entryPosition Optional entry position.
     * @param listOrEntryType Optional type byte.
     * @param rootListId Optional root list ID.
     * @param subunitSpecifier Optional subunit specifier data (for 0x24, 0x25 - NOT fully implemented).
     * @return A vector containing the specifier bytes, or an empty vector on error.
     */
    static std::vector<uint8_t> buildDescriptorSpecifier(
        DescriptorSpecifierType type,
        size_t sizeOfListId,
        size_t sizeOfObjectId,
        size_t sizeOfEntryPos,
        // Optional arguments based on type
        std::optional<uint64_t> listId = std::nullopt,
        std::optional<uint64_t> objectId = std::nullopt,
        std::optional<uint64_t> entryPosition = std::nullopt,
        std::optional<uint8_t> listOrEntryType = std::nullopt,
        std::optional<uint64_t> rootListId = std::nullopt,
        const std::optional<std::vector<uint8_t>>& subunitSpecifier = std::nullopt
    );

    /**
     * @brief Parses a descriptor specifier from a buffer.
     * @param buffer Pointer to the start of the buffer containing the specifier.
     * @param bufferLen The maximum length of the buffer available for parsing.
     * @param sizeOfListId Size in bytes for list IDs (obtained from target).
     * @param sizeOfObjectId Size in bytes for object IDs (obtained from target).
     * @param sizeOfEntryPos Size in bytes for entry positions (obtained from target).
     * @return A ParsedDescriptorSpecifier struct containing the parsed data and consumed size,
     *         or a struct with Unknown type and consumedSize=0 on error.
     */
    static ParsedDescriptorSpecifier parseDescriptorSpecifier(
        const uint8_t* buffer,
        size_t bufferLen,
        size_t sizeOfListId,
        size_t sizeOfObjectId,
        size_t sizeOfEntryPos);

     /**
      * @brief Calculates the expected size of a descriptor specifier based on its type
      *        and the dynamic sizes provided by the target.
      * @param buffer Pointer to the start of the buffer containing the specifier type (at least).
      * @param bufferLen The maximum length of the buffer available.
      * @param sizeOfListId Size in bytes for list IDs (obtained from target).
      * @param sizeOfObjectId Size in bytes for object IDs (obtained from target).
      * @param sizeOfEntryPos Size in bytes for entry positions (obtained from target).
      * @return The expected size in bytes, or 0 if the type is unknown/invalid or buffer too short for type byte,
      *         or if size cannot be determined generically (e.g., subunit dependent types).
      */
     static size_t getDescriptorSpecifierExpectedSize(
         const uint8_t* buffer, size_t bufferLen,
         size_t sizeOfListId,
         size_t sizeOfObjectId,
         size_t sizeOfEntryPos);

         // Helper for writing multi-byte values (MSB first)
         static bool appendBytes(std::vector<uint8_t>& vec, uint64_t val, size_t numBytes);
         // Helper for reading multi-byte values (MSB first)
         static uint64_t readBytes(const uint8_t* buffer, size_t numBytes);
         // Helper to get effective size using default if needed
         static size_t getEffectiveSize(size_t dynamicSize, size_t defaultSize);


};

} // namespace FWA

