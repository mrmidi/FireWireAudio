cmake_minimum_required(VERSION 3.26..4.0.0) # better SWIFT support requires 3.26 or later

# Set policies before project() command
cmake_policy(SET CMP0114 NEW) # For Xcode new build system
cmake_policy(SET CMP0175 NEW) # For add_custom_command() validation

# Set macOS deployment target to 15
set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum macOS deployment version")

# Include Objective-C++ language support
project(FireWireAudioDaemon LANGUAGES CXX OBJC OBJCXX)

# --- Sanitizer Setup ---
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer (cannot use with ASan)" OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "AddressSanitizer (ASan) and ThreadSanitizer (TSan) cannot be enabled simultaneously.")
endif()

set(SANITIZER_FLAGS "") # Initialize empty
set(SANITIZER_LINK_FLAGS "")

# Helper macro to append flags cleanly
macro(append_flag variable flag)
    if(${variable})
        set(${variable} "${${variable}} ${flag}") # Add space if not empty
    else()
        set(${variable} "${flag}") # No leading space if empty
    endif()
endmacro()

if(ENABLE_ASAN)
    message(STATUS "AddressSanitizer (ASan) Enabled")
    append_flag(SANITIZER_FLAGS "-fsanitize=address")
    append_flag(SANITIZER_FLAGS "-fno-omit-frame-pointer")
    append_flag(SANITIZER_LINK_FLAGS "-fsanitize=address")
endif()

if(ENABLE_UBSAN)
    message(STATUS "UndefinedBehaviorSanitizer (UBSan) Enabled")
    append_flag(SANITIZER_FLAGS "-fsanitize=undefined")
    if(NOT ENABLE_ASAN)
        append_flag(SANITIZER_FLAGS "-fno-omit-frame-pointer")
    endif()
    append_flag(SANITIZER_LINK_FLAGS "-fsanitize=undefined")
endif()

if(ENABLE_TSAN)
    message(STATUS "ThreadSanitizer (TSan) Enabled")
    append_flag(SANITIZER_FLAGS "-fsanitize=thread")
    append_flag(SANITIZER_LINK_FLAGS "-fsanitize=thread")
endif()
# --- End Sanitizer Variable Setup ---

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
    GIT_TAG        v1.15.2
)

# Catch2
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.8.0 
)

# libASPL
FetchContent_Declare(
    libASPL
    GIT_REPOSITORY https://github.com/mrmidi/libASPL.git
    GIT_TAG main
    OVERRIDE_FIND_PACKAGE
)

# Set up override for libaspl
set(FETCHCONTENT_FULLY_DISCONNECTED OFF)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libaspl-override.cmake")

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.12.0
)

FetchContent_MakeAvailable(spdlog catch2 libASPL nlohmann_json)

# --- Objective-C/Objective-C++ Files for XPC Bridge ---
# Define these lists ONCE
set(FWA_XPC_OBJCXX_FILES
    src/FWA/XPC/XPCBridge.mm
    src/FWA/XPC/XPCReceiverClient.mm
)
set(FWA_XPC_OBJC_FILES
    src/FWA/XPC/MixedAudioBuffer.m
)
set_source_files_properties(${FWA_XPC_OBJCXX_FILES} PROPERTIES LANGUAGE OBJCXX)
set_source_files_properties(${FWA_XPC_OBJC_FILES} PROPERTIES LANGUAGE OBJC)

