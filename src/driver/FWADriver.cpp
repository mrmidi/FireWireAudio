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
    auto context = std::make_shared<aspl::Context>();

    auto device = std::make_shared<aspl::Device>(context);
    device->AddStreamWithControlsAsync(aspl::Direction::Output);

    auto plugin = std::make_shared<aspl::Plugin>(context);
    plugin->AddDevice(device);

    return std::make_shared<aspl::Driver>(context, plugin);
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
        return nullptr;
    }

    static std::shared_ptr<aspl::Driver> driver = CreateDriver();

    return driver->GetReference();
}
