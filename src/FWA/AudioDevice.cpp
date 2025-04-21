#include "FWA/AudioDevice.h"
#include "FWA/CommandInterface.h"
#include "FWA/DeviceParser.hpp"
#include "FWA/DeviceController.h"
#include "FWA/Helpers.h" // For cfStringToString
#include <spdlog/spdlog.h>
#include <IOKit/IOMessage.h>
#include <CoreFoundation/CFNumber.h> // For CFNumber*

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
    spdlog::debug("AudioDevice::init - Initializing device GUID: 0x{:x}", guid_);
    // 1. Retrieve the FireWire objects.
    IOReturn result = IORegistryEntryGetParentEntry(avcUnit_, kIOServicePlane, &fwUnit_);
    if (result != kIOReturnSuccess || !fwUnit_) {
        spdlog::error("AudioDevice::init: Failed to get fwUnit: 0x{:x}", result);
        return std::unexpected(static_cast<IOKitError>(result != kIOReturnSuccess ? result : kIOReturnNotFound));
    }
    spdlog::debug("AudioDevice::init: Got fwUnit_ service: {}", (void*)fwUnit_);

    result = IORegistryEntryGetParentEntry(fwUnit_, kIOServicePlane, &fwDevice_);
    if (result != kIOReturnSuccess || !fwDevice_) {
        spdlog::error("AudioDevice::init: Failed to get fwDevice: 0x{:x}", result);
        return std::unexpected(static_cast<IOKitError>(result != kIOReturnSuccess ? result : kIOReturnNotFound));
    }
    spdlog::debug("AudioDevice::init: Got fwDevice_ service: {}", (void*)fwDevice_);

    result = IORegistryEntryGetParentEntry(fwDevice_, kIOServicePlane, &busController_);
    if (result != kIOReturnSuccess || !busController_) {
        spdlog::error("AudioDevice::init: Failed to get busController: 0x{:x}", result);
        return std::unexpected(static_cast<IOKitError>(result != kIOReturnSuccess ? result : kIOReturnNotFound));
    }
    spdlog::debug("AudioDevice::init: Got busController_ service: {}", (void*)busController_);

    // ---- Call the new method to read Vendor/Model Info ----
    IOReturn infoResult = readVendorAndModelInfo();
    if (infoResult != kIOReturnSuccess) {
        spdlog::warn("AudioDevice::init: Failed to read vendor/model info: 0x{:x}. Continuing initialization.", infoResult);
    } else {
        spdlog::info("AudioDevice::init: Read VendorID=0x{:08X}, ModelID=0x{:08X}, VendorName='{}'", vendorID_, modelID_, vendorName_);
    }
    // ---------------------------------------------------------

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
    if (!fwUnit_) {
        spdlog::error("AudioDevice::readVendorAndModelInfo: fwUnit_ is null.");
        return kIOReturnNotReady;
    }
    CFMutableDictionaryRef properties = nullptr;
    IOReturn result = IORegistryEntryCreateCFProperties(fwUnit_, &properties, kCFAllocatorDefault, kNilOptions);
    if (result != kIOReturnSuccess) {
        spdlog::error("AudioDevice::readVendorAndModelInfo: IORegistryEntryCreateCFProperties failed: 0x{:x}", result);
        return result;
    }
    if (!properties) {
        spdlog::error("AudioDevice::readVendorAndModelInfo: IORegistryEntryCreateCFProperties succeeded but returned null properties.");
        return kIOReturnNotFound;
    }
    // Get Vendor ID
    CFNumberRef unitVendorIDRef = (CFNumberRef)CFDictionaryGetValue(properties, CFSTR("Vendor_ID"));
    if (unitVendorIDRef) {
        if (CFGetTypeID(unitVendorIDRef) == CFNumberGetTypeID()) {
            if (!CFNumberGetValue(unitVendorIDRef, kCFNumberLongType, &vendorID_)) {
                spdlog::warn("AudioDevice::readVendorAndModelInfo: CFNumberGetValue failed for Vendor_ID.");
            }
            spdlog::debug("AudioDevice::readVendorAndModelInfo: Found Vendor ID: 0x{:08X}", vendorID_);
        } else {
            spdlog::warn("AudioDevice::readVendorAndModelInfo: Vendor_ID property is not a CFNumber.");
        }
    } else {
        spdlog::warn("AudioDevice::readVendorAndModelInfo: Vendor_ID property not found.");
    }
    // Get Model ID
    CFNumberRef unitModelIDRef = (CFNumberRef)CFDictionaryGetValue(properties, CFSTR("Model_ID"));
    if (unitModelIDRef) {
        if (CFGetTypeID(unitModelIDRef) == CFNumberGetTypeID()) {
            if (!CFNumberGetValue(unitModelIDRef, kCFNumberLongType, &modelID_)) {
                spdlog::warn("AudioDevice::readVendorAndModelInfo: CFNumberGetValue failed for Model_ID.");
            }
            spdlog::debug("AudioDevice::readVendorAndModelInfo: Found Model ID: 0x{:08X}", modelID_);
        } else {
            spdlog::warn("AudioDevice::readVendorAndModelInfo: Model_ID property is not a CFNumber.");
        }
    } else {
        spdlog::warn("AudioDevice::readVendorAndModelInfo: Model_ID property not found.");
    }
    // Get Vendor Name
    CFStringRef unitVendorStrDesc = (CFStringRef)CFDictionaryGetValue(properties, CFSTR("FireWire Vendor Name"));
    if (unitVendorStrDesc) {
        if (CFGetTypeID(unitVendorStrDesc) == CFStringGetTypeID()) {
            std::string name = Helpers::cfStringToString(unitVendorStrDesc);
            if (!name.empty()) {
                vendorName_ = name;
                spdlog::debug("AudioDevice::readVendorAndModelInfo: Found Vendor Name: '{}'", vendorName_);
            } else {
                spdlog::warn("AudioDevice::readVendorAndModelInfo: cfStringToString failed for FireWire Vendor Name.");
            }
        } else {
            spdlog::warn("AudioDevice::readVendorAndModelInfo: FireWire Vendor Name property is not a CFString.");
        }
    } else {
        spdlog::warn("AudioDevice::readVendorAndModelInfo: FireWire Vendor Name property not found.");
    }
    CFRelease(properties);
    return kIOReturnSuccess;
}

} // namespace FWA
