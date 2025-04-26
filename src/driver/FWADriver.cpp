/**
 * @file FWADriver.cpp
 * @brief Core Audio Server Plugin (ASPL) driver implementation
 *
 * This file implements a Core Audio Server Plugin driver using libASPL.
 * The driver provides a minimal no-op implementation that can be extended
 * with actual audio device functionality.
 */

#include <memory>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreAudio/AudioServerPlugIn.h>
#include <aspl/Driver.hpp>
#include "FWADriverDevice.hpp"
#include "FWADriverHandler.hpp"
#include "FWADriverInit.hpp"
#include <aspl/Tracer.hpp>
#include <os/log.h>

constexpr UInt32 SampleRate = 48000;
constexpr UInt32 ChannelCount = 2;

/**
 * @brief Creates and configures the ASPL driver instance
 * 
 * Sets up a basic audio driver with output stream and controls.
 * This is currently a no-op implementation that can be extended
 * with actual device functionality.
 *
 * @return std::shared_ptr<aspl::Driver> Configured driver instance
 */
std::shared_ptr<aspl::Driver> CreateDriver()
{
    auto tracer = std::make_shared<aspl::Tracer>(
        aspl::Tracer::Mode::Syslog,
        aspl::Tracer::Style::Flat
    );
    auto context = std::make_shared<aspl::Context>(tracer);
    context->Tracer->Message("FWADriver: Creating driver...");
    os_log(OS_LOG_DEFAULT, "FWADriver: Creating driver...");

    aspl::DeviceParameters deviceParams;
    deviceParams.Name = "FWA Firewire Audio";
    deviceParams.CanBeDefault = true;
    deviceParams.CanBeDefaultForSystemSounds = true;
    deviceParams.EnableRealtimeTracing = true;
    deviceParams.SampleRate = SampleRate;
    deviceParams.ChannelCount = ChannelCount;

    aspl::StreamParameters streamParams;
    streamParams.Direction = aspl::Direction::Output;
    streamParams.StartingChannel = 1;
    streamParams.Format = {
        .mSampleRate = 48000,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagIsSignedInteger,
        .mBitsPerChannel = 24,
        .mChannelsPerFrame = 2,
        .mBytesPerFrame = 8,
        .mFramesPerPacket = 1,
        .mBytesPerPacket = 8,
    };

    auto device = std::make_shared<FWADriverDevice>(context, deviceParams);
    device->AddStreamWithControlsAsync(streamParams);
    auto handler = std::make_shared<FWADriverHandler>();
    device->SetControlHandler(handler);
    device->SetIOHandler(handler);

    auto plugin = std::make_shared<aspl::Plugin>(context);
    plugin->AddDevice(device);

    std::shared_ptr<aspl::Driver> driver = std::make_shared<aspl::Driver>(context, plugin);
    auto initHandler = std::make_shared<FWADriverInit>();
    driver->SetDriverHandler(initHandler);
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
    if (!CFEqual(typeUUID, kAudioServerPlugInTypeUUID)) {
        os_log(OS_LOG_DEFAULT, "FWADriver: ERROR! CAN'T CREATE DRIVER");
        return nullptr;
    }
    static std::shared_ptr<aspl::Driver> driver = CreateDriver();
    os_log(OS_LOG_DEFAULT, "FWADriver: Driver successfully created");
    return driver->GetReference();
}
