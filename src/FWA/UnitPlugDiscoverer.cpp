#include "FWA/UnitPlugDiscoverer.hpp"
#include "FWA/CommandInterface.h"
#include <spdlog/spdlog.h>
#include <IOKit/avc/IOFireWireAVCConsts.h>
#include <vector>

namespace FWA {

// Constructor
UnitPlugDiscoverer::UnitPlugDiscoverer(CommandInterface* commandInterface)
    : commandInterface_(commandInterface) {
    if (!commandInterface_) {
        // Consider throwing or logging a fatal error if needed
        spdlog::critical("UnitPlugDiscoverer: CommandInterface pointer is null.");
        throw std::runtime_error("UnitPlugDiscoverer requires a valid CommandInterface.");
    }

}


std::expected<void, IOKitError> UnitPlugDiscoverer::discoverUnitPlugs(DeviceInfo& info) {
    spdlog::info("UnitPlugDiscoverer: Discovering unit plugs...");

    std::vector<uint8_t> cmd = {
        kAVCStatusInquiryCommand,
        0xFF,
        0x02,
        0x00,
        0xFF, 0xFF, 0xFF, 0xFF
    };

    auto respResult = commandInterface_->sendCommand(cmd);
    if (!respResult) {
        spdlog::error("UnitPlugDiscoverer: Failed to send unit plug discovery command.");
        return std::unexpected(respResult.error());
    }
    const auto& response = respResult.value();
    if (response.size() < 8 || (response[0] != kAVCImplementedStatus && response[0] != kAVCAcceptedStatus)) {
        spdlog::error("UnitPlugDiscoverer: Discovery command returned unexpected status or insufficient bytes.");
        return std::unexpected(IOKitError(kIOReturnError));
    }

    info.numIsoInPlugs_  = response[4];
    info.numIsoOutPlugs_ = response[5];
    info.numExtInPlugs_  = response[6];
    info.numExtOutPlugs_ = response[7];

    spdlog::info("UnitPlugDiscoverer: Discovered unit plugs: IsoIn = {}, IsoOut = {}, ExtIn = {}, ExtOut = {}",
                 info.getNumIsoInputPlugs(), info.getNumIsoOutputPlugs(),
                 info.getNumExternalInputPlugs(), info.getNumExternalOutputPlugs());

    return {};
}

} // namespace FWA
