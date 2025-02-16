#include <memory>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreAudio/AudioServerPlugIn.h>
#include <aspl/Driver.hpp>

std::shared_ptr<aspl::Driver> CreateDriver()
{
    auto context = std::make_shared<aspl::Context>();

    auto device = std::make_shared<aspl::Device>(context);
    device->AddStreamWithControlsAsync(aspl::Direction::Output);

    auto plugin = std::make_shared<aspl::Plugin>(context);
    plugin->AddDevice(device);

    return std::make_shared<aspl::Driver>(context, plugin);
}

extern "C" void* EntryPoint(CFAllocatorRef allocator, CFUUIDRef typeUUID)
{
    if (!CFEqual(typeUUID, kAudioServerPlugInTypeUUID)) {
        return nullptr;
    }

    static std::shared_ptr<aspl::Driver> driver = CreateDriver();

    return driver->GetReference();
}
