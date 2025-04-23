#include "FWA/IOKitFireWireDeviceDiscovery.h"
#include "FWA/AudioDevice.h"
#include "FWA/Error.h"
#include <spdlog/spdlog.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/firewire/IOFireWireLib.h>
#include <IOKit/avc/IOFireWireAVCLib.h>
#include <IOKit/IOMessage.h> 
#include <iostream>
#include <mach/mach.h>
#include <algorithm>
#include "FWA/Helpers.h"
#include "FWA/DeviceController.h" // Include full definition
#include <spdlog/fmt/fmt.h> // For fmt::ptr


namespace FWA {

IOKitFireWireDeviceDiscovery::IOKitFireWireDeviceDiscovery(std::shared_ptr<DeviceController> deviceController) :
    masterPort_(MACH_PORT_NULL),
    notifyPort_(nullptr),
    runLoopSource_(nullptr),
    deviceIterator_(0),
    deviceController_(deviceController),
    callback_(nullptr)
{
}

IOKitFireWireDeviceDiscovery::~IOKitFireWireDeviceDiscovery() {
    stopDiscovery();
}

std::expected<void, IOKitError> IOKitFireWireDeviceDiscovery::startDiscovery(DeviceNotificationCallback callback) {
    if (discoveryThreadRunning_) {
        return std::unexpected(static_cast<IOKitError>(kIOReturnExclusiveAccess));
    }
    
    callback_ = callback;
    discoveryThreadRunning_ = true;
    
    discoveryThread_ = std::thread(&IOKitFireWireDeviceDiscovery::discoveryThreadFunction, this);
    
    return {};
}
void IOKitFireWireDeviceDiscovery::discoveryThreadFunction() {
    
    kern_return_t kr = IOMainPort(MACH_PORT_NULL, &masterPort_);
    if (kr != KERN_SUCCESS || masterPort_ == MACH_PORT_NULL) {
        spdlog::error("discoveryThreadFunction: Failed to get IOMasterPort: {}", kr);
        discoveryThreadRunning_ = false;
        return;
    }
    notifyPort_ = IONotificationPortCreate(masterPort_);
    if (!notifyPort_) {
        spdlog::error("discoveryThreadFunction: Failed to create IONotificationPort");
        discoveryThreadRunning_ = false;
        return;
    }
    
    runLoopSource_ = IONotificationPortGetRunLoopSource(notifyPort_);
    if (!runLoopSource_) {
        spdlog::error("discoveryThreadFunction: Failed to get run loop source");
        discoveryThreadRunning_ = false;
        return;
    }
    discoveryRunLoop_ = CFRunLoopGetCurrent();
    CFRunLoopAddSource(discoveryRunLoop_, runLoopSource_, kCFRunLoopDefaultMode);
    
    CFMutableDictionaryRef matchingDictAdded = IOServiceMatching("IOFireWireAVCUnit");
    if (!matchingDictAdded)
    {
        spdlog::error("discoveryThreadFunction: cannot create matching dict");
        discoveryThreadRunning_ = false;
        return;
    }
    kern_return_t result = IOServiceAddMatchingNotification(
                                                            notifyPort_,
                                                            kIOMatchedNotification,
                                                            matchingDictAdded,
                                                            deviceAdded,
                                                            this,
                                                            &deviceIterator_
                                                            );
    if (result != KERN_SUCCESS) {
        spdlog::error("discoveryThreadFunction: Failed to add matching notification (added): {}", result);
        CFRelease(matchingDictAdded); // Release ONLY on error
        discoveryThreadRunning_ = false;
        return;
    }
    // Immediately iterate to find any already-connected devices.
    deviceAdded(this, deviceIterator_);
    
    CFRunLoopRun();
    spdlog::info("discoveryThreadFunction: Exiting run loop.");
    discoveryThreadRunning_ = false;
    
}

std::expected<void, IOKitError> IOKitFireWireDeviceDiscovery::stopDiscovery() {
    if (!discoveryThreadRunning_) {
        return {};
    }
    
    discoveryThreadRunning_ = false;
    
    if (discoveryRunLoop_) {
        CFRunLoopStop(discoveryRunLoop_);
    }
    
    if (discoveryThread_.joinable()) {
        discoveryThread_.join();
    }
    
    if (runLoopSource_) {
        CFRunLoopSourceInvalidate(runLoopSource_);
        runLoopSource_ = nullptr;
    }
    if (notifyPort_) {
        IONotificationPortDestroy(notifyPort_);
        notifyPort_ = nullptr;
    }
    if (masterPort_ != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), masterPort_);
        masterPort_ = MACH_PORT_NULL;
    }
    if (deviceIterator_) {
        IOObjectRelease(deviceIterator_);
        deviceIterator_ = 0;
    }
    
    // Clear devices under mutex protection
    {
        std::lock_guard<std::mutex> lock(devicesMutex_);
        devices_.clear();
    }
    
    callback_ = nullptr;
    return {};
}


