cmake_minimum_required(VERSION 3.26..4.0.0)

# Set policies before project() command
cmake_policy(SET CMP0114 NEW)
cmake_policy(SET CMP0175 NEW)

set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum macOS deployment version")

# Define project languages - Include Swift *only* for Xcode generator
if(CMAKE_GENERATOR MATCHES "Xcode")
  set(_PROJECT_LANGS CXX OBJC OBJCXX Swift)
else()
  set(_PROJECT_LANGS CXX OBJC OBJCXX)
endif()

project(FireWireAudioDaemon LANGUAGES ${_PROJECT_LANGS})

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS   "${CMAKE_CXX_FLAGS}   -stdlib=libc++")
set(CMAKE_OBJCXX_FLAGS "${CMAKE_OBJCXX_FLAGS} -stdlib=libc++")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

# --- Dependencies using FetchContent ---
include(FetchContent)
FetchContent_Declare(spdlog GIT_REPOSITORY https://github.com/gabime/spdlog.git GIT_TAG v1.15.2)
FetchContent_Declare(nlohmann_json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.12.0)
set(JSON_BuildTests OFF CACHE BOOL "Disable nlohmann_json tests")
FetchContent_Declare(catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.8.0)
set(CATCH2_BUILD_TESTS OFF CACHE BOOL "Disable Catch2 tests")
FetchContent_Declare(libASPL GIT_REPOSITORY https://github.com/mrmidi/libASPL.git GIT_TAG main OVERRIDE_FIND_PACKAGE)
set(FETCHCONTENT_FULLY_DISCONNECTED OFF)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libaspl-override.cmake")
FetchContent_MakeAvailable(spdlog nlohmann_json catch2 libASPL)

# --- Objective-C/Objective-C++ Files for XPC Bridge ---
set(FWA_XPC_OBJC_FILES src/FWA/XPC/MixedAudioBuffer.m)
set(FWA_XPC_OBJCXX_FILES src/FWA/XPC/XPCBridge.mm src/FWA/XPC/XPCReceiverClient.mm)
add_library(FWA_XPC STATIC
    ${FWA_XPC_OBJC_FILES}
    ${FWA_XPC_OBJCXX_FILES}
    include/FWA/XPC/XPCBridge.h include/FWA/XPC/MixedAudioBuffer.h
    include/FWA/XPC/DuetXPCProtocol.h include/FWA/XPC/DuetXPCClientProtocol.h
    include/FWA/XPC/XPCReceiverClient.hpp
)
set_source_files_properties(${FWA_XPC_OBJC_FILES} PROPERTIES LANGUAGE OBJC)
set_source_files_properties(${FWA_XPC_OBJCXX_FILES} PROPERTIES LANGUAGE OBJCXX)
target_compile_options(FWA_XPC PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-std=c++23 -stdlib=libc++>)
target_include_directories(FWA_XPC PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(FWA_XPC PUBLIC spdlog::spdlog "-framework Foundation")

# --- Create Isoch Library ---
add_library(FWAIsoch STATIC
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
    src/Isoch/core/AudioClockPLL.cpp
    src/Isoch/core/IsochDCLManager.cpp
    src/Isoch/core/IsochPortChannelManager.cpp
    src/Isoch/core/IsochTransmitBufferManager.cpp
    src/Isoch/core/IsochTransmitDCLManager.cpp
    src/Isoch/core/IsochPacketProvider.cpp
    src/Isoch/utils/AmdtpHelpers.cpp
    src/Isoch/utils/CIPHeaderHandler.cpp
    src/Isoch/utils/RunLoopHelper.cpp
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
    include/FWA/XPC/XPCBridge.h include/FWA/XPC/MixedAudioBuffer.h
    include/FWA/XPC/DuetXPCProtocol.h include/FWA/XPC/DuetXPCClientProtocol.h
    include/FWA/XPC/XPCReceiverClient.hpp
)
target_include_directories(FWAIsoch PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(FWAIsoch PUBLIC spdlog::spdlog FWA_XPC nlohmann_json::nlohmann_json "-framework IOKit" "-framework CoreFoundation" "-framework Foundation")

# --- Create FWA Library (Core C++ Logic + C API) ---
add_library(FWA STATIC
    src/FWA/AudioDevice.cpp
    src/FWA/AudioStreamFormat.cpp
    src/FWA/CommandInterface.cpp
    src/FWA/DeviceController.cpp
    src/FWA/DeviceParser.cpp
    src/FWA/DescriptorSpecifier.cpp
    src/FWA/DescriptorUtils.cpp
    src/FWA/Helpers.cpp
    src/FWA/IOKitFireWireDeviceDiscovery.cpp
    src/FWA/AVCInfoBlock.cpp
    src/FWA/DeviceInfo.cpp
    src/FWA/AudioPlug.cpp
    src/FWA/MusicSubunit.cpp
    src/FWA/AudioSubunit.cpp
    src/FWA/UnitPlugDiscoverer.cpp
    src/FWA/SubunitDiscoverer.cpp
    src/FWA/PlugDetailParser.cpp
    src/FWA/DescriptorReader.cpp
    src/FWA/MusicSubunitDescriptorParser.cpp
    src/FWA/JsonHelpers.cpp
    src/fwa_capi.cpp
    include/FWA/AudioDevice.h
    include/FWA/AudioPlug.hpp
    include/FWA/IOKitFireWireDeviceDiscovery.h
    include/fwa_capi.h
    include/FWA/JsonHelpers.hpp
)
set_target_properties(FWA PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(FWA PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
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

# --- Swift Application Target ---
if(CMAKE_GENERATOR MATCHES "Xcode")
    set(SWIFT_APP_SOURCES
        FWA-Control/DeviceManager.swift
        FWA-Control/FWControlApp.swift
        FWA-Control/ContentView.swift
        FWA-Control/AudioSubunitDetailsView.swift
        FWA-Control/DeviceBasicInfoView.swift
        FWA-Control/DeviceRefreshButton.swift
        FWA-Control/DeviceStatusIndicatorView.swift
        # FWA-Control/dummy.swift
        FWA-Control/ExpandableSectionView.swift
        FWA-Control/FWAManagerBridge.swift
        FWA-Control/HexDumpView.swift
        FWA-Control/InfoBlockDetailView.swift
        FWA-Control/MusicSubunitDetailsView.swift
        FWA-Control/PlugDetailView.swift
        FWA-Control/PlugListView.swift
        FWA-Control/SubunitInfoSectionView.swift
        FWA-Control/Test.swift
        FWA-Control/UnitInfoSectionView.swift
        # Models subdirectory
        FWA-Control/Models/DomainModels.swift
        FWA-Control/Models/Enums.swift
        FWA-Control/Models/JsonDecodables.swift
    )
    add_executable(FWA-Control MACOSX_BUNDLE ${SWIFT_APP_SOURCES})
    set_target_properties(FWA-Control PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/FWA-Control/Info.plist"
        MACOSX_BUNDLE_GUI_IDENTIFIER "net.mrmidi.fwa-control"
        MACOSX_BUNDLE_BUNDLE_NAME "FWA Control"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "net.mrmidi.fwa-control"
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_SOURCE_DIR}/FWA-Control/FWA_Control.entitlements"
        XCODE_ATTRIBUTE_SWIFT_OBJC_INTEROP_MODE "objc"
        XCODE_ATTRIBUTE_SWIFT_INCLUDE_PATHS "${CMAKE_SOURCE_DIR}/include;${FWA_CAPI_MODULEMAP_PATH}"
    )
    target_link_libraries(FWA-Control PRIVATE
        FWA
        "-framework SwiftUI"
        "-framework Cocoa"
        "-framework Foundation"
        "-framework IOKit"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
    add_dependencies(FWA-Control FWA)
endif()

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

# --- Source Groups for Swift Application ---
source_group("Source Files\\C_API" FILES src/fwa_capi.cpp)
source_group("Header Files\\C_API" FILES include/fwa_capi.h)
source_group("Swift Application" FILES ${SWIFT_APP_SOURCES})
# Group Swift Application Models files explicitly
source_group("Swift Application\\Models" FILES
    FWA-Control/Models/DomainModels.swift
    FWA-Control/Models/Enums.swift
    FWA-Control/Models/JsonDecodables.swift
)