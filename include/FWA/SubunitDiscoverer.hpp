#pragma once

#include "FWA/Error.h"
#include "FWA/Enums.hpp"
#include <expected>
#include <cstdint>
#include <vector>

namespace FWA {

class CommandInterface;
class DeviceInfo;

class SubunitDiscoverer {
public:
    explicit SubunitDiscoverer(CommandInterface* commandInterface);
    ~SubunitDiscoverer() = default;
    std::expected<void, IOKitError> discoverSubunits(DeviceInfo& info);
    std::expected<void, IOKitError> queryPlugCounts(DeviceInfo& info);
    SubunitDiscoverer(const SubunitDiscoverer&) = delete;
    SubunitDiscoverer& operator=(const SubunitDiscoverer&) = delete;
    SubunitDiscoverer(SubunitDiscoverer&&) = delete;
    SubunitDiscoverer& operator=(SubunitDiscoverer&&) = delete;
private:
    CommandInterface* commandInterface_;
};

} // namespace FWA
