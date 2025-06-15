cmake_minimum_required(VERSION 3.26)

# Ensure we link libc++ for all C++ targets (especially universal)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -stdlib=libc++")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -stdlib=libc++")

# Enable ARC for Objective-C++
set(CMAKE_OBJCXX_FLAGS "${CMAKE_OBJCXX_FLAGS} -fobjc-arc")

# --- Policies ---
cmake_policy(SET CMP0114 NEW)
cmake_policy(SET CMP0175 NEW)

# --- Deployment target and architectures ---
set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum macOS deployment version")

option(BUILD_X86_64_TARGETS "Also build separate x86_64 targets" OFF)

# Optionally force all targets to x86_64 only
if(BUILD_X86_64_TARGETS)
  message(STATUS "⚙️  Forcing build architecture to x86_64")
  set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "" FORCE)
else()
  message(STATUS "⚙️  Building native architecture only (arm64)")
  set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
endif()

# --- Project declaration ---
project(FireWireAudioDaemon LANGUAGES C CXX Swift OBJC OBJCXX)

# --- Compiler settings ---
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Apply -stdlib=libc++ only for CXX and ObjC++
add_compile_options(
  $<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>
  $<$<COMPILE_LANGUAGE:OBJCXX>:-stdlib=libc++>
)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Optimization setup ---
option(ENABLE_MAXIMUM_OPTIMIZATION "Enable maximum optimization for daemon targets" OFF)

# --- Sanitizer setup ---
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer (cannot use with ASan)" OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
  message(FATAL_ERROR "ASan and TSan cannot be enabled simultaneously.")
endif()

if(ENABLE_MAXIMUM_OPTIMIZATION AND (ENABLE_ASAN OR ENABLE_UBSAN OR ENABLE_TSAN))
  message(WARNING "Maximum optimization is enabled with sanitizers. This may interfere with debugging and sanitizer effectiveness.")
endif()

# --- Setup optimization flags ---
set(OPTIMIZATION_COMPILE_FLAGS_LIST "")
set(OPTIMIZATION_LINK_FLAGS_LIST "")

if(ENABLE_MAXIMUM_OPTIMIZATION)
  message(STATUS "🚀 Maximum optimization enabled for daemon targets")
  
  # Aggressive optimization flags for maximum performance
  list(APPEND OPTIMIZATION_COMPILE_FLAGS_LIST 
    "-O3"                      # Maximum optimization level
    "-DNDEBUG"                 # Disable debug assertions
    "-flto"                    # Link-time optimization
    "-march=native"            # Optimize for current CPU architecture
    "-mtune=native"            # Tune for current CPU
    "-ffast-math"              # Enable fast math optimizations
    "-funroll-loops"           # Unroll loops for performance
    "-finline-functions"       # Inline functions aggressively
    "-fomit-frame-pointer"     # Omit frame pointer for better performance
    # "-fno-stack-protector"   # Disable stack protection - may conflict with macOS security
    # "-fno-exceptions"        # Disable exception handling - commented out for ObjC++ compatibility
    # "-fno-rtti"              # Disable RTTI - commented out for ObjC++ compatibility
    # "-fstrict-aliasing"        # Enable strict aliasing optimizations
    # "-fvectorize"            # Enable auto-vectorization - redundant with -O3
    # "-fslp-vectorize"        # Enable SLP vectorization - redundant with -O3
  )
  
  # Link-time optimization flags
  list(APPEND OPTIMIZATION_LINK_FLAGS_LIST
    "-flto"                    # Link-time optimization
    "-Wl,-dead_strip"          # Remove dead code (macOS specific)
    # "-Wl,-no_eh_frame_hdr"   # Not available on macOS - Linux only
  )
endif()

set(SANITIZER_FLAGS "")
set(SANITIZER_LINK_FLAGS "")

if(ENABLE_ASAN)
  message(STATUS "ASan enabled")
  list(APPEND SANITIZER_COMPILE_FLAGS_LIST "-fsanitize=address")
  list(APPEND SANITIZER_COMPILE_FLAGS_LIST "-fno-omit-frame-pointer") # This is a separate flag
  list(APPEND SANITIZER_LINK_FLAGS_LIST "-fsanitize=address")
endif()

