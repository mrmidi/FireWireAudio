// FWADriver.cpp

#include <memory>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreAudio/AudioServerPlugIn.h>
#include <aspl/Driver.hpp>
#include "FWADriverDevice.hpp"
#include "FWADriverHandler.hpp"
#include "FWADriverInit.hpp"
#include <aspl/Tracer.hpp>

constexpr UInt32 SampleRate = 44100;
constexpr UInt32 ChannelCount = 2;
constexpr const char* LogPrefix = "FWADriverASPL: ";

std::shared_ptr<aspl::Driver> CreateDriver()
{
    auto tracer = std::make_shared<aspl::Tracer>(
        aspl::Tracer::Mode::Syslog,
        aspl::Tracer::Style::Flat
    );
    auto context = std::make_shared<aspl::Context>(tracer);
    context->Tracer->Message("%sCreating driver...", LogPrefix);

    aspl::DeviceParameters deviceParams;
    deviceParams.Name = "FWA Firewire Audio";
    deviceParams.CanBeDefault = true;
    deviceParams.CanBeDefaultForSystemSounds = true;
    deviceParams.EnableRealtimeTracing = false;
    deviceParams.SampleRate = SampleRate;
    deviceParams.ChannelCount = ChannelCount;

    aspl::StreamParameters streamParams;
    streamParams.Direction = aspl::Direction::Output;
    streamParams.StartingChannel = 1;
 

    streamParams.Format = {
        .mSampleRate       = 44100.0,
        .mFormatID         = kAudioFormatLinearPCM,
        .mFormatFlags      = kAudioFormatFlagIsSignedInteger   |
                            kAudioFormatFlagIsAlignedHigh     |   // 24 valid bits in 32-bit word
                            kAudioFormatFlagsNativeEndian,
        .mBitsPerChannel   = 24,      // 24 valid bits
        .mChannelsPerFrame = 2,
        .mBytesPerFrame    = 8,       // 4 bytes × 2 channels
        .mFramesPerPacket  = 1,
        .mBytesPerPacket   = 8
    };

    auto device = std::make_shared<FWADriverDevice>(context, deviceParams);
    device->AddStreamWithControlsAsync(streamParams);
    auto handler = std::make_shared<FWADriverHandler>();
    device->SetControlHandler(handler);
    device->SetIOHandler(handler);


    auto plugin = std::make_shared<aspl::Plugin>(context);
    plugin->AddDevice(device);

    std::shared_ptr<aspl::Driver> driver = std::make_shared<aspl::Driver>(context, plugin);
    // Pass handler to FWADriverInit
    auto initHandler = std::make_shared<FWADriverInit>(handler);
    driver->SetDriverHandler(initHandler);
    context->Tracer->Message("%sDriver configuration complete.", LogPrefix);
    return driver;
}

/**
 * @brief Core Audio Server Plugin entry point
 *
 * This function is called by Core Audio to instantiate the driver.
 * It validates the plugin type and returns a reference to the driver instance.
 *
 * @param allocator Memory allocator (unused)
 * @param typeUUID UUID of the plugin type being requested
 * @return void* Reference to the driver instance or nullptr if type doesn't match
 */
extern "C" void* EntryPoint(CFAllocatorRef allocator, CFUUIDRef typeUUID)
{
    os_log(OS_LOG_DEFAULT, "FWADriverASPL: EntryPoint called!");
    if (!CFEqual(typeUUID, kAudioServerPlugInTypeUUID)) {
        os_log(OS_LOG_DEFAULT, "%sEntryPoint: Incorrect typeUUID requested.", LogPrefix);
        return nullptr;
    }

    static std::shared_ptr<aspl::Driver> driver = CreateDriver();

    if (!driver) {
        os_log(OS_LOG_DEFAULT, "%sEntryPoint: CreateDriver failed to return a driver instance.", LogPrefix);
        return nullptr;
    }

    os_log(OS_LOG_DEFAULT, "%sEntryPoint: Driver created, returning reference.", LogPrefix);
    return driver->GetReference();
}