std::expected<std::shared_ptr<AudioDevice>, IOKitError> IOKitFireWireDeviceDiscovery::getDeviceByGuid(std::uint64_t guid)
{
    std::lock_guard<std::mutex> lock(devicesMutex_); // Protect access
    auto it = std::find_if(devices_.begin(), devices_.end(),
                           [&](const auto& device) { return device->getGuid() == guid; });
    
    if (it != devices_.end()) {
        return *it;
    } else {
        return std::unexpected(static_cast<IOKitError>(kIOReturnNotFound));
    }
}


void IOKitFireWireDeviceDiscovery::deviceAdded(void* refCon, io_iterator_t iterator)
{
    IOKitFireWireDeviceDiscovery* self = static_cast<IOKitFireWireDeviceDiscovery*>(refCon);
    if (!self || !self->deviceController_) { // Check controller validity early
        spdlog::error("deviceAdded: refCon or deviceController_ is null!");
        // Potentially release iterator resources if needed before returning
        io_object_t temp_dev;
        while ((temp_dev = IOIteratorNext(iterator)) != 0) { IOObjectRelease(temp_dev); }
        return;
    }

    spdlog::info("deviceAdded called");

    io_object_t device_service; // Use a different name to avoid confusion with AudioDevice object
    while ((device_service = IOIteratorNext(iterator)) != 0) {
        spdlog::info("deviceAdded: Iterating IOKit service");

        auto guidResult = self->getDeviceGuid(device_service);
        if (!guidResult) {
            spdlog::error("deviceAdded: Failed to get GUID: 0x{:x}", static_cast<int>(guidResult.error()));
            IOObjectRelease(device_service);
            continue;
        }
        UInt64 guid = guidResult.value();
        spdlog::info("deviceAdded: Found service with GUID: 0x{:x}", guid);

        // Check if device already exists IN THE CONTROLLER
        if (self->deviceController_->getDeviceByGuid(guid)) {
            spdlog::warn("deviceAdded: Device with GUID 0x{:x} already managed by controller. Skipping.", guid);
            IOObjectRelease(device_service);
            continue;
        }

        spdlog::info("deviceAdded: Creating AudioDevice for service...");

        // Create and Initialize the AudioDevice
        // createAudioDevice does NOT add to controller anymore
        auto audioDeviceResult = self->createAudioDevice(device_service);

        if (audioDeviceResult) {
            std::shared_ptr<AudioDevice> newAudioDevice = audioDeviceResult.value();
            spdlog::info("deviceAdded: AudioDevice created and initialized successfully for GUID 0x{:x}", guid);

            // --->>> ADD DEVICE TO CONTROLLER <<<---
            spdlog::info("Adding device 0x{:x} to controller.", guid);
            self->deviceController_->addDevice(newAudioDevice);
            // TODO: Check return/status of addDevice if it provides one

            // --->>> CALL SWIFT CALLBACK (AFTER ADDING) <<<---
            if (self->callback_) {
                spdlog::info("deviceAdded: Calling notification callback for GUID 0x{:x}", guid);
                self->callback_(newAudioDevice, true); // Pass shared_ptr
            }

        } else {
            spdlog::error("deviceAdded: Failed to create/initialize AudioDevice for service: 0x{:x}", static_cast<int>(audioDeviceResult.error()));
            // Don't add or call callback if creation/init failed
        }

        // Release the io_service_t reference obtained from the iterator
        // createAudioDevice should have managed the reference it uses internally
        IOObjectRelease(device_service); // Release the iterator's reference
    }
    spdlog::info("deviceAdded: Finished iterating devices");
}

