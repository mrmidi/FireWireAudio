#include "FWA/DescriptorUtils.hpp"
#include "FWA/DescriptorSpecifier.hpp"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace FWA {

std::vector<uint8_t> DescriptorUtils::buildDescriptorSpecifier(
    DescriptorSpecifierType type,
    size_t sizeOfListId,
    size_t sizeOfObjectId,
    size_t sizeOfEntryPos,
    std::optional<uint64_t> listId,
    std::optional<uint64_t> objectId,
    std::optional<uint64_t> entryPosition,
    std::optional<uint8_t> listOrEntryType,
    std::optional<uint64_t> rootListId,
    const std::optional<std::vector<uint8_t>>& subunitSpecifier)
{
    std::vector<uint8_t> specifier;
    specifier.push_back(static_cast<uint8_t>(type));

    size_t effListIdSize = getEffectiveSize(sizeOfListId, DEFAULT_SIZE_OF_LIST_ID);
    size_t effObjectIdSize = getEffectiveSize(sizeOfObjectId, DEFAULT_SIZE_OF_OBJECT_ID);
    size_t effEntryPosSize = getEffectiveSize(sizeOfEntryPos, DEFAULT_SIZE_OF_ENTRY_POS);

    bool success = true;

    switch (type) {
        case DescriptorSpecifierType::UnitSubunitIdentifier: // 0x00
            break;
        case DescriptorSpecifierType::ListById: // 0x10
            if (!listId || effListIdSize == 0) success = false;
            else success = appendBytes(specifier, listId.value(), effListIdSize);
            break;
        case DescriptorSpecifierType::ListByType: // 0x11
            if (!listOrEntryType) success = false;
            else specifier.push_back(listOrEntryType.value());
            break;
        case DescriptorSpecifierType::EntryByPositionInListId: // 0x20
            if (!listId || !entryPosition || effListIdSize == 0 || effEntryPosSize == 0) success = false;
            else {
                success &= appendBytes(specifier, listId.value(), effListIdSize);
                success &= appendBytes(specifier, entryPosition.value(), effEntryPosSize);
            }
            break;
        case DescriptorSpecifierType::EntryByObjectIdInListTypeRoot: // 0x21
            if (!rootListId || !listOrEntryType || !objectId || effListIdSize == 0 || effObjectIdSize == 0) success = false;
            else {
                success &= appendBytes(specifier, rootListId.value(), effListIdSize);
                specifier.push_back(listOrEntryType.value());
                success &= appendBytes(specifier, objectId.value(), effObjectIdSize);
            }
            break;
        case DescriptorSpecifierType::EntryByTypeCreate: // 0x22
            if (!listOrEntryType) success = false;
            else specifier.push_back(listOrEntryType.value());
            break;
        case DescriptorSpecifierType::EntryByObjectIdGeneral: // 0x23
            if (!objectId || effObjectIdSize == 0) success = false;
            else success = appendBytes(specifier, objectId.value(), effObjectIdSize);
            break;
        case DescriptorSpecifierType::EntryByObjectIdInSubunitListTypeRoot: // 0x24
            if (!subunitSpecifier || !rootListId || !listOrEntryType || !objectId || effListIdSize == 0 || effObjectIdSize == 0) success = false;
            else {
                specifier.insert(specifier.end(), subunitSpecifier->begin(), subunitSpecifier->end());
                success &= appendBytes(specifier, rootListId.value(), effListIdSize);
                specifier.push_back(listOrEntryType.value());
                success &= appendBytes(specifier, objectId.value(), effObjectIdSize);
            }
            break;
        case DescriptorSpecifierType::EntryByObjectIdInSubunit: // 0x25
            if (!subunitSpecifier || !objectId || effObjectIdSize == 0) success = false;
            else {
                specifier.insert(specifier.end(), subunitSpecifier->begin(), subunitSpecifier->end());
                success &= appendBytes(specifier, objectId.value(), effObjectIdSize);
            }
            break;
        case DescriptorSpecifierType::SubunitDependentStart: // Range 0x80 - 0xBF
        case DescriptorSpecifierType::SubunitDependentEnd:
            spdlog::debug("buildDescriptorSpecifier: Built subunit-dependent specifier 0x{:02x} with type byte only.", static_cast<uint8_t>(type));
            break;
        default:
            spdlog::error("buildDescriptorSpecifier: Unknown or unsupported type 0x{:02x}", static_cast<uint8_t>(type));
            success = false;
            break;
    }
    if (!success) {
        spdlog::error("Failed to build specifier for type 0x{:02x} (missing args or unsupported size?)", static_cast<uint8_t>(type));
        return {}; // Return empty vector for error
    }
    return specifier;
}

