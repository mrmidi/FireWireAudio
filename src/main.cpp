// src/main.cpp
#include <iostream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "FWA/IOKitFireWireDeviceDiscovery.h"
#include "FWA/DeviceController.h"
#include "FWA/CommandInterface.h"
#include "FWA/DeviceParser.hpp"  // << ADDED
#include <CoreFoundation/CoreFoundation.h> 
#include <thread>
#include <chrono>
#include "FWA/Helpers.h"

int main() {
    try {
        // Set up spdlog
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto logger = std::make_shared<spdlog::logger>("daemon_logger", console_sink);
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::debug); // Set log level to DEBUG!

        spdlog::info("Daemon starting...");

        // Create instances using dependency injection
        auto discovery = std::make_unique<FWA::IOKitFireWireDeviceDiscovery>();
        FWA::DeviceController controller(std::move(discovery));

        FWA::DeviceNotificationCallback callback = [&controller](std::shared_ptr<FWA::AudioDevice> device, bool connected) {
            if (connected) {
                spdlog::info("Device connected: {}, Vendor: {}", device->getDeviceName(), device->getVendorName());

                // Get the CommandInterface from the AudioDevice.
                auto commandInterface = device->getCommandInterface();

                if (!commandInterface) {
                    spdlog::error("Command interface is NULL");
                    return;
                }

//                // Activate the command interface
//                auto activationResult = commandInterface->activate();
//                if (!activationResult) {
//                    spdlog::error("Failed to activate CommandInterface: 0x{:x}", activationResult.error().iokit_return());
//                    return;
//                }
                // **Exit the run loop once parsing is complete**
                CFRunLoopStop(CFRunLoopGetCurrent());

            } else {
                spdlog::info("Device disconnected: {}", device->getGuid());
                CFRunLoopStop(CFRunLoopGetCurrent());
            }
        };

        // Start the DeviceController
        auto result = controller.start(callback);
        if (!result) {
            spdlog::error("Failed to start DeviceController: 0x{:x}", result.error().iokit_return());
            return 1;
        }
        spdlog::info("Entering main run loop...");
        CFRunLoopRun(); // This blocks until CFRunLoopStop is called.

        // Stop the device discovery process
        spdlog::info("Exiting main run loop...");
        controller.stop();

    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Log initialization failed: " << ex.what() << std::endl;
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "An error occurred: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
