# FWA-Control/CMakeLists.txt

# Collect all Swift source files in this directory and subdirectories
file(GLOB_RECURSE FWA_CONTROL_SWIFT_SOURCES
    *.swift
)

# Make the list available to the parent scope
set(SWIFT_APP_SOURCES ${FWA_CONTROL_SWIFT_SOURCES} PARENT_SCOPE)
