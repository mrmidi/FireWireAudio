cmake_minimum_required(VERSION 3.21)
project(FireWireAudioDaemon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands (for sonarlint)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies using FetchContent ---
include(FetchContent)

# spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.13.0
)

# Catch2
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2 
)

# libASPL
FetchContent_Declare(
    libASPL
    GIT_REPOSITORY https://github.com/gavv/libASPL.git
    GIT_TAG main
)

FetchContent_MakeAvailable(spdlog catch2 libASPL)

# --- Create a Library for the Application Code ---
add_library(FWA STATIC
    src/AudioDevice.cpp
    src/CommandInterface.cpp
    src/DeviceController.cpp
    src/Helpers.cpp
    src/IOKitFireWireDeviceDiscovery.cpp
    src/main.cpp
    src/DeviceParser.cpp
    # Isoch implementation files
    src/Isoch/IsochBufferManager.cpp
    src/Isoch/CIPHeaderHandler.cpp
    src/Isoch/AmdtpHelpers.cpp
    src/Isoch/AmdtpTransmitter.cpp
    include/FWA/AudioDevice.h
    include/FWA/CommandInterface.h
    include/FWA/DeviceController.h
    include/FWA/Error.h
    include/FWA/Helpers.h
    include/FWA/IFireWireDeviceDiscovery.h
    include/FWA/IOKitFireWireDeviceDiscovery.h
    include/FWA/AudioPlug.hpp
    include/FWA/AudioStreamFormat.hpp
    include/FWA/AVCInfoBlock.hpp
    include/FWA/Enums.hpp
    include/FWA/Subunit.hpp
    include/FWA/DeviceParser.hpp
    include/FWA/DeviceInfo.hpp
    # Isoch header files
    include/FWA/Isoch/IsochBufferManager.hpp
    include/FWA/Isoch/CIPHeaderHandler.hpp
    include/FWA/Isoch/AmdtpHelpers.hpp
    include/FWA/Isoch/AmdtpTransmitter.hpp
)

# Set include directories for the FWA library (PUBLIC)
target_include_directories(FWA
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Link spdlog to the FWA library
target_link_libraries(FWA
    PUBLIC
        spdlog::spdlog
)

# --- Main Executable ---
add_executable(fwadaemon src/main.cpp)

# Link macOS frameworks and the FWA library
target_link_libraries(fwadaemon
    PRIVATE
        FWA
        "-framework IOKit"
        "-framework Foundation"
        "-framework CoreFoundation"
)

# --- Audio Driver Target ---
add_library(FWADriver MODULE src/driver/FWADriver.cpp)
set_target_properties(FWADriver PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "driver"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist
    PREFIX ""
    SUFFIX ""
)

target_link_libraries(FWADriver
    PRIVATE
        FWA
        libASPL
        "-framework CoreFoundation"
        "-framework CoreAudio"
)

# --- Testing ---
enable_testing()
add_subdirectory(test)


# --- Source Groups (for IDE organization) ---

# Group source files
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "Source Files" FILES
    src/AudioDevice.cpp
    src/CommandInterface.cpp
    src/DeviceController.cpp
    src/Helpers.cpp
    src/IOKitFireWireDeviceDiscovery.cpp
    src/main.cpp
    src/DeviceParser.cpp
    src/Isoch/IsochBufferManager.cpp
    src/Isoch/CIPHeaderHandler.cpp
    src/Isoch/AmdtpHelpers.cpp
    src/Isoch/AmdtpTransmitter.cpp
)

# Group header files
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/include PREFIX "Header Files" FILES
    include/FWA/AudioDevice.h
    include/FWA/CommandInterface.h
    include/FWA/DeviceController.h
    include/FWA/Error.h
    include/FWA/Helpers.h
    include/FWA/IFireWireDeviceDiscovery.h
    include/FWA/IOKitFireWireDeviceDiscovery.h
    include/FWA/DeviceParser.hpp
    include/FWA/Isoch/IsochBufferManager.hpp
    include/FWA/Isoch/CIPHeaderHandler.hpp
    include/FWA/Isoch/AmdtpHelpers.hpp
    include/FWA/Isoch/AmdtpTransmitter.hpp
)

# Group Capabilities files
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/include PREFIX "Capabilities" FILES
    include/FWA/AudioPlug.hpp
    include/FWA/AudioStreamFormat.hpp
    include/FWA/AVCInfoBlock.hpp
    include/FWA/Enums.hpp
    include/FWA/Subunit.hpp
    include/FWA/DeviceInfo.hpp
)