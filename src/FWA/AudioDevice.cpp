#include "FWA/AudioDevice.h"
#include "FWA/CommandInterface.h"
#include "FWA/DeviceParser.hpp"
#include "FWA/DeviceController.h"
#include <spdlog/spdlog.h>
#include <IOKit/IOMessage.h>

namespace FWA {

AudioDevice::AudioDevice(std::uint64_t guid,
                         const std::string& deviceName,
                         const std::string& vendorName,
                         io_service_t avcUnit,
                         DeviceController *deviceController)
    : guid_(guid),
      deviceName_(deviceName),
      vendorName_(vendorName),
      avcUnit_(avcUnit),
      deviceController_(deviceController)
{
    spdlog::info("AudioDevice::AudioDevice - Creating device with GUID: 0x{:x}", guid);

    if (avcUnit_) {
        IOObjectRetain(avcUnit_);
    }
    // Do NOT call shared_from_this() here!
}

AudioDevice::~AudioDevice()
{
    if (avcUnit_) {
        IOObjectRelease(avcUnit_);
        avcUnit_ = 0;
    }
    if (fwUnit_) {
        IOObjectRelease(fwUnit_);
        fwUnit_ = 0;
    }
    if (fwDevice_) {
        IOObjectRelease(fwDevice_);
        fwDevice_ = 0;
    }
    if (busController_) {
        IOObjectRelease(busController_);
        busController_ = 0;
    }
    if (interestNotification_) {
        IOObjectRelease(interestNotification_);
        interestNotification_ = 0;
    }
    if (notificationPort_) {
        IONotificationPortDestroy(notificationPort_);
        notificationPort_ = nullptr;
    }
}

std::expected<void, IOKitError> AudioDevice::init()
{

    // 1. Retrieve the FireWire objects.
    IOReturn result = IORegistryEntryGetParentEntry(avcUnit_, kIOServicePlane, &fwUnit_);
    if (result != kIOReturnSuccess) {
        spdlog::error("AudioDevice::init: Failed to get fwUnit: 0x{:x}", result);
        return std::unexpected(static_cast<IOKitError>(result));
    }

    result = IORegistryEntryGetParentEntry(fwUnit_, kIOServicePlane, &fwDevice_);
    if (result != kIOReturnSuccess) {
        spdlog::error("AudioDevice::init: Failed to get fwDevice: 0x{:x}", result);
        IOObjectRelease(fwUnit_);
        return std::unexpected(static_cast<IOKitError>(result));
    }
    result = IORegistryEntryGetParentEntry(fwDevice_, kIOServicePlane, &busController_);
    if (result != kIOReturnSuccess) {
        spdlog::error("AudioDevice::init: Failed to get busController: 0x{:x}", result);
        IOObjectRelease(fwUnit_);
        IOObjectRelease(fwDevice_);
        return std::unexpected(static_cast<IOKitError>(result));
    }

    // 2. Create the notification port.
    notificationPort_ = IONotificationPortCreate(kIOMainPortDefault);
    if (!notificationPort_) {
        spdlog::error("AudioDevice::init: Failed to create notification port");
        return std::unexpected(IOKitError::Error);
    }
    
    // create interfaces
    result = createFWDeviceInterface();
    
    if (result != kIOReturnSuccess) {
        spdlog::error("AudioDevice::init: Failed to create FW device interface: 0x{:x}", result);
    }

    // CFRunLoopAddSource(CFRunLoopGetCurrent(),
    //                    IONotificationPortGetRunLoopSource(notificationPort_),
    //                    kCFRunLoopDefaultMode);

  
    // Do not discover capabilities for now
    
    // 3. Now safely create the CommandInterface using shared_from_this().
    
    commandInterface_ = std::make_shared<CommandInterface>(shared_from_this());
//    
//    commandInterface_->activate();

    // 4. Discover device capabilities using DeviceParser.
//    spdlog::info("Starting device capability discovery...");
//    auto parser = std::make_shared<DeviceParser>(shared_from_this());
//    auto parseResult = parser->parse();
//    if (!parseResult) {
//        spdlog::error("Device capability parsing failed.");
//        return std::unexpected(parseResult.error());
//    }
//    spdlog::info("Device capability parsing completed successfully.");

    // readVendorAndModelInfo();

    return {};
}

IOReturn AudioDevice::createFWDeviceInterface() {
    // Local Vars
    IOCFPlugInInterface **theCFPlugInInterface;
    SInt32 theScore;
    IOReturn result = kIOReturnSuccess;

    result = IOCreatePlugInInterfaceForService(
                                               fwDevice_,
                                            kIOFireWireLibTypeID,
                                            kIOCFPlugInInterfaceID,        //interfaceType,
                                            &theCFPlugInInterface,
                                            &theScore);
    if (!result)
    {
        HRESULT comErr;
        comErr = (*theCFPlugInInterface)->QueryInterface(
                                                   theCFPlugInInterface,
                                                   CFUUIDGetUUIDBytes( kIOFireWireNubInterfaceID ),
                                                   (void**) &deviceInterface);
        if (comErr == S_OK)
        {
            result = (*deviceInterface)->AddCallbackDispatcherToRunLoop(deviceInterface, deviceController_->getRunLoopRef());
        }
        else
            result = comErr;

        (*theCFPlugInInterface)->Release(theCFPlugInInterface);    // Leave just one reference.

        // Open the interface
        if (!result)
        {
            // If the avc interface is already open, use it's session ref to open the device interface
            if (avcInterface_)
                result = (*deviceInterface)->OpenWithSessionRef(deviceInterface,
                                                    (*avcInterface_)->getSessionRef(avcInterface_));
            else
                result = (*deviceInterface)->Open(deviceInterface);
            if (result)
            {
                (*deviceInterface)->Release(deviceInterface) ;
                deviceInterface = 0;
            }
        }
    }

    return result;
    
}

IOReturn AudioDevice::readVendorAndModelInfo() {
    CFMutableDictionaryRef properties = nullptr;
    IOReturn result = IORegistryEntryCreateCFProperties(fwUnit_, &properties, kCFAllocatorDefault, 0);
    if (result == kIOReturnSuccess && properties) {
        CFNumberRef unitVendorID = (CFNumberRef)CFDictionaryGetValue(properties, CFSTR("Vendor_ID"));
        if (unitVendorID) {
            UInt32 vendorID = 0;
            CFNumberGetValue(unitVendorID, kCFNumberLongType, &vendorID);
            spdlog::info(" *** Vendor ID: 0x{:08x}", vendorID);
        }
        CFNumberRef unitModelID = (CFNumberRef)CFDictionaryGetValue(properties, CFSTR("Model_ID"));
        if (unitModelID) {
            UInt32 modelID = 0;
            CFNumberGetValue(unitModelID, kCFNumberLongType, &modelID);
            spdlog::info(" *** Model ID: 0x{:08x}", modelID);
        }
        CFStringRef unitVendorStrDesc = (CFStringRef)CFDictionaryGetValue(properties, CFSTR("FireWire Vendor Name"));
        if (unitVendorStrDesc) {
            char vbuf[256];
            if (CFStringGetCString(unitVendorStrDesc, vbuf, sizeof(vbuf), kCFStringEncodingMacRoman))
                vendorName_ = vbuf;
        }
        CFRelease(properties);
    }
    return result;
}

} // namespace FWA