std::expected<std::shared_ptr<AudioDevice>, IOKitError>
IOKitFireWireDeviceDiscovery::createAudioDevice(io_object_t device) {
    spdlog::info("createAudioDevice: Entered function");
    
    CFMutableDictionaryRef properties = nullptr;
    IOReturn result = IORegistryEntryCreateCFProperties(device, &properties, kCFAllocatorDefault, kNilOptions);
    if (result != kIOReturnSuccess || properties == nullptr) {
        spdlog::error("createAudioDevice: Failed to get device properties: {}", result);
        if (properties) CFRelease(properties);
        return std::unexpected(static_cast<IOKitError>(result));
    }
    spdlog::info("createAudioDevice: Got device properties");
    
    // display the dictionary
    //FWA::Helpers::printCFDictionary(properties); //Keep this, VERY useful
    
    // 1. Get the GUID.
    CFNumberRef guidNumber = (CFNumberRef)CFDictionaryGetValue(properties, CFSTR("GUID"));
    if (guidNumber == nullptr) {
        spdlog::error("createAudioDevice: Device missing GUID");
        CFRelease(properties);
        return std::unexpected(static_cast<IOKitError>(kIOReturnNotFound));
    }
    spdlog::info("createAudioDevice: Got GUID CFNumberRef");
    
    UInt64 guid;
    if (!CFNumberGetValue(guidNumber, kCFNumberSInt64Type, &guid)) {
        spdlog::error("createAudioDevice: Failed to get GUID value");
        CFRelease(properties);
        return std::unexpected(static_cast<IOKitError>(kIOReturnBadArgument));
    }
    spdlog::info("createAudioDevice: Got GUID value");
    
    // 2. Get the device name.
    CFStringRef nameRef = (CFStringRef)CFDictionaryGetValue(properties, CFSTR("FireWire Product Name"));
    std::string deviceName = "Unknown Device"; // Default value.
    if (nameRef != nullptr) {
        char nameBuffer[256]; // Choose a reasonable buffer size.
        if (CFStringGetCString(nameRef, nameBuffer, sizeof(nameBuffer), kCFStringEncodingUTF8)) {
            deviceName = nameBuffer;
        }
    }
    spdlog::info("createAudioDevice: Got device name");
    
    // 3. Get the vendor name.
    CFStringRef vendorRef = (CFStringRef)CFDictionaryGetValue(properties, CFSTR("FireWire Vendor Name"));
    std::string vendorName = "Unknown Vendor"; // Default
    if (vendorRef != nullptr) {
        char vendorBuffer[256];
        if (CFStringGetCString(vendorRef, vendorBuffer, sizeof(vendorBuffer), kCFStringEncodingUTF8)) {
            vendorName = vendorBuffer;
        }
    }
    spdlog::info("createAudioDevice: Got vendor name");
    
    CFRelease(properties); // *CRITICAL*: Release the properties dictionary.
    
    // Get the AVC Unit service (io_object_t).  This is what we'll use for
    // communication. We traverse from "device" (IOFireWireAVCUnit) to its Unit, using IORegistryEntryGetParentEntry
    io_service_t avcUnit = 0;
    
    spdlog::info("AVC Device before getting parent entry");
    // DEBUG:
    char className[256];
    IOObjectGetClass(device, className);
    spdlog::info("device class: {}", className);


    result = IORegistryEntryGetParentEntry(device, kIOServicePlane, &avcUnit);
    if (result != kIOReturnSuccess) {
        spdlog::error("createAudioDevice: Failed to get parent AVC unit entry: {}", result);
        return std::unexpected(static_cast<IOKitError>(result));
    }
    if (!avcUnit)
    {
        spdlog::error("createAudioDevice: Failed to get avcUnit");
        return std::unexpected(static_cast<IOKitError>(kIOReturnNotFound));
    }

    // DEBUG:
    spdlog::info("AVC Device after getting parent entry");
    IOObjectGetClass(device, className);
    spdlog::info("device class: {}", className);

    
    // ---- Add interest notification for THIS device ----
    
    io_object_t interestNotification = 0; // This will hold the notification object.
    
    // Set up interest-based notification for removal
    result = IOServiceAddInterestNotification(
                                              notifyPort_,       // The notification port  <----- Use notifyPort_ here.
                                              device,                 // The service object representing AV/C Unit
                                              kIOGeneralInterest,      // The type of interest
                                              deviceInterestCallback, // The callback function
                                              this,                   // refCon: Pass 'this' pointer.
                                              &interestNotification    // Store the notification object for later removal
                                              );
    
    if (result != kIOReturnSuccess) {
        spdlog::error("createAudioDevice: Failed to add interest notification: {}", result);
        IOObjectRelease(avcUnit); // Release avcUnit if failed.
        return std::unexpected(static_cast<IOKitError>(result));
    }
    
    // Now is safe to create device, with all io objects it needs.
    // Create the AudioDevice, passing in the required parameters.
    std::shared_ptr<AudioDevice> audioDevice = std::make_shared<AudioDevice>(guid, deviceName, vendorName,
                                                                             device, deviceController_.get());
    
    // Release the local reference to avcUnit, AudioDevice now owns it
    IOObjectRelease(device);
    
    // --- Initialize the AudioDevice ---
    auto initResult = audioDevice->init();
    if (!initResult) {
        spdlog::error("Failed to initialize AudioDevice: 0x{:x}", static_cast<int>(initResult.error()));
        // DO NOT release the device object here; the AudioDevice destructor handles it.
        return std::unexpected(initResult.error());
    }
    
    return audioDevice; // Return smart pointer
}

void IOKitFireWireDeviceDiscovery::setTestCallback(DeviceNotificationCallback callback) {
    spdlog::info("setTestCallback called");
    callback_ = callback;
}

