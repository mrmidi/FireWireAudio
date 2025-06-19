#include "FWA/AudioDevice.h"
#include "FWA/CommandInterface.h"
#include "FWA/DeviceParser.hpp"
#include "FWA/DeviceController.h"
#include "FWA/Helpers.h" // For cfStringToString
#include "FWA/Enums.hpp"
#include <spdlog/spdlog.h>
#include <IOKit/IOMessage.h>
#include <CoreFoundation/CFNumber.h> // For CFNumber*
#include <memory> // Required for std::make_shared
#include <format>
#include "Isoch/IsoStreamHandler.hpp" // Ensure this is included

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
      deviceController_(deviceController),
      fwUnit_(0),
      fwDevice_(0),
      busController_(0),
      notificationPort_(nullptr),
      interestNotification_(0),
      deviceInterface(nullptr),
      avcInterface_(nullptr),
      isoStreamHandler_(nullptr) // Initialize to nullptr
{
    spdlog::info("AudioDevice::AudioDevice - Creating device with GUID: 0x{:x}", guid);

    if (avcUnit_) {
        IOObjectRetain(avcUnit_);
    }
    // Do NOT call shared_from_this() here!
}

AudioDevice::~AudioDevice()
{
    stopStreams(); // Ensure streams are stopped before destruction
    {
        std::lock_guard<std::mutex> lock(streamHandlerMutex_);
        isoStreamHandler_.reset();
    }

    if (interestNotification_) {
        IOObjectRelease(interestNotification_);
        interestNotification_ = 0;
    }
    if (notificationPort_) {
        IONotificationPortDestroy(notificationPort_);
        notificationPort_ = nullptr;
    }
    if (deviceInterface) {
        (*deviceInterface)->Close(deviceInterface);
        (*deviceInterface)->Release(deviceInterface);
        deviceInterface = nullptr;
    }
    if (avcInterface_) {
        avcInterface_ = nullptr;
    }
    if (busController_) {
        IOObjectRelease(busController_);
        busController_ = 0;
    }
    if (fwDevice_) {
        IOObjectRelease(fwDevice_);
        fwDevice_ = 0;
    }
    if (fwUnit_) {
        IOObjectRelease(fwUnit_);
        fwUnit_ = 0;
    }
    if (avcUnit_) {
        IOObjectRelease(avcUnit_);
        avcUnit_ = 0;
    }
    spdlog::debug("AudioDevice::~AudioDevice - Destroyed device GUID: 0x{:x}", guid_);
}

