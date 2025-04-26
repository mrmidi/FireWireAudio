#include "FWADriverDevice.hpp"

FWADriverDevice::FWADriverDevice(std::shared_ptr<const aspl::Context> context,
                                 const aspl::DeviceParameters& params)
    : aspl::Device(context, params) // Forward to base class
{
    // Custom initialization if needed
}
