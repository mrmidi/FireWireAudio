#include "Isoch/utils/RunLoopHelper.hpp"
#include <spdlog/spdlog.h>
#include <pthread.h>

void logRunLoopInfo(const char* className, const char* functionName, CFRunLoopRef runLoop) {
    spdlog::info("[RunLoop] Class: {} | Function: {} | CFRunLoopRef: {} | Thread ID: {}",
                 className, functionName, (void*)runLoop, (unsigned long)pthread_self());
}

void logCallbackThreadInfo(const char* className, const char* callbackName, void* refcon) {
    spdlog::info("[Callback] Class: {} | Function: {} | Thread ID: {} | refcon: {}",
                 className, callbackName, (unsigned long)pthread_self(), (void*)refcon);
}
