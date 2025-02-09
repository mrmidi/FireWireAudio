#pragma once

#include "IFireWireDeviceDiscovery.h"
#include "FWA/Error.h"
#include <IOKit/IOKitLib.h>
#include <IOKit/firewire/IOFireWireLib.h>
#include <IOKit/avc/IOFireWireAVCLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <expected>
#include <vector>
#include "AudioDevice.h"
#include <thread>
#include <mutex> // Add mutex include

namespace FWA {

class IOKitFireWireDeviceDiscovery : public IFireWireDeviceDiscovery {
public:
    IOKitFireWireDeviceDiscovery();
    ~IOKitFireWireDeviceDiscovery() override;
    
    std::expected<void, IOKitError> startDiscovery(DeviceNotificationCallback callback) override;
    std::expected<void, IOKitError> stopDiscovery() override;
    std::expected<std::vector<std::shared_ptr<AudioDevice>>, IOKitError> getConnectedDevices() override;
    std::expected<std::shared_ptr<AudioDevice>, IOKitError> getDeviceByGuid(std::uint64_t guid) override;
    
    bool isMasterPortValid() const { return masterPort_ != MACH_PORT_NULL; }
    bool isNotificationPortValid() const { return notifyPort_ != nullptr; }
    
    void setTestCallback(DeviceNotificationCallback callback);
    
private:
    mach_port_t                 masterPort_;
    IONotificationPortRef       notifyPort_;
    CFRunLoopSourceRef          runLoopSource_;
    io_iterator_t               deviceIterator_;
    DeviceNotificationCallback  callback_;
    
    static void deviceAdded(void* refCon, io_iterator_t iterator);
    
    std::vector<std::shared_ptr<AudioDevice>> devices_;
    std::mutex devicesMutex_; // Add the mutex
    
    std::expected<std::shared_ptr<AudioDevice>, IOKitError> createAudioDevice(io_object_t device);
    std::shared_ptr<AudioDevice> findDeviceByGuid(UInt64 guid);
    
    std::thread discoveryThread_;
    std::atomic<bool> discoveryThreadRunning_{false};
    void discoveryThreadFunction();
    CFRunLoopRef discoveryRunLoop_ = nullptr;
    // Helper function to get GUID
    std::expected<UInt64, IOKitError> getDeviceGuid(io_object_t device);
    
    // Static callback for device removal (interest-based).
    static void deviceInterestCallback(void* refCon, io_service_t service, natural_t messageType, void* messageArgument);
    
    // Friend declaration to allow the callback to access private members.
    friend class IOKitFireWireDeviceDiscoveryTests;
};

} // namespace FWA
