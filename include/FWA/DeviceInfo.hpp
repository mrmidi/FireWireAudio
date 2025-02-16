#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include "FWA/AudioDevice.h"
#include "FWA/AudioPlug.hpp"
#include "FWA/AudioStreamFormat.hpp"
#include "FWA/Subunit.hpp"
#include "FWA/AVCInfoBlock.hpp"

namespace FWA {

/**
 * @brief Container class for device capabilities and configuration information
 * 
 * This class holds the discovered capabilities, subunits, and configuration
 * information for a FireWire audio device.
 */
class DeviceInfo {
    public:
        DeviceInfo() = default;
        ~DeviceInfo() = default;
    
        std::shared_ptr<MusicSubunit> musicSubunit = std::make_shared<MusicSubunit>();  ///< Music subunit capabilities
        std::shared_ptr<AudioSubunit> audioSubunit = std::make_shared<AudioSubunit>();  ///< Audio subunit capabilities
    
        std::vector<std::shared_ptr<AVCInfoBlock>> infoBlocks;  ///< Parsed AV/C info blocks
};

} // namespace FWA
