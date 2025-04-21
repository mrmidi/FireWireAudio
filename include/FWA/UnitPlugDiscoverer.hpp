#pragma once

#include "FWA/Error.h"
#include <expected>
#include <cstdint>
#include <vector>
#include "FWA/DeviceInfo.hpp"

namespace FWA {

class CommandInterface;
class DeviceInfo;

class UnitPlugDiscoverer {
public:
    explicit UnitPlugDiscoverer(CommandInterface* commandInterface);
    ~UnitPlugDiscoverer() = default;
    std::expected<void, IOKitError> discoverUnitPlugs(DeviceInfo& info);
    UnitPlugDiscoverer(const UnitPlugDiscoverer&) = delete;
    UnitPlugDiscoverer& operator=(const UnitPlugDiscoverer&) = delete;
    UnitPlugDiscoverer(UnitPlugDiscoverer&&) = delete;
    UnitPlugDiscoverer& operator=(UnitPlugDiscoverer&&) = delete;
private:
    CommandInterface* commandInterface_;
};

} // namespace FWA
