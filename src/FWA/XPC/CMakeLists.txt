# XPC Bridge Library
add_library(FWA_XPC STATIC
    MixedAudioBuffer.m
    XPCBridge.mm
    XPCReceiverClient.mm
)
set_source_files_properties(MixedAudioBuffer.m PROPERTIES LANGUAGE OBJC)
set_source_files_properties(XPCBridge.mm XPCReceiverClient.mm PROPERTIES LANGUAGE OBJCXX)
target_compile_options(FWA_XPC PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-std=c++23 -stdlib=libc++>)
target_include_directories(FWA_XPC PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>)
target_link_libraries(FWA_XPC PUBLIC spdlog::spdlog "-framework Foundation")
