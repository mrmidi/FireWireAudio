/**
 * @file main.cpp
 * @brief Entry point for the FireWire Audio Daemon
 *
 * This daemon monitors FireWire devices, detects audio interfaces,
 * and manages their connection and configuration in the system.
 */

#include <iostream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "FWA/IOKitFireWireDeviceDiscovery.h"
#include "FWA/DeviceController.h"
#include "FWA/CommandInterface.h"
#include "FWA/DeviceParser.hpp"
#include <CoreFoundation/CoreFoundation.h> 
#include <thread>
#include <chrono>
#include <signal.h>
#include "FWA/Helpers.h"
#include "Isoch/IsoStreamHandler.hpp"
#include "FWA/Error.h"

// Global objects to gracefully handle termination signals
std::shared_ptr<FWA::IsoStreamHandler> g_streamHandler;
bool g_shuttingDown = false;

/**
 * @brief Signal handler for graceful shutdown
 * @param signal The signal caught
 */
void signalHandler(int signal) {
    if (g_shuttingDown) {
        // Force exit if already shutting down and another signal arrives
        std::exit(1);
    }
    
    g_shuttingDown = true;
    spdlog::info("Caught signal {} - shutting down...", signal);
    
    // Clean up resources in the correct order
    if (g_streamHandler) {
        g_streamHandler->stop();
        g_streamHandler.reset();
    }
    
    // Stop the runloop
    CFRunLoopStop(CFRunLoopGetCurrent());
}

/**
 * @brief Main program entry point
 * @return int Exit status
 */
int main() {
    try {
        // Set up spdlog
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto logger = std::make_shared<spdlog::logger>("daemon_logger", console_sink);
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::debug); // Set log level to DEBUG!

        spdlog::info("Daemon starting...");
        
        // Set up signal handling for graceful shutdown
        signal(SIGINT, signalHandler);  // Ctrl+C
        signal(SIGTERM, signalHandler); // killall
        
        // Create a shared pointer to DeviceController first
        auto controller = std::make_shared<FWA::DeviceController>(nullptr);
        
        // Now create the discovery with the controller
        auto discovery = std::make_unique<FWA::IOKitFireWireDeviceDiscovery>(controller);
        
        // Set the discovery in the controller
        controller->setDiscovery(std::move(discovery));

        FWA::DeviceNotificationCallback callback = [&controller, &logger](std::shared_ptr<FWA::AudioDevice> device, bool connected) {
            if (connected) {
                spdlog::info("Device connected: {}, Vendor: {}", device->getDeviceName(), device->getVendorName());

                // Get the CommandInterface from the AudioDevice.
                auto commandInterface = device->getCommandInterface();

                if (!commandInterface) {
                    spdlog::error("Command interface is NULL");
                    return;
                }

                // Activate the command interface if needed
                if (commandInterface->isActive()) {
                    spdlog::info("CommandInterface already activated");
                } else {
                    auto activationResult = commandInterface->activate();
                    if (!activationResult) {
                        //                    spdlog::error("Failed to activate CommandInterface: 0x{:x}", activationResult.error().iokit_return());
                        spdlog::error("Failed to activate CommandInterface");
                        return;
                    }
                }
                
                // Create the IsoStreamHandler for the device
                g_streamHandler = std::make_shared<FWA::IsoStreamHandler>(device, logger, commandInterface, device->getDeviceInterface());
                
                // Start the stream handler
                auto streamResult = g_streamHandler->start();
                if (!streamResult) {
                    spdlog::error("Failed to start IsoStreamHandler");
                    g_streamHandler.reset();
                    return;
                }
                
                spdlog::info("IsoStreamHandler started successfully");
                
                // Stay in the run loop to keep processing device events
                // Only stop when explicitly requested (via signal handler)

            } else {
                spdlog::info("Device disconnected: {}", device->getGuid());
                
                // Clean up the stream handler if the device was disconnected
                if (g_streamHandler) {
                    g_streamHandler->stop();
                    g_streamHandler.reset();
                }
                
                // Don't exit the run loop - wait for another device
            }
        };

        // Start the DeviceController
        auto result = controller->start(callback);
        if (!result) {
            spdlog::error("Failed to start DeviceController: 0x{:x}", static_cast<int>(result.error()));
            exit(1);
        }
        spdlog::info("Entering main run loop...");
        CFRunLoopRun(); // This blocks until CFRunLoopStop is called.

        // Stop the device discovery process
        spdlog::info("Exiting main run loop...");
        controller->stop();

    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Log initialization failed: " << ex.what() << std::endl;
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "An error occurred: " << ex.what() << std::endl;
        return 1;
    }

    spdlog::info("Daemon shutting down...");
    return 0;
}
