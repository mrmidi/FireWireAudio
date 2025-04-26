#ifndef FWADRIVERDEVICE_HPP
#define FWADRIVERDEVICE_HPP

#include <aspl/Device.hpp>
#include <memory>

class FWADriverDevice : public aspl::Device {
public:
    FWADriverDevice(std::shared_ptr<const aspl::Context> context,
                    const aspl::DeviceParameters& params);
    UInt32 GetTransportType() const override {
        return kAudioDeviceTransportTypeFireWire;
    }
};

#endif // FWADRIVERDEVICE_HPP
