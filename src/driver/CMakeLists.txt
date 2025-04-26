# Driver CMakeLists.txt
# TODO: 
# Build with -Os -fno-stack-protector -fomit-frame-pointer -ftrivial-auto-var-init=zero only for the RT translation unit
# NOT NOW, After initial testing

add_library(FWADriver MODULE
    FWADriver.cpp
    FWADriverDevice.cpp
    FWADriverDevice.hpp
    FWADriverHandler.cpp
    FWADriverHandler.hpp
    FWADriverInit.cpp
    FWADriverInit.hpp
)
set_target_properties(FWADriver PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "driver"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/Info.plist
    PREFIX ""
    SUFFIX ""
)
target_include_directories(FWADriver PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${libaspl_SOURCE_DIR}/include
    ${libaspl_BINARY_DIR}/include
)
target_link_libraries(FWADriver
    PRIVATE
        FWA
        libASPL
        "-framework CoreFoundation"
        "-framework CoreAudio"
)
