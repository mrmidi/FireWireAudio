#pragma once

#include "FWA/Error.h"
#include "FWA/Enums.hpp"
#include <expected>
#include <vector>
#include <cstdint>

namespace FWA {

class DescriptorReader;
class MusicSubunit;
class AVCInfoBlock;

class MusicSubunitDescriptorParser {
public:
    explicit MusicSubunitDescriptorParser(DescriptorReader& descriptorReader);
    ~MusicSubunitDescriptorParser() = default;
    std::expected<void, IOKitError> fetchAndParse(MusicSubunit& musicSubunit);
    MusicSubunitDescriptorParser(const MusicSubunitDescriptorParser&) = delete;
    MusicSubunitDescriptorParser& operator=(const MusicSubunitDescriptorParser&) = delete;
    MusicSubunitDescriptorParser(MusicSubunitDescriptorParser&&) = delete;
    MusicSubunitDescriptorParser& operator=(MusicSubunitDescriptorParser&&) = delete;
private:
    DescriptorReader& descriptorReader_;
    std::expected<void, IOKitError> parseMusicSubunitStatusDescriptor(
        const std::vector<uint8_t>& descriptorData,
        MusicSubunit& musicSubunit);
    static uint8_t getSubunitAddress(SubunitType type, uint8_t id);
};

} // namespace FWA
