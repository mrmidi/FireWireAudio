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

class DeviceInfo {
    public:
        DeviceInfo() = default;
        ~DeviceInfo() = default;
    
        // Subunit information.
        std::shared_ptr<MusicSubunit> musicSubunit = std::make_shared<MusicSubunit>();
        std::shared_ptr<AudioSubunit> audioSubunit = std::make_shared<AudioSubunit>();
    
        // Parsed AV/C info blocks.
        std::vector<std::shared_ptr<AVCInfoBlock>> infoBlocks;
    };

} // namespace FWA
