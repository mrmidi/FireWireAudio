#pragma once

#include "FWA/Enums.hpp"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <optional>
#include <variant>
#include <string> // For holding subunit address string if needed



namespace FWA {

// --- Structures to hold parsed data for specific specifier types ---

struct SpecifierUnitSubunit {
    // No specific fields beyond the type
};

struct SpecifierListById {
    uint64_t listId = 0; // Use uint64_t to accommodate various sizes, actual bytes depend on sizeOfListId
};

struct SpecifierListByType {
    uint8_t listType = 0;
};

struct SpecifierEntryByPosition {
    uint64_t listId = 0;        // Use uint64_t, actual bytes depend on sizeOfListId
    uint64_t entryPosition = 0; // Use uint64_t, actual bytes depend on sizeOfEntryPos
};

struct SpecifierEntryByObjectIdInListTypeRoot {
     uint64_t rootListId = 0;     // Use uint64_t, actual bytes depend on sizeOfListId
     uint8_t listType = 0;
     uint64_t objectId = 0;       // Use uint64_t, actual bytes depend on sizeOfObjectId
};

struct SpecifierEntryByTypeCreate {
     uint8_t entryType = 0;
};

struct SpecifierEntryByObjectIdGeneral {
     uint64_t objectId = 0; // Use uint64_t, actual bytes depend on sizeOfObjectId
};

// TODO: Add structs for 0x24, 0x25 if full implementation is needed later.
// They involve subunit specifiers (which might be std::vector<uint8_t> or a parsed struct).
// struct SpecifierEntryByObjectIdInSubunit { ... };
// struct SpecifierEntryByObjectIdInSubunitListTypeRoot { ... };

// --- Result structure for Parsing ---

struct ParsedDescriptorSpecifier {
    DescriptorSpecifierType type = DescriptorSpecifierType::Unknown;
    size_t consumedSize = 0; // How many bytes this specifier occupied in the buffer

    // Use std::variant to hold the specific data based on type
    std::variant<
        std::monostate, // Represents no specific data or parse error
        SpecifierUnitSubunit,
        SpecifierListById,
        SpecifierListByType,
        SpecifierEntryByPosition,
        SpecifierEntryByObjectIdInListTypeRoot,
        SpecifierEntryByTypeCreate,
        SpecifierEntryByObjectIdGeneral
        // Add other specifier data structs here when defined
    > specificData;

    // Default constructor
    ParsedDescriptorSpecifier() = default;

    // Constructor for initialization
    ParsedDescriptorSpecifier(DescriptorSpecifierType t, size_t size = 0)
        : type(t), consumedSize(size), specificData(std::monostate{}) {}
};



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
static std::vector<uint8_t> makeDescriptorSpecifier(
    DescriptorSpecifierType type,
    size_t sizeOfListId,
    size_t sizeOfObjectId,
    size_t sizeOfEntryPos,
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
    size_t sizeOfEntryPos
);

/**
 * @brief Calculates the expected size of a descriptor specifier based on its type
 *        and the dynamic sizes provided by the target.
 * @param buffer Pointer to the start of the buffer containing the specifier type (at least).
 * @param bufferLen The maximum length of the buffer available.
 * @param sizeOfListId Size in bytes for list IDs (obtained from target).
 * @param sizeOfObjectId Size in bytes for object IDs (obtained from target).
 * @param sizeOfEntryPos Size in bytes for entry positions (obtained from target).
 * @return The expected size in bytes, or 0 if the type is unknown/invalid or buffer too short for type byte.
 */
static size_t expectedDescriptorSpecifierSize(
    const uint8_t* buffer,
    size_t bufferLen,
    size_t sizeOfListId,
    size_t sizeOfObjectId,
    size_t sizeOfEntryPos
);

// Helper for appending bytes (MSB first)
static bool appendBytes(std::vector<uint8_t>& vec, uint64_t value, size_t numBytes);

// Helper for writing multi-byte values (MSB first)
static void writeMultiByteValue(std::vector<uint8_t>& vec, uint64_t val, size_t numBytes);

// Helper for reading multi-byte values (MSB first)
static uint64_t readMultiByteValue(const uint8_t* buffer, size_t numBytes);

// Helper to get effective size using default if needed
static size_t getEffectiveSize(size_t dynamicSize, size_t defaultSize);






}; // class DescriptorSpecifier
