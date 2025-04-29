// MusicSubunitDescriptorParser.hpp
#pragma once

#include "FWA/Error.h"
#include "FWA/Enums.hpp"
#include <expected>
#include <vector>
#include <cstdint>

namespace FWA {

class DescriptorAccessor;
class MusicSubunit;
class AVCInfoBlock;

class MusicSubunitDescriptorParser {
public:
    explicit MusicSubunitDescriptorParser(DescriptorAccessor& descriptorAccessor);
    ~MusicSubunitDescriptorParser() = default;
    std::expected<void, IOKitError> fetchAndParse(MusicSubunit& musicSubunit);

    // Parse the Music Subunit Status Descriptor (info blocks)
    std::expected<void, IOKitError> parseMusicSubunitStatusDescriptor(
        const std::vector<uint8_t>& descriptorData,
        MusicSubunit& musicSubunit);

    // Parse the Music Subunit Identifier Descriptor (capabilities)
    std::expected<void, IOKitError> parseMusicSubunitIdentifierDescriptor(
        const std::vector<uint8_t>& descriptorData,
        MusicSubunit& musicSubunit);

    // std::expected<void,IOKitError> fetchAndParse(MusicSubunit& musicSubunit);

    MusicSubunitDescriptorParser(const MusicSubunitDescriptorParser&) = delete;
    MusicSubunitDescriptorParser& operator=(const MusicSubunitDescriptorParser&) = delete;
    MusicSubunitDescriptorParser(MusicSubunitDescriptorParser&&) = delete;
    MusicSubunitDescriptorParser& operator=(MusicSubunitDescriptorParser&&) = delete;

private:
    DescriptorAccessor& descriptorAccessor_;
};

} // namespace FWA