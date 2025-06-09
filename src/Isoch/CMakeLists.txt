# Isoch Library
add_library(FWAIsoch STATIC
    AudioDeviceStream.cpp
    IsoStreamHandler.cpp
    SharedManagers.cpp
    core/AmdtpReceiver.cpp
    core/AmdtpTransmitter.cpp
    core/AppleSyTGenerator.cpp
    core/IsochBufferManager.cpp
    core/IsochTransportManager.cpp
    core/IsochPacketProcessor.cpp
    core/IsochMonitoringManager.cpp
    core/ReceiverFactory.cpp
    core/AudioClockPLL.cpp
    core/IsochDCLManager.cpp
    core/IsochPortChannelManager.cpp
    core/IsochTransmitBufferManager.cpp
    core/IsochTransmitDCLManager.cpp
    core/IsochPacketProvider.cpp
    utils/AmdtpHelpers.cpp
    # utils/CIPHeaderHandler.cpp
    utils/RunLoopHelper.cpp
)
target_include_directories(FWAIsoch PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>)
target_link_libraries(FWAIsoch PUBLIC spdlog::spdlog nlohmann_json::nlohmann_json Shared "-framework IOKit" "-framework CoreFoundation" "-framework Foundation")