std::shared_ptr<AudioDevice> IOKitFireWireDeviceDiscovery::findDeviceByGuid(UInt64 guid) {
    spdlog::info("findDeviceByGuid called with GUID: 0x{:x}", guid);
    auto it = std::find_if(devices_.begin(), devices_.end(),
                           [guid](const std::shared_ptr<AudioDevice>& device) {
        return device->getGuid() == guid;
    });
    
    if (it != devices_.end()) {
        spdlog::info("findDeviceByGuid: Device found");
        return *it;
    } else {
        spdlog::info("findDeviceByGuid: Device not found");
        return nullptr;
    }
}

std::expected<std::vector<std::shared_ptr<AudioDevice>>, IOKitError> IOKitFireWireDeviceDiscovery::getConnectedDevices()
{
    std::lock_guard<std::mutex> lock(devicesMutex_); // Should be locked!
    return devices_;
}

// Helper function to get GUID
std::expected<UInt64, IOKitError> IOKitFireWireDeviceDiscovery::getDeviceGuid(io_object_t device)
{
    CFMutableDictionaryRef props = nullptr;
    IOReturn result = IORegistryEntryCreateCFProperties(device, &props, kCFAllocatorDefault, kNilOptions);
    if (result != kIOReturnSuccess || props == nullptr) {
        if (props) CFRelease(props);
        return std::unexpected(static_cast<IOKitError>(result));
    }
    
    CFNumberRef guidNumber = (CFNumberRef)CFDictionaryGetValue(props, CFSTR("GUID"));
    UInt64 guid = 0;
    if (guidNumber == nullptr || !CFNumberGetValue(guidNumber, kCFNumberSInt64Type, &guid)) {
        CFRelease(props);
        return std::unexpected(static_cast<IOKitError>(kIOReturnNotFound));
    }
    CFRelease(props);
    return guid;
}

// --- The interest callback ---
void IOKitFireWireDeviceDiscovery::deviceInterestCallback(void* refCon, io_service_t service, natural_t messageType, void* messageArgument) {
    IOKitFireWireDeviceDiscovery* self = static_cast<IOKitFireWireDeviceDiscovery*>(refCon);
    if (!self) return;
    
    spdlog::info("deviceInterestCallback called: messageType = 0x{:x}", messageType);
    
    if (messageType == kIOMessageServiceIsTerminated) {
        spdlog::info("Device terminated!");
        // 1. Find device in the list.
        
        // Get properties to find the GUID
        CFMutableDictionaryRef properties = nullptr;
        IOReturn result = IORegistryEntryCreateCFProperties(service, &properties, kCFAllocatorDefault, kNilOptions);
        if (result != kIOReturnSuccess || properties == nullptr) {
            spdlog::error("deviceInterestCallback: Failed to get device properties: {}", result);
            if (properties) CFRelease(properties);
            return;
        }
        CFNumberRef guidNumber = (CFNumberRef)CFDictionaryGetValue(properties, CFSTR("GUID"));
        UInt64 guid = 0;
        if (guidNumber == nullptr || !CFNumberGetValue(guidNumber, kCFNumberSInt64Type, &guid)) {
            spdlog::error("deviceInterestCallback: Device missing GUID or invalid GUID");
            CFRelease(properties);
            return;
        }
        CFRelease(properties);
        // 2. find device
        // Check if device with that GUID already added
        std::shared_ptr<AudioDevice> foundDevice;
        {   // Scope for the lock_guard
            std::lock_guard<std::mutex> lock(self->devicesMutex_);
            foundDevice = self->findDeviceByGuid(guid);
        }
        
        if (!foundDevice) {
            spdlog::info("deviceInterestCallback: device is not found.");
            return;
        }
        
        // 3. Call the callback with 'false' for disconnected.
        if (self->callback_) {
            self->callback_(foundDevice, false);
        }
        
        // 4. Remove device from the list
        {   // Scope for the lock_guard
            std::lock_guard<std::mutex> lock(self->devicesMutex_);
            self->devices_.erase(std::remove_if(self->devices_.begin(), self->devices_.end(),
                                                [&](const std::shared_ptr<AudioDevice>& dev) {
                return dev->getGuid() == guid;
            }), self->devices_.end());
        }
        spdlog::info("deviceInterestCallback: Device 0x{:x} removed.", guid);
    }
}

void IOKitFireWireDeviceDiscovery::setDeviceController(std::shared_ptr<DeviceController> controller) {
    if (deviceController_ == controller) {
        spdlog::trace("IOKitFireWireDeviceDiscovery::setDeviceController - Controller already set to {}", fmt::ptr(controller.get()));
        return;
    }
    deviceController_ = controller;
    spdlog::info("IOKitFireWireDeviceDiscovery::setDeviceController - Controller set: {}", fmt::ptr(controller.get()));
    // Potentially update other internal state if needed when controller is set/changed
}

} // namespace FWA