std::expected<void, IOKitError> AudioDevice::init()
{
    spdlog::debug("AudioDevice::init - Initializing device GUID: 0x{:x}", guid_);
    if (!avcUnit_) {
        spdlog::error("AudioDevice::init: Cannot initialize without a valid avcUnit service.");
        return std::unexpected(IOKitError(kIOReturnNotAttached));
    }
    IOReturn result = IORegistryEntryGetParentEntry(avcUnit_, kIOServicePlane, &fwUnit_);
    if (result != kIOReturnSuccess || !fwUnit_) {
        spdlog::error("AudioDevice::init: Failed to get fwUnit: 0x{:x}", result);
        return std::unexpected(static_cast<IOKitError>(result != kIOReturnSuccess ? result : kIOReturnNotFound));
    }
    spdlog::debug("AudioDevice::init: Got fwUnit_ service: {:#x}", fwUnit_);

    result = IORegistryEntryGetParentEntry(fwUnit_, kIOServicePlane, &fwDevice_);
    if (result != kIOReturnSuccess || !fwDevice_) {
        spdlog::error("AudioDevice::init: Failed to get fwDevice: 0x{:x}", result);
        IOObjectRelease(fwUnit_); fwUnit_ = 0;
        return std::unexpected(static_cast<IOKitError>(result != kIOReturnSuccess ? result : kIOReturnNotFound));
    }
    spdlog::debug("AudioDevice::init: Got fwDevice_ service: {:#x}", fwDevice_);

    result = IORegistryEntryGetParentEntry(fwDevice_, kIOServicePlane, &busController_);
    if (result != kIOReturnSuccess || !busController_) {
        spdlog::error("AudioDevice::init: Failed to get busController: 0x{:x}", result);
        IOObjectRelease(fwDevice_); fwDevice_ = 0;
        IOObjectRelease(fwUnit_); fwUnit_ = 0;
        return std::unexpected(static_cast<IOKitError>(result != kIOReturnSuccess ? result : kIOReturnNotFound));
    }
    spdlog::debug("AudioDevice::init: Got busController_ service: {:#x}", busController_);

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
    // --- MODIFICATION START ---
    if (result != kIOReturnSuccess) {
        spdlog::error("AudioDevice::init: Failed to create FW device interface: 0x{:x}", result);
        // Clean up resources acquired so far in init before returning error
        if (notificationPort_) {
            // Remove source before destroying port if it was added
            // CFRunLoopRemoveSource(CFRunLoopGetCurrent(), IONotificationPortGetRunLoopSource(notificationPort_), kCFRunLoopDefaultMode);
            IONotificationPortDestroy(notificationPort_);
            notificationPort_ = nullptr;
        }
         if (busController_) { IOObjectRelease(busController_); busController_ = 0; }
         if (fwDevice_) { IOObjectRelease(fwDevice_); fwDevice_ = 0; }
         if (fwUnit_) { IOObjectRelease(fwUnit_); fwUnit_ = 0; }
        // Return the error immediately
        return std::unexpected(static_cast<IOKitError>(result));
    }
    // If we reach here, deviceInterface should be non-null and potentially open.
    spdlog::debug("AudioDevice::init: FW device interface created successfully (result = 0x{:x}).", result);
    // --- MODIFICATION END ---

    // CFRunLoopAddSource(CFRunLoopGetCurrent(),
    //                    IONotificationPortGetRunLoopSource(notificationPort_),
    //                    kCFRunLoopDefaultMode);

  
    // Do not discover capabilities for now
    
    // 3. Now safely create the CommandInterface using shared_from_this().
    try {
        commandInterface_ = std::make_shared<CommandInterface>(shared_from_this());
        spdlog::debug("AudioDevice::init: CommandInterface created.");
        auto activationResult = commandInterface_->activate();
        if (!activationResult && activationResult.error() != IOKitError::StillOpen) {
            spdlog::error("AudioDevice::init: Failed to activate CommandInterface before parsing: 0x{:x}", static_cast<int>(activationResult.error()));
            commandInterface_.reset();
            return std::unexpected(activationResult.error());
        }
        spdlog::debug("AudioDevice::init: CommandInterface activated (or was already active).");
    } catch (const std::bad_weak_ptr& e) {
        spdlog::critical("AudioDevice::init: Failed to create CommandInterface. Was AudioDevice created using std::make_shared? Exception: {}", e.what());
        if (deviceInterface) { (*deviceInterface)->Release(deviceInterface); deviceInterface = nullptr; }
        return std::unexpected(IOKitError(kIOReturnInternalError));
    }

    // 4. Discover device capabilities using DeviceParser.
    spdlog::info("AudioDevice::init: Starting device capability discovery...");
    auto parser = std::make_shared<DeviceParser>(this);
    auto parseResult = parser->parse();
    if (!parseResult) {
        spdlog::error("AudioDevice::init: Device capability parsing failed: 0x{:x}", static_cast<int>(parseResult.error()));
    } else {
        spdlog::info("AudioDevice::init: Device capability parsing completed successfully.");
    }

    spdlog::info("AudioDevice::init completed successfully for GUID: 0x{:x}", guid_);
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
            
            /* NEW – let isochronous callbacks reach the run-loop as well */
            if (!result) {
                result = (*deviceInterface)->AddIsochCallbackDispatcherToRunLoop(
                                            deviceInterface,
                                            deviceController_->getRunLoopRef());
            }
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

// --- PRIVATE HELPERS ---
std::expected<void, IOKitError> AudioDevice::checkControlResponse(
    const std::expected<std::vector<uint8_t>, IOKitError>& result,
    const char* commandName)
{
    if (!result) {
        spdlog::error("{} command failed: 0x{:x}", commandName, static_cast<int>(result.error()));
        return std::unexpected(result.error());
    }
    const auto& response = result.value();
    if (response.empty()) {
        spdlog::error("{} command returned empty response.", commandName);
        return std::unexpected(IOKitError::BadArgument);
    }
    uint8_t avcStatus = response[0];
    spdlog::debug("{} response status: 0x{:02x}", commandName, avcStatus);
    if (avcStatus == kAVCAcceptedStatus) {
        return {};
    } else if (avcStatus == kAVCRejectedStatus) {
        spdlog::warn("{} command REJECTED.", commandName);
        return std::unexpected(IOKitError::NotPermitted);
    } else if (avcStatus == kAVCNotImplementedStatus) {
        spdlog::error("{} command NOT IMPLEMENTED.", commandName);
        return std::unexpected(IOKitError::Unsupported);
    } else if (avcStatus == kAVCInterimStatus) {
         spdlog::info("{} command returned INTERIM. Further NOTIFY expected.", commandName);
         return {};
    } else {
        spdlog::error("{} command failed with unexpected status 0x{:02x}", commandName, avcStatus);
        return std::unexpected(IOKitError::BadArgument);
    }
}

std::expected<void, IOKitError> AudioDevice::checkDestPlugConfigureControlSubcommandResponse(
   const std::vector<uint8_t>& response,
   const char* commandName)
{
    if (response.size() < 13) {
         spdlog::error("{} response too short ({}) for subcommand status.", commandName, response.size());
         return std::unexpected(IOKitError::BadArgument);
    }
    uint8_t subcmdResultStatus = response[6];
    spdlog::debug("{} subcommand result status: 0x{:02x}", commandName, subcmdResultStatus);
    if (subcmdResultStatus == kAVCDestPlugResultStatusOK) {
        return {};
    } else if (subcmdResultStatus == kAVCDestPlugResultMusicPlugNotExist) {
         spdlog::error("{} failed: Music Plug does not exist.", commandName);
         return std::unexpected(IOKitError::NotFound);
    } else if (subcmdResultStatus == kAVCDestPlugResultSubunitPlugNotExist) {
         spdlog::error("{} failed: Destination Subunit Plug does not exist.", commandName);
         return std::unexpected(IOKitError::NotFound);
    } else if (subcmdResultStatus == kAVCDestPlugResultMusicPlugConnected) {
         spdlog::error("{} failed: Music Plug already connected.", commandName);
         return std::unexpected(IOKitError::StillOpen);
    } else if (subcmdResultStatus == kAVCDestPlugResultNoConnection) {
         spdlog::error("{} failed: No connection (Unknown subfunction) reported in subcommand status.", commandName);
         return std::unexpected(IOKitError::BadArgument);
    } else if (subcmdResultStatus == kAVCDestPlugResultUnknownMusicPlugType) {
         spdlog::error("{} failed: Unknown music plug type reported.", commandName);
         return std::unexpected(IOKitError::BadArgument);
    }
    else {
         spdlog::error("{} failed with subcommand status 0x{:02x}", commandName, subcmdResultStatus);
         return std::unexpected(IOKitError::Error);
    }
}

std::vector<uint8_t> AudioDevice::buildDestPlugConfigureControlCmd(
    uint8_t subfunction,
    uint8_t musicPlugType,
    uint16_t musicPlugID,
    uint8_t destSubunitPlugID,
    uint8_t streamPosition0,
    uint8_t streamPosition1)
{
    std::vector<uint8_t> cmd;
    uint8_t subunitAddr = Helpers::getSubunitAddress(SubunitType::Music, info_.getMusicSubunit().getId());
    cmd.push_back(kAVCControlCommand);
    cmd.push_back(subunitAddr);
    cmd.push_back(kAVCDestinationPlugConfigureOpcode);
    cmd.push_back(1);
    cmd.push_back(0xFF);
    cmd.push_back(0xFF);
    cmd.push_back(subfunction);
    cmd.push_back(musicPlugType);
    cmd.push_back(static_cast<uint8_t>(musicPlugID >> 8));
    cmd.push_back(static_cast<uint8_t>(musicPlugID & 0xFF));
    cmd.push_back(destSubunitPlugID);
    cmd.push_back(streamPosition0);
    cmd.push_back(streamPosition1);
    return cmd;
}

std::vector<uint8_t> AudioDevice::buildSetStreamFormatControlCmd(
    PlugDirection direction,
    uint8_t plugNum,
    const std::vector<uint8_t>& formatBytes)
{
     std::vector<uint8_t> cmd;
     uint8_t subunitAddr = kAVCUnitAddress;
     uint8_t streamFormatOpcode = kAVCStreamFormatOpcodePrimary;
     cmd.push_back(kAVCControlCommand);
     cmd.push_back(subunitAddr);
     cmd.push_back(streamFormatOpcode);
     cmd.push_back(kAVCStreamFormatSetSubfunction);
     cmd.push_back((direction == PlugDirection::Input) ? 0x00 : 0x01);
     uint8_t plugType = (plugNum < 0x80) ? 0x00 : 0x01;
     if(plugType != 0x00) {
         spdlog::error("buildSetStreamFormatControlCmd: Can only set format for Iso plugs (num < 128).");
         return {};
     }
     cmd.push_back(plugType);
     cmd.push_back(plugType);
     cmd.push_back(plugNum);
     cmd.push_back(0xFF);
     cmd.insert(cmd.end(), formatBytes.begin(), formatBytes.end());
     return cmd;
}

std::expected<void, IOKitError> AudioDevice::connectMusicPlug(
    uint8_t musicPlugType,
    uint16_t musicPlugID,
    uint8_t destSubunitPlugID,
    uint8_t streamPosition0,
    uint8_t streamPosition1)
{
    if (!commandInterface_) return std::unexpected(IOKitError::NotReady);
    if (!info_.hasMusicSubunit()) return std::unexpected(IOKitError::NotFound);
    spdlog::info("AudioDevice::connectMusicPlug: Type=0x{:02x}, ID={}, DestPlug={}, StreamPos=[{}, {}]",
                 musicPlugType, musicPlugID, destSubunitPlugID, streamPosition0, streamPosition1);
    std::vector<uint8_t> cmd = buildDestPlugConfigureControlCmd(
        kAVCDestPlugSubfuncConnect,
        musicPlugType,
        musicPlugID,
        destSubunitPlugID,
        streamPosition0,
        streamPosition1);
    spdlog::trace(" -> Sending Connect Music Plug command (0x40/00): {}", Helpers::formatHexBytes(cmd));
    auto result = commandInterface_->sendCommand(cmd);
    auto check = checkControlResponse(result, "ConnectMusicPlug(0x40/00)");
    if (!check) return check;
    return checkDestPlugConfigureControlSubcommandResponse(result.value(), "ConnectMusicPlug(0x40/00)");
}

std::expected<void, IOKitError> AudioDevice::disconnectMusicPlug(
    uint8_t musicPlugType,
    uint16_t musicPlugID)
{
     if (!commandInterface_) return std::unexpected(IOKitError::NotReady);
     if (!info_.hasMusicSubunit()) return std::unexpected(IOKitError::NotFound);
     spdlog::info("AudioDevice::disconnectMusicPlug: Type=0x{:02x}, ID={}", musicPlugType, musicPlugID);
     std::vector<uint8_t> cmd = buildDestPlugConfigureControlCmd(
        kAVCDestPlugSubfuncDisconnect,
        musicPlugType,
        musicPlugID,
        0xFF,
        0xFF,
        0xFF);
    spdlog::trace(" -> Sending Disconnect Music Plug command (0x40/02): {}", Helpers::formatHexBytes(cmd));
    auto result = commandInterface_->sendCommand(cmd);
    auto check = checkControlResponse(result, "DisconnectMusicPlug(0x40/02)");
     if (!check) return check;
     return checkDestPlugConfigureControlSubcommandResponse(result.value(), "DisconnectMusicPlug(0x40/02)");
}

std::expected<void, IOKitError> AudioDevice::setUnitIsochPlugStreamFormat(
    PlugDirection direction,
    uint8_t plugNum,
    const AudioStreamFormat& format)
{
    if (!commandInterface_) return std::unexpected(IOKitError::NotReady);
    if (plugNum >= 0x80) {
        spdlog::error("setUnitIsochPlugStreamFormat: Invalid plug number {} for Iso plug.", plugNum);
        return std::unexpected(IOKitError::BadArgument);
    }
    spdlog::info("AudioDevice::setUnitIsochPlugStreamFormat: Dir={}, PlugNum={}, Format=[{}]",
                 (direction == PlugDirection::Input ? "Input" : "Output"), plugNum, format.toString());
    std::vector<uint8_t> formatBytes = format.serializeToBytes();
    if (formatBytes.empty()) {
        spdlog::error("setUnitIsochPlugStreamFormat: Failed to serialize the provided AudioStreamFormat.");
        return std::unexpected(IOKitError::BadArgument);
    }
     spdlog::trace(" -> Serialized format bytes: {}", Helpers::formatHexBytes(formatBytes));
    std::vector<uint8_t> cmd = buildSetStreamFormatControlCmd(direction, plugNum, formatBytes);
     if (cmd.empty()) {
          return std::unexpected(IOKitError::BadArgument);
     }
    spdlog::trace(" -> Sending Set Stream Format command (0xBF/C2): {}", Helpers::formatHexBytes(cmd));
    auto result = commandInterface_->sendCommand(cmd);
    return checkControlResponse(result, "SetUnitIsochPlugStreamFormat(0xBF/C2)");
}

std::expected<void, IOKitError> AudioDevice::changeMusicPlugConnection(
    uint8_t musicPlugType,
    uint16_t musicPlugID,
    uint8_t newDestSubunitPlugID,
    uint8_t newStreamPosition0,
    uint8_t newStreamPosition1)
{
    if (!commandInterface_) return std::unexpected(IOKitError::NotReady);
    if (!info_.hasMusicSubunit()) return std::unexpected(IOKitError::NotFound);

    spdlog::info("AudioDevice::changeMusicPlugConnection: Type=0x{:02x}, ID={}, NewDestPlug={}, NewStreamPos=[{}, {}]",
                 musicPlugType, musicPlugID, newDestSubunitPlugID, newStreamPosition0, newStreamPosition1);

    std::vector<uint8_t> cmd = buildDestPlugConfigureControlCmd(
        kAVCDestPlugSubfuncChangeConnection, // Subfunction 0x01
        musicPlugType,
        musicPlugID,
        newDestSubunitPlugID,     // New destination
        newStreamPosition0,    // New stream pos
        newStreamPosition1);   // New stream pos

    spdlog::trace(" -> Sending Change Music Plug Connection command (0x40/01): {}", Helpers::formatHexBytes(cmd));
    auto result = commandInterface_->sendCommand(cmd);

    auto check = checkControlResponse(result, "ChangeMusicPlugConnection(0x40/01)");
    if (!check) return check;

    return checkDestPlugConfigureControlSubcommandResponse(result.value(), "ChangeMusicPlugConnection(0x40/01)");
}

std::expected<void, IOKitError> AudioDevice::disconnectAllMusicPlugs(uint8_t fromDestSubunitPlugID) {
    if (!commandInterface_) return std::unexpected(IOKitError::NotReady);
    if (!info_.hasMusicSubunit()) return std::unexpected(IOKitError::NotFound);

    spdlog::info("AudioDevice::disconnectAllMusicPlugs: FromDestPlugID={}", fromDestSubunitPlugID);

    std::vector<uint8_t> cmd = buildDestPlugConfigureControlCmd(
        kAVCDestPlugSubfuncDisconnectAll, // Subfunction 0x03
        0xFF,                             // musicPlugType = FF
        0xFFFF,                           // musicPlugID = FFFF
        fromDestSubunitPlugID,            // Specify which destination plug to clear
        0xFF,                             // streamPosition[0] = FF
        0xFF);                            // streamPosition[1] = FF

    spdlog::trace(" -> Sending Disconnect All Music Plugs command (0x40/03): {}", Helpers::formatHexBytes(cmd));
    auto result = commandInterface_->sendCommand(cmd);

    auto check = checkControlResponse(result, "DisconnectAllMusicPlugs(0x40/03)");
    if (!check) return check;

    return checkDestPlugConfigureControlSubcommandResponse(result.value(), "DisconnectAllMusicPlugs(0x40/03)");
}

std::expected<void, IOKitError> AudioDevice::defaultConfigureMusicPlugs() {
     if (!commandInterface_) return std::unexpected(IOKitError::NotReady);
     if (!info_.hasMusicSubunit()) return std::unexpected(IOKitError::NotFound);

     spdlog::info("AudioDevice::defaultConfigureMusicPlugs: Resetting connections to default.");

     std::vector<uint8_t> cmd = buildDestPlugConfigureControlCmd(
         kAVCDestPlugSubfuncDefaultConfigure, // Subfunction 0x04
         0xFF,                                // musicPlugType = FF
         0xFFFF,                              // musicPlugID = FFFF
         0xFF,                                // destSubunitPlugID = FF
         0xFF,                                // streamPosition[0] = FF
         0xFF);                               // streamPosition[1] = FF

     spdlog::trace(" -> Sending Default Configure Music Plugs command (0x40/04): {}", Helpers::formatHexBytes(cmd));
     auto result = commandInterface_->sendCommand(cmd);

     auto check = checkControlResponse(result, "DefaultConfigureMusicPlugs(0x40/04)");
     if (!check) return check;

     return checkDestPlugConfigureControlSubcommandResponse(result.value(), "DefaultConfigureMusicPlugs(0x40/04)");
}

std::expected<void, IOKitError> AudioDevice::startStreams() {
    spdlog::info("AudioDevice::startStreams called for GUID 0x{:x}", guid_);
    std::lock_guard<std::mutex> lock(streamHandlerMutex_); // Protect handler creation/access

    // --- Pre-checks ---
    if (!commandInterface_) {
         spdlog::error("AudioDevice::startStreams: CommandInterface not initialized for GUID 0x{:x}", guid_);
         return std::unexpected(IOKitError::NotReady);
    }
     if (!deviceInterface) {
         spdlog::error("AudioDevice::startStreams: deviceInterface (IOFireWireLibDeviceRef) not initialized for GUID 0x{:x}", guid_);
         return std::unexpected(IOKitError::NotReady);
     }
      if (!deviceController_) {
         spdlog::error("AudioDevice::startStreams: deviceController_ is null for GUID 0x{:x}", guid_);
         return std::unexpected(IOKitError::NotReady);
     }

    // --- Create Handler if needed ---
    if (!isoStreamHandler_) {
        spdlog::info("AudioDevice::startStreams: Creating IsoStreamHandler for GUID 0x{:x}", guid_);
        try {
            auto logger_to_use = spdlog::get("fwa_global") ? spdlog::get("fwa_global") : spdlog::default_logger();
            isoStreamHandler_ = std::make_shared<IsoStreamHandler>(
                shared_from_this(),
                logger_to_use,
                commandInterface_,
                deviceInterface
            );
            spdlog::info("AudioDevice::startStreams: IsoStreamHandler created successfully.");
        } catch (const std::bad_weak_ptr& e) {
             spdlog::critical("AudioDevice::startStreams: Failed to create IsoStreamHandler (bad_weak_ptr). Was AudioDevice created via std::make_shared? Exception: {}", e.what());
             return std::unexpected(IOKitError(kIOReturnInternalError));
        } catch (const std::exception& e) {
            spdlog::critical("AudioDevice::startStreams: Failed to create IsoStreamHandler for GUID 0x{:x}: {}", guid_, e.what());
            isoStreamHandler_.reset();
            return std::unexpected(IOKitError::InternalError);
        } catch (...) {
             spdlog::critical("AudioDevice::startStreams: Unknown exception creating IsoStreamHandler for GUID 0x{:x}", guid_);
             isoStreamHandler_.reset();
             return std::unexpected(IOKitError::InternalError);
        }
    } else {
        spdlog::info("AudioDevice::startStreams: IsoStreamHandler already exists for GUID 0x{:x}", guid_);
    }

    // --- Start the streams via the handler ---
    spdlog::debug("AudioDevice::startStreams: Calling handler->start()...");
    auto startResult = isoStreamHandler_->start();
    if (!startResult) {
         spdlog::error("AudioDevice::startStreams: handler->start() failed for GUID 0x{:x}: 0x{:x}", guid_, static_cast<int>(startResult.error()));
    } else {
         spdlog::info("AudioDevice::startStreams: handler->start() succeeded for GUID 0x{:x}.", guid_);
    }
    return startResult;
}

std::expected<void, IOKitError> AudioDevice::stopStreams() {
    spdlog::info("AudioDevice::stopStreams called for GUID 0x{:x}", guid_);
    std::shared_ptr<IsoStreamHandler> handler;
    {
        std::lock_guard<std::mutex> lock(streamHandlerMutex_);
        handler = isoStreamHandler_;
    }

    if (handler) {
        spdlog::debug("AudioDevice::stopStreams: Calling handler->stop()...");
        handler->stop();
        spdlog::info("AudioDevice::stopStreams: handler->stop() called for GUID 0x{:x}", guid_);
        return {};
    } else {
        spdlog::warn("AudioDevice::stopStreams: No active IsoStreamHandler to stop for GUID 0x{:x}", guid_);
        return {};
    }
}

Isoch::ITransmitPacketProvider* AudioDevice::getTransmitPacketProvider() {
    std::lock_guard<std::mutex> lock(streamHandlerMutex_);
    
    if (!isoStreamHandler_) {
        spdlog::debug("AudioDevice::getTransmitPacketProvider: No IsoStreamHandler available for GUID 0x{:x}", guid_);
        return nullptr;
    }
    
    auto* provider = isoStreamHandler_->getTransmitPacketProvider();
    if (!provider) {
        spdlog::debug("AudioDevice::getTransmitPacketProvider: IsoStreamHandler returned null provider for GUID 0x{:x}", guid_);
        return nullptr;
    }
    
    spdlog::debug("AudioDevice::getTransmitPacketProvider: Successfully retrieved provider for GUID 0x{:x}", guid_);
    return provider;
}

} // namespace FWA