if(ENABLE_UBSAN)
  message(STATUS "UBSan enabled")
  list(APPEND SANITIZER_COMPILE_FLAGS_LIST "-fsanitize=undefined")
  # Add -fno-omit-frame-pointer if not already added by ASan (it's good for UBSan stack traces too)
  if(NOT ("-fno-omit-frame-pointer" IN_LIST SANITIZER_COMPILE_FLAGS_LIST))
    list(APPEND SANITIZER_COMPILE_FLAGS_LIST "-fno-omit-frame-pointer")
  endif()
  list(APPEND SANITIZER_LINK_FLAGS_LIST "-fsanitize=undefined")
endif()

if(ENABLE_TSAN)
  message(STATUS "TSan enabled")
  list(APPEND SANITIZER_COMPILE_FLAGS_LIST "-fsanitize=thread")
  # TSan also requires/benefits from -fno-omit-frame-pointer
  if(NOT ("-fno-omit-frame-pointer" IN_LIST SANITIZER_COMPILE_FLAGS_LIST))
    list(APPEND SANITIZER_COMPILE_FLAGS_LIST "-fno-omit-frame-pointer")
  endif()
  list(APPEND SANITIZER_LINK_FLAGS_LIST "-fsanitize=thread")
endif()

# --- Dependencies via FetchContent ---
include(FetchContent)

