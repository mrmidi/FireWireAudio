cmake_minimum_required(VERSION 3.26)

add_library(FWAXPC STATIC
    RingBufferManager.mm
    ShmIsochBridge.mm
)

target_include_directories(FWAXPC
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include" # Allows users to find include/xpc/FWAXPC/*.hpp
    PRIVATE
        "${PROJECT_SOURCE_DIR}/include/Isoch/interfaces" # Needed for ITransmitPacketProvider.hpp by ShmIsochBridge.mm
        # Add other private include dirs if needed
)

# Dependencies
target_link_libraries(FWAXPC
    PUBLIC
        Shared                     # Needs SharedMemoryStructures.hpp definition/impl
    PRIVATE
        spdlog::spdlog
        # DO NOT link FWAIsoch here, only needed include path above
        "-framework Foundation"    # Needed for Objective-C/MM files
)

# Set Language Standards and ARC
set_target_properties(FWAXPC PROPERTIES
    OBJCXX_STANDARD 23             # Or your desired C++ standard for ObjC++
    OBJCXX_STANDARD_REQUIRED ON
    OBJCXX_EXTENSIONS OFF
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJCXX_ARC YES
)

# Install target (optional)
install(TARGETS FWAXPC
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
