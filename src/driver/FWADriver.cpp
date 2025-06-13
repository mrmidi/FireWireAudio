// FWADriver.cpp - Final Refactored Version

#include <memory>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreAudio/AudioServerPlugIn.h>
#include <aspl/Driver.hpp>
#include "FWADriverDevice.hpp"
#include "FWADriverHandler.hpp"
#include "FWADriverInit.hpp"
#include "FWAStream.hpp" // Your custom stream class
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
 
    // THIS IS THE FINAL, CORRECTED PHYSICAL FORMAT:
    // We request the final format Core Audio should convert TO.
    // By declaring this as the physical format and having FWAStream override
    // GetVirtualFormat() to return Float32, we trigger the Core Audio
    // conversion pipeline correctly.
    // streamParams.Format = {
    //     .mSampleRate       = 44100.0,
    //     .mFormatID         = kAudioFormatLinearPCM,
    //     .mFormatFlags      = kAudioFormatFlagIsBigEndian |
    //                        kAudioFormatFlagIsSignedInteger,
    //     .mBitsPerChannel   = 32, // The number of valid bits of audio data.
    //     .mChannelsPerFrame = 2,
    //     .mBytesPerFrame    = 8,  // The size of the container: 4 bytes × 2 channels.
    //     .mFramesPerPacket  = 1,
    //     .mBytesPerPacket   = 8
    // };

    // another try physical format 24-in-32, big-endian
    streamParams.Format = {
        .mSampleRate       = 44100.0,
        .mFormatID         = kAudioFormatLinearPCM,
        .mFormatFlags      = kAudioFormatFlagIsBigEndian   |
                            kAudioFormatFlagIsSignedInteger|
                            kAudioFormatFlagIsAlignedHigh, // <-- left-justified 24 bits
                            // kAudioFormatFlagIsPacked,
        .mBitsPerChannel   = 24,        // <-- FIX 1
        .mChannelsPerFrame = 2,
        .mBytesPerFrame    = 8,         // 4 B × 2 ch
        .mFramesPerPacket  = 1,
        .mBytesPerPacket   = 8
    };

    // LOG STREAM FORMAT
    os_log(OS_LOG_DEFAULT, "%sCreating FWAStream with format: SampleRate=%.2f, FormatID=%u, Flags=%u, BitsPerChannel=%u, ChannelsPerFrame=%u, BytesPerFrame=%u",
           LogPrefix,
           streamParams.Format.mSampleRate,
           streamParams.Format.mFormatID,
           streamParams.Format.mFormatFlags,
           streamParams.Format.mBitsPerChannel,
           streamParams.Format.mChannelsPerFrame,
           streamParams.Format.mBytesPerFrame);

    auto device = std::make_shared<FWADriverDevice>(context, deviceParams);

    if (!device) {
        os_log(OS_LOG_DEFAULT, "%sFailed to create FWADriverDevice instance.", LogPrefix);
    }

    // Manually create and add an instance of our custom FWAStream.
    auto stream = std::make_shared<FWAStream>(context, device, streamParams);

    if (!stream) {
        os_log(OS_LOG_DEFAULT, "%sFailed to create FWAStream instance.", LogPrefix);
        return nullptr;
    } else {
        os_log(OS_LOG_DEFAULT, "%sFWAStream instance created successfully.", LogPrefix);
    }

    // // LOG STREAM DETAILS
    // os_log(OS_LOG_DEFAULT, "%sFWAStream created with SampleRate=%.2f, Channels=%u, FormatID=%u",
    //        LogPrefix,
    //        stream->GetSampleRate(),
    //        stream->GetChannelCount(),
    //        stream->GetPhysicalFormat().mFormatID);
    
    // Add default mute/volume controls to ensure compatibility.
//    stream->AddDefaultControls();
    
    device->AddStreamAsync(stream);

    // make sure that streams are created before we set the handler
    auto streams = device->GetStreamCount(aspl::Direction::Output);
    if (streams == 0) {
        os_log(OS_LOG_DEFAULT, "%sNo streams created for device.", LogPrefix);
        return nullptr;
    } else {
        os_log(OS_LOG_DEFAULT, "%sDevice has %u streams created.", LogPrefix, streams);
    }

    
    auto handler = std::make_shared<FWADriverHandler>();
    device->SetControlHandler(handler);
    device->SetIOHandler(handler);

    auto plugin = std::make_shared<aspl::Plugin>(context);
    plugin->AddDevice(device);

    std::shared_ptr<aspl::Driver> driver = std::make_shared<aspl::Driver>(context, plugin);

    if (!driver) {
        os_log(OS_LOG_DEFAULT, "%sFailed to create FWADriver instance.", LogPrefix);
        return nullptr;
    }

    os_log(OS_LOG_DEFAULT, "%sDriver created successfully.", LogPrefix);
    
    auto initHandler = std::make_shared<FWADriverInit>(handler);
    if (!initHandler) {
        os_log(OS_LOG_DEFAULT, "%sFailed to create FWADriverInit instance.", LogPrefix);
        return nullptr;
    } else {
        os_log(OS_LOG_DEFAULT, "%sFWADriverInit instance created successfully.", LogPrefix);
    }
    driver->SetDriverHandler(initHandler);
    
    context->Tracer->Message("%sDriver configuration complete.", LogPrefix);
    os_log(OS_LOG_DEFAULT, "%sDriver created and configured successfully.", LogPrefix);
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
