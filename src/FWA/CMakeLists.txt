# FWA Core Library
add_library(FWA STATIC
    AudioDevice.cpp
    AudioStreamFormat.cpp
    CommandInterface.cpp
    DescriptorAccessor.cpp
    DeviceController.cpp
    DeviceParser.cpp
    DescriptorSpecifier.cpp
    DescriptorUtils.cpp
    Helpers.cpp
    IOKitFireWireDeviceDiscovery.cpp
    AVCInfoBlock.cpp
    DeviceInfo.cpp
    AudioPlug.cpp
    MusicSubunit.cpp
    MusicSubunitCapabilities.cpp
    AudioSubunit.cpp
    UnitPlugDiscoverer.cpp
    SubunitDiscoverer.cpp
    PlugDetailParser.cpp
    DescriptorReader.cpp
    MusicSubunitDescriptorParser.cpp
    JsonHelpers.cpp
    DiceAudioDevice.cpp
    DiceEAP.cpp
    DiceRouter.cpp
    ../fwa_capi.cpp
)
set_target_properties(FWA PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(FWA PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<TARGET_PROPERTY:nlohmann_json::nlohmann_json,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(FWA PUBLIC
    spdlog::spdlog
    FWAIsoch
    FWA_XPC
    nlohmann_json::nlohmann_json
    "-framework Foundation"
    "-framework IOKit"
    "-framework CoreFoundation"
)