# --- Create Isoch Library ---
add_library(FWAIsoch STATIC
    src/Isoch/AudioDeviceStream.cpp
    src/Isoch/IsoStreamHandler.cpp
    src/Isoch/SharedManagers.cpp
    
    # Isoch core
    src/Isoch/core/AmdtpReceiver.cpp
    src/Isoch/core/AmdtpTransmitter.cpp
    src/Isoch/core/IsochBufferManager.cpp
    src/Isoch/core/IsochTransportManager.cpp
    src/Isoch/core/IsochPacketProcessor.cpp
    src/Isoch/core/IsochMonitoringManager.cpp
    src/Isoch/core/ReceiverFactory.cpp
    src/Isoch/core/AudioClockPLL.cpp
    # New refactored components
    src/Isoch/core/IsochDCLManager.cpp
    src/Isoch/core/IsochPortChannelManager.cpp
    # New transmitter components
    src/Isoch/core/IsochTransmitBufferManager.cpp
    src/Isoch/core/IsochTransmitDCLManager.cpp
    src/Isoch/core/IsochPacketProvider.cpp
    
    # Isoch utils
    src/Isoch/utils/AmdtpHelpers.cpp
    src/Isoch/utils/CIPHeaderHandler.cpp
    src/Isoch/utils/RunLoopHelper.cpp
    
    # Isoch header files (from include/Isoch)
    include/Isoch/AudioDeviceStream.hpp
    include/Isoch/CIPHeaderHandler.hpp
    include/Isoch/IsoStreamHandler.hpp
    include/Isoch/SharedManagers.hpp
    
    # Isoch core headers
    include/Isoch/core/AmdtpReceiver.hpp
    include/Isoch/core/AmdtpTransmitter.hpp
    include/Isoch/core/AmdtpTypes.hpp
    include/Isoch/core/ReceiverTypes.hpp
    include/Isoch/core/TransmitterTypes.hpp
    include/Isoch/core/SharedTypes.hpp
    include/Isoch/core/Types.hpp
    include/Isoch/core/IsochTypes.hpp
    include/Isoch/core/IsochBufferManager.hpp
    include/Isoch/core/IsochTransportManager.hpp
    include/Isoch/core/IsochPacketProcessor.hpp
    include/Isoch/core/IsochMonitoringManager.hpp
    include/Isoch/core/ReceiverFactory.hpp
    include/Isoch/core/AudioClockPLL.hpp
    # New transmitter component headers
    include/Isoch/core/IsochTransmitBufferManager.hpp
    
    # Isoch interfaces
    include/Isoch/interfaces/TransmitterInterfaces.hpp
    # New interface headers
    include/Isoch/interfaces/ITransmitBufferManager.hpp
    include/Isoch/interfaces/ITransmitDCLManager.hpp
    include/Isoch/interfaces/ITransmitPacketProvider.hpp
    
    # Isoch utils headers
    include/Isoch/utils/AmdtpHelpers.hpp
    include/Isoch/utils/CIPHeaderHandler.hpp
    include/Isoch/utils/RingBuffer.hpp
    include/Isoch/utils/RunLoopHelper.hpp
    
    # --- Also list XPC Bridge HEADERS needed by Isoch ---
    include/FWA/XPC/XPCBridge.h
    include/FWA/XPC/MixedAudioBuffer.h
    include/FWA/XPC/DuetXPCProtocol.h
    include/FWA/XPC/DuetXPCClientProtocol.h
    include/FWA/XPC/XPCReceiverClient.hpp
)

