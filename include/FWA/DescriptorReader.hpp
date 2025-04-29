#pragma once

#include "FWA/Error.h"
#include "FWA/DescriptorSpecifier.hpp"
#include <expected>
#include <vector>
#include <cstdint>

namespace FWA {

class CommandInterface;

class DescriptorReader {
public:
    explicit DescriptorReader(CommandInterface* commandInterface);
    ~DescriptorReader() = default;
    std::expected<std::vector<uint8_t>, IOKitError> readDescriptor(
        uint8_t subunitAddr,
        DescriptorSpecifierType descriptorSpecifierType,
        const std::vector<uint8_t>& descriptorSpecifierSpecificData = {});
    DescriptorReader(const DescriptorReader&) = delete;
    DescriptorReader& operator=(const DescriptorReader&) = delete;
    DescriptorReader(DescriptorReader&&) = delete;
    DescriptorReader& operator=(DescriptorReader&&) = delete;
private:
    CommandInterface* commandInterface_;
};

} // namespace FWA