# Core C++ deps
FetchContent_Declare(spdlog        GIT_REPOSITORY https://github.com/gabime/spdlog.git        GIT_TAG v1.15.2)
FetchContent_Declare(nlohmann_json GIT_REPOSITORY https://github.com/nlohmann/json.git        GIT_TAG v3.12.0)
set(JSON_BuildTests OFF CACHE BOOL "Disable nlohmann_json tests")
FetchContent_Declare(catch2        GIT_REPOSITORY https://github.com/catchorg/Catch2.git      GIT_TAG v3.8.0)
set(CATCH2_BUILD_TESTS OFF CACHE BOOL "Disable Catch2 tests")
FetchContent_Declare(googletest    GIT_REPOSITORY https://github.com/google/googletest.git    GIT_TAG v1.14.0)
set(BUILD_GMOCK ON CACHE BOOL "Enable Google Mock")
set(gtest_force_shared_crt ON CACHE BOOL "Force shared CRT for MSVC")
FetchContent_Declare(libASPL       GIT_REPOSITORY https://github.com/mrmidi/libASPL.git         GIT_TAG main)

# Swift Atomics - MUST be declared before SimplyCoreAudio
FetchContent_Declare(swift_atomics
    GIT_REPOSITORY https://github.com/apple/swift-atomics.git
    GIT_TAG        1.3.0
)

# SimplyCoreAudio - now depends on swift_atomics being available
FetchContent_Declare(SimplyCoreAudio 
    GIT_REPOSITORY https://github.com/mrmidi/SimplyCoreAudio.git  # Use your fork
    GIT_TAG develop
)

# Swift Collections (Fetch sources only)
FetchContent_Declare(swift_collections
    GIT_REPOSITORY https://github.com/apple/swift-collections.git
    GIT_TAG        1.2.0
)

# Swift-Log (Fetch sources only)
FetchContent_Declare(swift_log
    GIT_REPOSITORY https://github.com/apple/swift-log.git
    GIT_TAG        1.5.4
)

# Pull them all in (Downloads sources) - ORDER MATTERS HERE
set(FETCHCONTENT_FULLY_DISCONNECTED OFF)

# First fetch swift-atomics (SimplyCoreAudio dependency)
FetchContent_MakeAvailable(swift_atomics)

# Then everything else (but NOT SimplyCoreAudio - we'll handle it manually)
FetchContent_MakeAvailable(
  swift_atomics
  spdlog
  nlohmann_json
  catch2
  googletest
  libASPL
  swift_log
  swift_collections
)

# Manually populate SimplyCoreAudio without building it
FetchContent_GetProperties(SimplyCoreAudio)
if(NOT simplycoreaudio_POPULATED)
  FetchContent_Populate(SimplyCoreAudio)
endif()

# --- Manual build of Swift Dependencies ---

# Manual build of swift-log sources
FetchContent_GetProperties(swift_log)
if(NOT swift_log_POPULATED)
  message(FATAL_ERROR "swift-log failed to populate.")
endif()
file(GLOB SWIFT_LOG_SOURCES "${swift_log_SOURCE_DIR}/Sources/Logging/*.swift")
add_library(Logging STATIC ${SWIFT_LOG_SOURCES})
target_compile_definitions(Logging PUBLIC SWIFT_PACKAGE)
set_target_properties(Logging PROPERTIES FOLDER "Dependencies/Swift")
source_group("Dependencies/Swift/Logging" FILES ${SWIFT_LOG_SOURCES})
message(STATUS "Configured manual swift-log target 'Logging'")

# Manual build of swift-collections sources
FetchContent_GetProperties(swift_collections)
if(NOT swift_collections_POPULATED)
  message(FATAL_ERROR "swift-collections source directory was not populated.")
endif()
# Find all Swift source files within the downloaded dependency sources.
file(GLOB_RECURSE SWIFT_COLLECTIONS_SOURCES
  "${swift_collections_SOURCE_DIR}/Sources/*.swift"
  "${swift_collections_SOURCE_DIR}/Sources/**/*.swift"
)
# Create the manual library target. Use a unique name.
add_library(SwiftCollectionsManual STATIC
  ${SWIFT_COLLECTIONS_SOURCES}
)
# Apply necessary compile definitions FOR THE MANUAL TARGET:
target_compile_definitions(SwiftCollectionsManual PUBLIC
  SWIFT_PACKAGE
  COLLECTIONS_SINGLE_MODULE
)
# Optional: Organize the manual target in IDEs
set_target_properties(SwiftCollectionsManual PROPERTIES FOLDER "Dependencies/Swift")
source_group(TREE "${swift_collections_SOURCE_DIR}/Sources" PREFIX "Dependencies/Swift/swift-collections" FILES ${SWIFT_COLLECTIONS_SOURCES})
message(STATUS "Configured manual swift-collections target 'SwiftCollectionsManual'")

# Manual build of SimplyCoreAudio sources
# SimplyCoreAudio should already be populated from the FetchContent_Populate call above
if(NOT simplycoreaudio_POPULATED)
  message(FATAL_ERROR "SimplyCoreAudio source directory was not populated.")
endif()

# Find all Swift source files
file(GLOB_RECURSE SIMPLYCOREAUDIO_SOURCES
  "${simplycoreaudio_SOURCE_DIR}/Sources/SimplyCoreAudio/*.swift"
)

# Find C sources for SimplyCoreAudioC
file(GLOB_RECURSE SIMPLYCOREAUDIOC_SOURCES
  "${simplycoreaudio_SOURCE_DIR}/Sources/SimplyCoreAudioC/*.c"
  "${simplycoreaudio_SOURCE_DIR}/Sources/SimplyCoreAudioC/*.h"
)

# Create the C compatibility library first
if(SIMPLYCOREAUDIOC_SOURCES)
  add_library(SimplyCoreAudioC STATIC ${SIMPLYCOREAUDIOC_SOURCES})
  target_include_directories(SimplyCoreAudioC
    PUBLIC
      "${simplycoreaudio_SOURCE_DIR}/Sources/SimplyCoreAudioC"
  )
  set_target_properties(SimplyCoreAudioC PROPERTIES FOLDER "Dependencies/Swift")
endif()

# Create the main SimplyCoreAudio library
add_library(SimplyCoreAudio STATIC ${SIMPLYCOREAUDIO_SOURCES})

# Set Swift-specific properties
set_target_properties(SimplyCoreAudio PROPERTIES
  Swift_MODULE_NAME SimplyCoreAudio
  Swift_MODULE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/swift
  FOLDER "Dependencies/Swift"
)

# Add Swift import search paths for Xcode - use generator expressions for Debug/Release
target_compile_options(SimplyCoreAudio PRIVATE
  $<$<COMPILE_LANGUAGE:Swift>:-I${CMAKE_CURRENT_BINARY_DIR}/lib/$<CONFIG>>
  $<$<COMPILE_LANGUAGE:Swift>:-I${CMAKE_CURRENT_BINARY_DIR}/_deps/swift_atomics-src/Sources/_AtomicsShims/include>
)

# For Xcode generator, we need to set the SWIFT_INCLUDE_PATHS with config-specific paths
set_target_properties(SimplyCoreAudio PROPERTIES
  XCODE_ATTRIBUTE_SWIFT_INCLUDE_PATHS "${CMAKE_CURRENT_BINARY_DIR}/lib/Release;${CMAKE_CURRENT_BINARY_DIR}/lib/Debug;${CMAKE_CURRENT_BINARY_DIR}/_deps/swift_atomics-src/Sources/_AtomicsShims/include"
)

# Link dependencies
target_link_libraries(SimplyCoreAudio
  PRIVATE
    Atomics
    _AtomicsShims
)

if(TARGET SimplyCoreAudioC)
  target_link_libraries(SimplyCoreAudio
    PUBLIC
      SimplyCoreAudioC
  )
endif()

# Platform-specific linking (macOS frameworks)
if(APPLE)
  find_library(COREAUDIO_FRAMEWORK CoreAudio)
  find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox)
  find_library(FOUNDATION_FRAMEWORK Foundation)
  
  if(COREAUDIO_FRAMEWORK AND AUDIOTOOLBOX_FRAMEWORK AND FOUNDATION_FRAMEWORK)
    target_link_libraries(SimplyCoreAudio
      PUBLIC
        ${COREAUDIO_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
    )
  endif()
endif()

# Export include directories
target_include_directories(SimplyCoreAudio
  PUBLIC
    "${simplycoreaudio_SOURCE_DIR}/Sources"
)

source_group(TREE "${simplycoreaudio_SOURCE_DIR}/Sources" PREFIX "Dependencies/Swift/SimplyCoreAudio" FILES ${SIMPLYCOREAUDIO_SOURCES} ${SIMPLYCOREAUDIOC_SOURCES})
message(STATUS "Configured manual SimplyCoreAudio target 'SimplyCoreAudio'")


# --- Shared library for common code ---
add_library(Shared STATIC
  src/shared/SharedMemoryStructures.cpp
)
target_include_directories(Shared PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include/shared
  ${CMAKE_CURRENT_SOURCE_DIR}/include/shared/xpc
)
target_link_libraries(Shared PUBLIC spdlog "-framework Foundation")

# --- Subdirectories ---
add_subdirectory(src/Isoch)
add_subdirectory(src/FWA)
add_subdirectory(src/driver)
add_subdirectory(src/xpc/FWAXPC)


# --- XPC Service bundle ---
set(XPC_SERVICE_NAME "FWADaemon")
set(XPC_SERVICE_SOURCES
  src/xpc/FWADaemon/main.m
  src/xpc/FWADaemon/FWADaemon.mm
  src/xpc/FWADaemon/core/DaemonCore.cpp
)
add_executable(${XPC_SERVICE_NAME} MACOSX_BUNDLE ${XPC_SERVICE_SOURCES})
set_target_properties(${XPC_SERVICE_NAME} PROPERTIES
  BUNDLE                     TRUE
  SUFFIX                     ""
  MACOSX_BUNDLE_EXTENSION    "xpc"
  MACOSX_BUNDLE_INFO_PLIST   "${CMAKE_CURRENT_SOURCE_DIR}/src/xpc/FWADaemon/Info.plist"
  XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/src/xpc/FWADaemon/FWADaemon.entitlements"
  XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
  PREFIX ""
  MACOSX_BUNDLE_BUNDLE_NAME         "FWADaemon"
  MACOSX_BUNDLE_BUNDLE_VERSION      "1.0"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
  MACOSX_BUNDLE_GUI_IDENTIFIER      "net.mrmidi.FWADaemon"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "net.mrmidi.FWADaemon"
  XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
  XCODE_ATTRIBUTE_CLANG_ENABLE_OBJCXX_ARC YES
)
target_include_directories(${XPC_SERVICE_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include/shared/xpc
  ${CMAKE_CURRENT_SOURCE_DIR}/include/shared
  ${CMAKE_CURRENT_SOURCE_DIR}/src/xpc/FWADaemon
)

# add optimization flags if enabled
if(OPTIMIZATION_COMPILE_FLAGS_LIST)
  target_compile_options(${XPC_SERVICE_NAME} PRIVATE ${OPTIMIZATION_COMPILE_FLAGS_LIST})
endif()
if(OPTIMIZATION_LINK_FLAGS_LIST)
  target_link_options(${XPC_SERVICE_NAME} PRIVATE ${OPTIMIZATION_LINK_FLAGS_LIST})
endif()

# add ASAN/UBSAN flags if enabled
if(SANITIZER_COMPILE_FLAGS_LIST) # Check if the list is not empty
  target_compile_options(${XPC_SERVICE_NAME} PRIVATE ${SANITIZER_COMPILE_FLAGS_LIST})
endif()
if(SANITIZER_LINK_FLAGS_LIST) # Check if the list is not empty
  target_link_options(${XPC_SERVICE_NAME} PRIVATE ${SANITIZER_LINK_FLAGS_LIST}) # Use target_link_options for linker flags
endif()

target_link_libraries(${XPC_SERVICE_NAME} PRIVATE
  spdlog
  "-framework Foundation"
  Shared
  FWAXPC
  FWAIsoch
  FWA
  # Note: Linker flags from SANITIZER_LINK_FLAGS_LIST are applied via target_link_options,
  # not directly in target_link_libraries unless they are actual library names like -lsomething
)

# Post-build copy of assets
add_custom_command(TARGET ${XPC_SERVICE_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/src/xpc/FWADaemon/Assets.xcassets
    $<TARGET_BUNDLE_CONTENT_DIR:${XPC_SERVICE_NAME}>/Resources/Assets.xcassets
)


# --- Standalone CLI daemon ---
add_executable(FirewireAudioDaemon src/main.cpp)

# add optimization flags if enabled
if(OPTIMIZATION_COMPILE_FLAGS_LIST)
  target_compile_options(FirewireAudioDaemon PRIVATE ${OPTIMIZATION_COMPILE_FLAGS_LIST})
endif()
if(OPTIMIZATION_LINK_FLAGS_LIST)
  target_link_options(FirewireAudioDaemon PRIVATE ${OPTIMIZATION_LINK_FLAGS_LIST})
endif()

# add sanitizer flags if enabled
if(SANITIZER_COMPILE_FLAGS_LIST)
  target_compile_options(FirewireAudioDaemon PRIVATE ${SANITIZER_COMPILE_FLAGS_LIST})
endif()
if(SANITIZER_LINK_FLAGS_LIST)
  target_link_options(FirewireAudioDaemon PRIVATE ${SANITIZER_LINK_FLAGS_LIST})
endif()
target_link_libraries(FirewireAudioDaemon PRIVATE FWA Shared FWAXPC FWAIsoch)

# --- FWA-Control Swift GUI app ---
# Define Swift source files explicitly by group 
# (Instead of using GLOB which can be problematic with source_group)
set(SWIFT_MODEL_SOURCES
  FWA-Control/Models/DomainModels.swift
  FWA-Control/Models/Enums.swift
  FWA-Control/Models/JsonDecodables.swift
  FWA-Control/Models/ModelTypes.swift
  FWA-Control/Models/HALInfo.swift
  FWA-Control/Models/SystemStatus.swift
)

set(SWIFT_VIEW_SOURCES
  FWA-Control/ContentView.swift
  FWA-Control/OverviewView.swift
  FWA-Control/StatusBarView.swift
  FWA-Control/SettingsView.swift
  FWA-Control/AVCTabView.swift
  FWA-Control/ConnectionMatrixView.swift
  FWA-Control/LogConsoleView.swift
  FWA-Control/AudioSubunitDetailsView.swift
  FWA-Control/DeviceBasicInfoView.swift
  FWA-Control/DeviceStatusIndicatorView.swift
  FWA-Control/HexDumpView.swift
  FWA-Control/InfoBlockDetailView.swift
  FWA-Control/MusicSubunitDetailsView.swift
  FWA-Control/PlugDetailView.swift
  FWA-Control/PlugListView.swift
  FWA-Control/SubunitInfoSectionView.swift
  FWA-Control/UnitInfoSectionView.swift
  FWA-Control/CoreAudioHALView.swift
  # Views subdirectory
  FWA-Control/Views/DevicePickerView.swift
  FWA-Control/Views/DeviceOverviewCard.swift
  FWA-Control/Views/AudioConfigurationSection.swift
  FWA-Control/Views/PlugMatrixSection.swift
  FWA-Control/Views/SubunitDetailsSection.swift
  FWA-Control/Views/TechnicalSpecsSection.swift
  FWA-Control/Views/SharedOverviewComponents.swift
  FWA-Control/Views/EmptyDeviceStateView.swift
)

set(SWIFT_VIEWMODEL_SOURCES
  FWA-Control/UIManager.swift
)

set(SWIFT_SERVICE_SOURCES
  FWA-Control/EngineService.swift
  FWA-Control/SystemServicesManager.swift
  FWA-Control/LogStore.swift
  FWA-Control/DeviceDataMapper.swift
  FWA-Control/DriverInstallService.swift
  FWA-Control/XPCManager.swift
  FWA-Control/DaemonServiceManager.swift
  FWA-Control/PermissionManager.swift
  FWA-Control/CoreAudioHALManager.swift
  # FWA-Control/DriverInstaller.swift
)

set(SWIFT_APP_CONFIG_SOURCES
  FWA-Control/FWControlApp.swift
  FWA-Control/AppLoggers.swift
)

set(SWIFT_LOGGING_SOURCES
  FWA-Control/Logging/InMemoryLogHandler.swift
)

# Combine all sources
set(SWIFT_APP_SOURCES
  ${SWIFT_MODEL_SOURCES}
  ${SWIFT_VIEW_SOURCES}
  ${SWIFT_VIEWMODEL_SOURCES}
  ${SWIFT_SERVICE_SOURCES}
  ${SWIFT_APP_CONFIG_SOURCES}
  ${SWIFT_LOGGING_SOURCES}
)

add_executable(FWA-Control MACOSX_BUNDLE ${SWIFT_APP_SOURCES})
set_target_properties(FWA-Control PROPERTIES
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/FWA-Control/Info.plist"
  MACOSX_BUNDLE_GUI_IDENTIFIER "net.mrmidi.fwa-control"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "net.mrmidi.fwa-control"
  XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_SOURCE_DIR}/FWA-Control/FWA_Control.entitlements"
  XCODE_ATTRIBUTE_SWIFT_OBJC_INTEROP_MODE "objc"
  XCODE_ATTRIBUTE_SWIFT_INCLUDE_PATHS
    "${CMAKE_CURRENT_BINARY_DIR}/lib/Release;${CMAKE_CURRENT_BINARY_DIR}/lib/Debug;${CMAKE_CURRENT_BINARY_DIR}/_deps/swift_atomics-src/Sources/_AtomicsShims/include;${CMAKE_CURRENT_BINARY_DIR}/include;${CMAKE_SOURCE_DIR}/include;${CMAKE_SOURCE_DIR}/include/shared;${CMAKE_SOURCE_DIR}/include/shared/xpc"
  XCODE_ATTRIBUTE_SWIFT_OPTIMIZATION_LEVEL "-Onone"
  XCODE_ATTRIBUTE_SWIFT_VERSION "6.0"
)

set(FWADAEMON_LAUNCHD_PLIST "${CMAKE_SOURCE_DIR}/FWA-Control/FWADaemon.plist")
target_sources(FWA-Control PRIVATE "${FWADAEMON_LAUNCHD_PLIST}")
set_source_files_properties(
  "${FWADAEMON_LAUNCHD_PLIST}"
  PROPERTIES MACOSX_PACKAGE_LOCATION "Resources"
)
add_custom_command(TARGET FWA-Control POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E rm -rf
          $<TARGET_BUNDLE_CONTENT_DIR:FWA-Control>/Library/LaunchDaemons
  COMMAND ${CMAKE_COMMAND} -E make_directory
          $<TARGET_BUNDLE_CONTENT_DIR:FWA-Control>/Library/LaunchDaemons
  COMMAND ${CMAKE_COMMAND} -E copy
          $<TARGET_FILE:FWADaemon>
          $<TARGET_BUNDLE_CONTENT_DIR:FWA-Control>/Library/LaunchDaemons/FWADaemon
  COMMAND ${CMAKE_COMMAND} -E copy
          ${FWADAEMON_LAUNCHD_PLIST}
          $<TARGET_BUNDLE_CONTENT_DIR:FWA-Control>/Library/LaunchDaemons/FWADaemon.plist
  COMMAND ${CMAKE_COMMAND} -E copy
          ${FWADAEMON_LAUNCHD_PLIST}
          $<TARGET_BUNDLE_CONTENT_DIR:FWA-Control>/Resources/FWADaemon.plist
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          $<TARGET_BUNDLE_DIR:FWADriver>
          $<TARGET_BUNDLE_CONTENT_DIR:FWA-Control>/Resources/FWADriver.driver
  COMMENT "Embedding helper tools and resources into FWA-Control.app"
)
target_link_libraries(FWA-Control PRIVATE
  Logging
  SwiftCollectionsManual
  SimplyCoreAudio
  FWA
  Shared
  FWAXPC
  Atomics
  _AtomicsShims
  "-framework SwiftUI"
  "-framework Cocoa"
  "-framework Foundation"
  "-framework IOKit"
  "-framework CoreFoundation"
  "-framework CoreAudio"
  "-framework AVFoundation"
)
add_dependencies(FWA-Control Logging SwiftCollectionsManual SimplyCoreAudio FWADaemon FWADriver FWA Shared FWAXPC Atomics _AtomicsShims)
# --- Relax Swift 6 concurrency checks and add Swift module import paths ---
target_compile_options(FWA-Control PRIVATE
  $<$<COMPILE_LANGUAGE:Swift>:-Xfrontend -strict-concurrency=targeted>
  $<$<COMPILE_LANGUAGE:Swift>:-I${CMAKE_CURRENT_BINARY_DIR}/lib/$<CONFIG>>
  $<$<COMPILE_LANGUAGE:Swift>:-I${CMAKE_CURRENT_BINARY_DIR}/_deps/swift_atomics-src/Sources/_AtomicsShims/include>
)

# --- Documentation and testing (unchanged) ---
find_package(Doxygen)
if(DOXYGEN_FOUND)
  set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/docs)
  set(DOXYGEN_PROJECT_NAME "FireWire Audio Daemon")
  set(DOXYGEN_EXTRACT_ALL YES)
  set(DOXYGEN_RECURSIVE YES)
  doxygen_add_docs(docs ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include
    COMMENT "Generating documentation with Doxygen"
  )
endif()

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
src/main.cpp
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
include/Isoch/utils/TimingUtils.hpp
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
# src/xpc/FWADaemon/main.m
src/xpc/FWADaemon/FWADaemon.mm
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/include/xpc PREFIX "XPC Header Files" FILES
include/xpc/FWADaemon/FWADaemon.h
include/xpc/FWADaemon/FWADaemonProtocol.h
include/xpc/FWADaemon/FWADaemonClientProtocol.h
include/xpc/FWADaemon/FWADaemonControlProtocol.h
include/xpc/FWADaemon/FWAClientNotificationProtocol.h
include/xpc/FWADaemon/XPCReceiverClientProtocol.h
include/xpc/FWADaemon/MixedAudioBuffer.h
)

# --- Source Groups for Swift Application ---
source_group("Source Files\\C_API" FILES src/fwa_capi.cpp)
source_group("Header Files\\C_API" FILES include/fwa_capi.h)

# Source Groups for Swift app - use the explicit lists we defined earlier
source_group("Swift Application\\Models" FILES ${SWIFT_MODEL_SOURCES})
source_group("Swift Application\\Views" FILES ${SWIFT_VIEW_SOURCES})
source_group("Swift Application\\ViewModels" FILES ${SWIFT_VIEWMODEL_SOURCES})
source_group("Swift Application\\Services" FILES ${SWIFT_SERVICE_SOURCES})
source_group("Swift Application\\App" FILES ${SWIFT_APP_CONFIG_SOURCES})
source_group("Swift Application\\Logging" FILES ${SWIFT_LOGGING_SOURCES})

# Add resource files (non-Swift files) to appropriate groups
source_group("Swift Application\\Resources" FILES 
  FWA-Control/Info.plist
  FWA-Control/FWA_Control.entitlements
  FWA-Control/FWControl.entitlements
  FWA-Control/FWADaemon.plist
)