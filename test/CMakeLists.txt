include(CTest)

add_executable(fwadaemon_tests
    IOKitFireWireDeviceDiscoveryTests.cpp
)

target_link_libraries(fwadaemon_tests
    PRIVATE
        Catch2::Catch2WithMain
        FWA
        "-framework IOKit"           # Link IOKit framework
        "-framework CoreFoundation"  # Link CoreFoundation framework
)

# Include directories for the test target
target_include_directories(fwadaemon_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include 
)

add_test(NAME fwadaemon_tests COMMAND fwadaemon_tests)