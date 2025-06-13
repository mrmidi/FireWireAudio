include(CTest)

# Catch2 Tests (existing)
add_executable(fwadaemon_tests_catch2
    IOKitFireWireDeviceDiscoveryTests.cpp
)

target_link_libraries(fwadaemon_tests_catch2
    PRIVATE
        Catch2::Catch2WithMain
        FWA
        "-framework IOKit"           # Link IOKit framework
        "-framework CoreFoundation"  # Link CoreFoundation framework
)

target_include_directories(fwadaemon_tests_catch2
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include 
)

# Google Test + Mock Tests (new - better for complex testing)
add_executable(fwadaemon_tests_gtest
    CIPPreCalculatorModernTests.cpp
    # ConcurrencyTests.cpp  # Commented out temporarily due to spdlog API issues
)

target_link_libraries(fwadaemon_tests_gtest
    PRIVATE
        gtest_main
        gmock_main
        FWAIsoch  # We need the Isoch library for CIP pre-calculator
        Shared
        spdlog
        "-framework IOKit"
        "-framework CoreFoundation"
)

target_include_directories(fwadaemon_tests_gtest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src  # For accessing implementation details
)

# Register tests
add_test(NAME catch2_tests COMMAND fwadaemon_tests_catch2)
add_test(NAME gtest_tests COMMAND fwadaemon_tests_gtest)