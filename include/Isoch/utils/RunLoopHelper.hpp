#pragma once
#include <CoreFoundation/CoreFoundation.h>

// Debugging tool to trace run loop creation and callback execution

// Function to log run loop creation with class name
void logRunLoopInfo(const char* className, const char* functionName, CFRunLoopRef runLoop);

// Function to log callback thread execution
void logCallbackThreadInfo(const char* className, const char* callbackName, void* refcon);