ParsedDescriptorSpecifier DescriptorUtils::parseDescriptorSpecifier(
    const uint8_t* buffer,
    size_t bufferLen,
    size_t sizeOfListId,
    size_t sizeOfObjectId,
    size_t sizeOfEntryPos)
{
    if (!buffer || bufferLen < 1) {
        return {};
    }
    DescriptorSpecifierType type = static_cast<DescriptorSpecifierType>(buffer[0]);
    ParsedDescriptorSpecifier result(type, 1);
    const uint8_t* currentPtr = buffer + 1;
    size_t remainingLen = bufferLen - 1;
    try {
        switch (type) {
            case DescriptorSpecifierType::UnitSubunitIdentifier:
                result.specificData = SpecifierUnitSubunit{};
                break;
            case DescriptorSpecifierType::ListById:
                if (DEFAULT_SIZE_OF_LIST_ID == 2) {
                    if (remainingLen < 2) throw std::runtime_error("Buffer too short for ListById");
                    result.specificData = SpecifierListById{ readBytes(currentPtr, 2) };
                    result.consumedSize += 2;
                } else {
                    throw std::runtime_error("Unsupported list_ID size");
                }
                break;
            case DescriptorSpecifierType::ListByType:
                if (remainingLen < 1) throw std::runtime_error("Buffer too short for ListByType");
                result.specificData = SpecifierListByType{ *currentPtr };
                result.consumedSize += 1;
                break;
            case DescriptorSpecifierType::EntryByPositionInListId: {
                SpecifierEntryByPosition data;
                if (DEFAULT_SIZE_OF_LIST_ID == 2) {
                    if (remainingLen < 2) throw std::runtime_error("Buffer too short for ListId in EntryByPos");
                    data.listId = readBytes(currentPtr, 2);
                    result.consumedSize += 2;
                    currentPtr += 2;
                    remainingLen -= 2;
                } else {
                    throw std::runtime_error("Unsupported list_ID size");
                }
                if (DEFAULT_SIZE_OF_ENTRY_POS == 2) {
                    if (remainingLen < 2) throw std::runtime_error("Buffer too short for EntryPos in EntryByPos");
                    data.entryPosition = readBytes(currentPtr, 2);
                    result.consumedSize += 2;
                } else {
                    throw std::runtime_error("Unsupported entry_pos size");
                }
                result.specificData = data;
                break;
            }
            case DescriptorSpecifierType::SubunitDependentStart:
            case DescriptorSpecifierType::SubunitDependentEnd:
                spdlog::debug("parseDescriptorSpecifier: Encountered subunit-dependent type 0x{:02x}. Cannot parse specific fields.", static_cast<uint8_t>(type));
                result.specificData = std::monostate{};
                break;
            default:
                spdlog::warn("parseDescriptorSpecifier: Unknown or unsupported type 0x{:02x}", static_cast<uint8_t>(type));
                result.type = DescriptorSpecifierType::Unknown;
                result.consumedSize = 0;
                result.specificData = std::monostate{};
                break;
        }
    } catch (const std::runtime_error& e) {
        spdlog::error("parseDescriptorSpecifier: Error parsing type 0x{:02x}: {}", static_cast<uint8_t>(type), e.what());
        result.type = DescriptorSpecifierType::Unknown;
        result.consumedSize = 0;
        result.specificData = std::monostate{};
    }
    return result;
}

size_t DescriptorUtils::getDescriptorSpecifierExpectedSize(
    const uint8_t* buffer, size_t bufferLen,
    size_t sizeOfListId,
    size_t sizeOfObjectId,
    size_t sizeOfEntryPos) {
    if (!buffer || bufferLen < 1) {
        return 0;
    }
    DescriptorSpecifierType type = static_cast<DescriptorSpecifierType>(buffer[0]);
    size_t expectedSize = 1;
    switch (type) {
        case DescriptorSpecifierType::UnitSubunitIdentifier:
            break;
        case DescriptorSpecifierType::ListById:
            expectedSize += DEFAULT_SIZE_OF_LIST_ID;
            break;
        case DescriptorSpecifierType::ListByType:
            expectedSize += 1;
            break;
        case DescriptorSpecifierType::EntryByPositionInListId:
            expectedSize += DEFAULT_SIZE_OF_LIST_ID;
            expectedSize += DEFAULT_SIZE_OF_ENTRY_POS;
            break;
        case DescriptorSpecifierType::SubunitDependentStart:
        case DescriptorSpecifierType::SubunitDependentEnd:
            spdlog::debug("getDescriptorSpecifierExpectedSize: Subunit-dependent type 0x{:02x}, returning base size 1.", static_cast<uint8_t>(type));
            break;
        default:
            spdlog::warn("getDescriptorSpecifierExpectedSize: Unknown or unsupported type 0x{:02x}", static_cast<uint8_t>(type));
            return 0;
    }
    return expectedSize;
}

// Helper for writing multi-byte values (MSB first)
bool DescriptorUtils::appendBytes(std::vector<uint8_t>& vec, uint64_t val, size_t numBytes) {
    if (numBytes == 0 || numBytes > 8) return false;
    for (size_t i = 0; i < numBytes; ++i) {
        vec.push_back(static_cast<uint8_t>((val >> ((numBytes - 1 - i) * 8)) & 0xFF));
    }
    return true;
}

// Helper for reading multi-byte values (MSB first)
uint64_t DescriptorUtils::readBytes(const uint8_t* buffer, size_t numBytes) {
    uint64_t val = 0;
    for (size_t i = 0; i < numBytes; ++i) {
        val = (val << 8) | buffer[i];
    }
    return val;
}

// Helper to get effective size using default if needed
size_t DescriptorUtils::getEffectiveSize(size_t dynamicSize, size_t defaultSize) {
    return dynamicSize == 0 ? defaultSize : dynamicSize;
}

} // namespace FWA