# Set include directories for FWAIsoch
target_include_directories(FWAIsoch
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Link dependencies for FWAIsoch
target_link_libraries(FWAIsoch
    PUBLIC
        spdlog::spdlog
        "-framework IOKit"
        "-framework CoreFoundation"
        "-framework Foundation"  # Required for Objective-C/Cocoa
)

# --- Apply Sanitizers to FWAIsoch ---
if(SANITIZER_FLAGS)
    string(REPLACE " " ";" SANITIZER_FLAGS_LIST ${SANITIZER_FLAGS})
    target_compile_options(FWAIsoch PRIVATE ${SANITIZER_FLAGS_LIST})
endif()
# --- End Apply Sanitizers to FWAIsoch ---

# --- Create a Library for the Application Code ---
add_library(FWA STATIC
    src/FWA/AVCInfoBlock.cpp  
    src/FWA/AudioDevice.cpp
    src/FWA/DeviceInfo.cpp
    src/FWA/AudioPlug.cpp
    src/FWA/AudioStreamFormat.cpp
    src/FWA/MusicSubunit.cpp
    src/FWA/AudioSubunit.cpp
    src/FWA/CommandInterface.cpp
    src/FWA/DeviceController.cpp
    src/FWA/DeviceParser.cpp
    src/FWA/UnitPlugDiscoverer.cpp
    src/FWA/SubunitDiscoverer.cpp
    src/FWA/PlugDetailParser.cpp
    src/FWA/DescriptorReader.cpp
    src/FWA/MusicSubunitDescriptorParser.cpp  # add parser implementation
    src/FWA/DescriptorSpecifier.cpp
    src/FWA/DescriptorUtils.cpp
    src/FWA/Helpers.cpp
    src/FWA/IOKitFireWireDeviceDiscovery.cpp
    src/FWA/JsonHelpers.cpp
    # Base headers
    include/FWA/AudioDevice.h
    include/FWA/AudioPlug.hpp
    include/FWA/AudioStreamFormat.hpp
    include/FWA/AVCInfoBlock.hpp
    include/FWA/CommandInterface.h
    include/FWA/DeviceController.h
    include/FWA/DeviceInfo.hpp
    include/FWA/DeviceParser.hpp
    include/FWA/UnitPlugDiscoverer.hpp
    include/FWA/SubunitDiscoverer.hpp
    include/FWA/PlugDetailParser.hpp
    include/FWA/DescriptorReader.hpp
    include/FWA/MusicSubunitDescriptorParser.hpp
    include/FWA/DescriptorSpecifier.hpp
    include/FWA/DescriptorUtils.hpp
    include/FWA/Enums.hpp
    include/FWA/Error.h
    include/FWA/Helpers.h
    include/FWA/IFireWireDeviceDiscovery.h
    include/FWA/IOKitFireWireDeviceDiscovery.h
    include/FWA/Subunit.hpp
    # XPC Related headers
    include/FWA/XPC/XPCBridge.h
    include/FWA/XPC/MixedAudioBuffer.h
    include/FWA/XPC/DuetXPCProtocol.h
    include/FWA/XPC/DuetXPCClientProtocol.h
    include/FWA/XPC/XPCReceiverClient.hpp
    # Include the Objective-C++ and Objective-C files
    ${FWA_XPC_OBJCXX_FILES}
    ${FWA_XPC_OBJC_FILES}
)

# Set include directories for the FWA library (PUBLIC)
target_include_directories(FWA
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Link spdlog, FWAIsoch and needed frameworks to the FWA library
target_link_libraries(FWA
    PUBLIC
        spdlog::spdlog
        FWAIsoch
        nlohmann_json::nlohmann_json
        "-framework Foundation"  # Required for Objective-C/Cocoa
)

# --- Apply Sanitizers to FWA ---
if(SANITIZER_FLAGS)
    string(REPLACE " " ";" SANITIZER_FLAGS_LIST_FWA ${SANITIZER_FLAGS})
    target_compile_options(FWA PRIVATE ${SANITIZER_FLAGS_LIST_FWA})
endif()
# --- End Apply Sanitizers to FWA ---

# --- nlohmann_json include directory propagation (for json_fwd.hpp in headers) ---
if(TARGET nlohmann_json::nlohmann_json)
    get_target_property(NLOHMANN_JSON_INCLUDE_DIR nlohmann_json::nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
    if(NLOHMANN_JSON_INCLUDE_DIR)
        target_include_directories(FWA PUBLIC ${NLOHMANN_JSON_INCLUDE_DIR})
        target_include_directories(FWAIsoch PUBLIC ${NLOHMANN_JSON_INCLUDE_DIR})
    endif()
endif()

# --- XPC Service Target ---
set(XPC_SERVICE_NAME "FWADaemon")
set(XPC_SERVICE_SOURCES
    src/xpc/DuetXPC/main.m
    src/xpc/DuetXPC/DuetXPC.m
    src/xpc/DuetXPC/MixedAudioBuffer.m
)

# Create XPC Service bundle target as an executable
add_executable(${XPC_SERVICE_NAME} MACOSX_BUNDLE ${XPC_SERVICE_SOURCES})

# Set XPC Service bundle properties
set_target_properties(${XPC_SERVICE_NAME} PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "xpc"
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/xpc/DuetXPC/Info.plist"
    XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/src/xpc/DuetXPC/DuetXPC.entitlements"
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
    PREFIX ""
    SUFFIX ""
    MACOSX_BUNDLE_BUNDLE_NAME "FWADaemon"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    MACOSX_BUNDLE_GUI_IDENTIFIER "net.mrmidi.FWADaemon"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "net.mrmidi.FWADaemon"
)

# Set include directories for XPC Service
target_include_directories(${XPC_SERVICE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/xpc/DuetXPC
)

# Link frameworks for XPC Service
target_link_libraries(${XPC_SERVICE_NAME} PRIVATE
    "-framework Foundation"
)

# Copy XPC Service assets with explicit POST_BUILD
add_custom_command(TARGET ${XPC_SERVICE_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/src/xpc/DuetXPC/Assets.xcassets
    $<TARGET_BUNDLE_CONTENT_DIR:${XPC_SERVICE_NAME}>/Resources/Assets.xcassets
)

# --- Main Executable ---
# Renamed to avoid case conflict with FWADaemon
add_executable(FireWireAudioDaemon src/main.cpp)
# --- Apply Sanitizer Linker Flags to Main Executable ---
if(SANITIZER_LINK_FLAGS)
    string(REPLACE " " ";" SANITIZER_LINK_FLAGS_LIST ${SANITIZER_LINK_FLAGS})
    target_link_options(FireWireAudioDaemon PRIVATE ${SANITIZER_LINK_FLAGS_LIST})
endif()
# --- End Apply Sanitizer Linker Flags ---

# Link macOS frameworks and the FWA library
target_link_libraries(FireWireAudioDaemon
    PRIVATE
        FWA
        "-framework IOKit"
        "-framework Foundation"
        "-framework CoreFoundation"
)

# Make the XPC service a dependency of the main daemon
add_dependencies(FireWireAudioDaemon ${XPC_SERVICE_NAME})

# --- Audio Driver Target ---
add_library(FWADriver MODULE src/driver/FWADriver.cpp)
set_target_properties(FWADriver PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "driver"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist
    PREFIX ""
    SUFFIX ""
)

# Add proper include directories for FWADriver
target_include_directories(FWADriver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${libaspl_SOURCE_DIR}/include
    ${libaspl_BINARY_DIR}/include  # Add this line to include generated headers
)

target_link_libraries(FWADriver
    PRIVATE
        FWA
        libASPL
        "-framework CoreFoundation"
        "-framework CoreAudio"
)

# --- Documentation ---
find_package(Doxygen)
if(DOXYGEN_FOUND)
    set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/docs)
    set(DOXYGEN_PROJECT_NAME "FireWire Audio Daemon")
    set(DOXYGEN_PROJECT_BRIEF "A FireWire Audio Driver and Daemon for macOS")
    set(DOXYGEN_EXTRACT_ALL YES)
    set(DOXYGEN_EXTRACT_PRIVATE YES)
    set(DOXYGEN_EXTRACT_STATIC YES)
    set(DOXYGEN_RECURSIVE YES)
    
    doxygen_add_docs(docs 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        COMMENT "Generating documentation with Doxygen"
    )
endif()

# --- Testing ---
enable_testing()
add_subdirectory(test)

# --- Source Groups (for IDE organization) ---

# Group source files
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "Source Files" FILES
    src/FWA/AudioDevice.cpp
    src/FWA/CommandInterface.cpp
    src/FWA/DeviceController.cpp
    src/FWA/DeviceParser.cpp
    src/FWA/UnitPlugDiscoverer.cpp
    src/FWA/SubunitDiscoverer.cpp
    src/FWA/PlugDetailParser.cpp
    src/FWA/DescriptorReader.cpp
    src/FWA/MusicSubunitDescriptorParser.cpp
    src/FWA/DescriptorSpecifier.cpp
    src/FWA/DescriptorUtils.cpp
    src/FWA/Helpers.cpp
    src/FWA/IOKitFireWireDeviceDiscovery.cpp
    src/FWA/JsonHelpers.cpp
    src/FWA/XPC/XPCBridge.mm
    src/FWA/XPC/XPCReceiverClient.mm
    src/FWA/XPC/MixedAudioBuffer.m
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
    include/FWA/XPC/XPCBridge.h
    include/FWA/XPC/XPCReceiverClient.hpp
    include/FWA/XPC/DuetXPCProtocol.h
    include/FWA/XPC/DuetXPCClientProtocol.h
    include/FWA/XPC/MixedAudioBuffer.h
)

# Group Isoch source files
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "Isoch Source Files" FILES
    src/Isoch/AudioDeviceStream.cpp
    src/Isoch/IsoStreamHandler.cpp
    src/Isoch/SharedManagers.cpp
    src/Isoch/core/AmdtpReceiver.cpp
    src/Isoch/core/AmdtpTransmitter.cpp
    src/Isoch/core/IsochBufferManager.cpp
    src/Isoch/core/IsochTransportManager.cpp
    src/Isoch/core/IsochPacketProcessor.cpp
    src/Isoch/core/IsochMonitoringManager.cpp
    src/Isoch/core/ReceiverFactory.cpp
    src/Isoch/core/IsochDCLManager.cpp
    src/Isoch/core/IsochPortChannelManager.cpp
    src/Isoch/core/AudioClockPLL.cpp
    src/Isoch/core/IsochTransmitBufferManager.cpp
    src/Isoch/core/IsochTransmitDCLManager.cpp
    src/Isoch/core/IsochPacketProvider.cpp
    src/Isoch/utils/AmdtpHelpers.cpp
    src/Isoch/utils/CIPHeaderHandler.cpp
    src/Isoch/utils/RunLoopHelper.cpp
)

# Group Isoch header files
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/include PREFIX "Isoch Header Files" FILES
    include/Isoch/AudioDeviceStream.hpp
    include/Isoch/CIPHeaderHandler.hpp
    include/Isoch/IsoStreamHandler.hpp
    include/Isoch/SharedManagers.hpp
    include/Isoch/core/AmdtpReceiver.hpp
    include/Isoch/core/AmdtpTransmitter.hpp
    include/Isoch/core/AmdtpTypes.hpp
    include/Isoch/core/ReceiverTypes.hpp
    include/Isoch/core/TransmitterTypes.hpp
    include/Isoch/core/SharedTypes.hpp
    include/Isoch/core/Types.hpp
    include/Isoch/core/IsochTypes.hpp
    include/Isoch/core/IsochBufferManager.hpp
    include/Isoch/core/IsochTransportManager.hpp
    include/Isoch/core/IsochPacketProcessor.hpp
    include/Isoch/core/IsochMonitoringManager.hpp
    include/Isoch/core/ReceiverFactory.hpp
    include/Isoch/core/IsochDCLManager.hpp
    include/Isoch/core/IsochPortChannelManager.hpp
    include/Isoch/core/AudioClockPLL.hpp
    include/Isoch/core/IsochTransmitBufferManager.hpp
    include/Isoch/interfaces/TransmitterInterfaces.hpp
    include/Isoch/interfaces/ITransmitBufferManager.hpp
    include/Isoch/interfaces/ITransmitDCLManager.hpp
    include/Isoch/interfaces/ITransmitPacketProvider.hpp
    include/Isoch/utils/AmdtpHelpers.hpp
    include/Isoch/utils/CIPHeaderHandler.hpp
    include/Isoch/utils/RingBuffer.hpp
    include/Isoch/utils/RunLoopHelper.hpp
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

# --- Source Groups for XPC Service ---
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src/xpc PREFIX "XPC Source Files" FILES
    src/xpc/DuetXPC/main.m
    src/xpc/DuetXPC/DuetXPC.m
    src/xpc/DuetXPC/MixedAudioBuffer.m
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/include/xpc PREFIX "XPC Header Files" FILES
    include/xpc/DuetXPC/DuetXPC.h
    include/xpc/DuetXPC/DuetXPCProtocol.h
    include/xpc/DuetXPC/DuetXPCClientProtocol.h
    include/xpc/DuetXPC/XPCReceiverClientProtocol.h
    include/xpc/DuetXPC/MixedAudioBuffer.h

)