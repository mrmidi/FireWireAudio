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
#include "FWA/AVCInfoBlock.hpp"
#include <iomanip>
#include <string>

// Helper function to recursively print the info block tree
void printInfoBlockTree(const FWA::AVCInfoBlock& block, int indentLevel);
static const char* MusicSubunitInfoBlockTypeDescriptions(uint16_t infoBlockType);

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
                        spdlog::error("Failed to activate CommandInterface");
                        return;
                    }
                }

                // --- Device Capabilities Discovery ---
                spdlog::info("Starting device capability discovery...");
                FWA::DeviceParser parser(device.get());
                auto parseResult = parser.parse();
                if (!parseResult) {
                    spdlog::error("Device capability discovery failed: 0x{:x}", static_cast<int>(parseResult.error()));
                } else {
                    // Print discovered capabilities
                    const auto& info = device->getDeviceInfo();
                    std::cout << "\n==== Device Capabilities for: " << device->getDeviceName() << " ====" << std::endl;
                    std::cout << "Isochronous Input Plugs:  " << info.getNumIsoInputPlugs() << std::endl;
                    std::cout << "Isochronous Output Plugs: " << info.getNumIsoOutputPlugs() << std::endl;
                    std::cout << "External Input Plugs:     " << info.getNumExternalInputPlugs() << std::endl;
                    std::cout << "External Output Plugs:    " << info.getNumExternalOutputPlugs() << std::endl;
                    std::cout << "Has Music Subunit:        " << (info.hasMusicSubunit() ? "Yes" : "No") << std::endl;
                    std::cout << "Has Audio Subunit:        " << (info.hasAudioSubunit() ? "Yes" : "No") << std::endl;

                    // List Iso Input Plugs
                    for (const auto& plug : info.getIsoInputPlugs()) {
                        std::cout << "  [Iso In Plug " << (int)plug->getPlugNum() << "] Usage: " << plug->getPlugUsageString();
                        if (plug->getCurrentStreamFormat())
                            std::cout << ", Format: " << plug->getCurrentStreamFormat()->toString();
                        std::cout << std::endl;
                    }
                    // List Iso Output Plugs
                    for (const auto& plug : info.getIsoOutputPlugs()) {
                        std::cout << "  [Iso Out Plug " << (int)plug->getPlugNum() << "] Usage: " << plug->getPlugUsageString();
                        if (plug->getCurrentStreamFormat())
                            std::cout << ", Format: " << plug->getCurrentStreamFormat()->toString();
                        std::cout << std::endl;
                    }
                    // List External Input Plugs
                    for (const auto& plug : info.getExternalInputPlugs()) {
                        std::cout << "  [Ext In Plug " << (int)plug->getPlugNum() << "] Usage: " << plug->getPlugUsageString();
                        if (plug->getCurrentStreamFormat())
                            std::cout << ", Format: " << plug->getCurrentStreamFormat()->toString();
                        std::cout << std::endl;
                    }
                    // List External Output Plugs
                    for (const auto& plug : info.getExternalOutputPlugs()) {
                        std::cout << "  [Ext Out Plug " << (int)plug->getPlugNum() << "] Usage: " << plug->getPlugUsageString();
                        if (plug->getCurrentStreamFormat())
                            std::cout << ", Format: " << plug->getCurrentStreamFormat()->toString();
                        std::cout << std::endl;
                    }
                    // Music Subunit Plugs
                    if (info.hasMusicSubunit()) {
                        const auto& music = info.getMusicSubunit();
                        std::cout << "Music Subunit Dest Plugs:  " << music.getMusicDestPlugCount() << std::endl;
                        for (const auto& plug : music.getMusicDestPlugs()) {
                            std::cout << "  [Music Dest Plug " << (int)plug->getPlugNum() << "] Usage: " << plug->getPlugUsageString();
                            if (plug->getCurrentStreamFormat())
                                std::cout << ", Format: " << plug->getCurrentStreamFormat()->toString();
                            std::cout << std::endl;
                        }
                        std::cout << "Music Subunit Source Plugs: " << music.getMusicSourcePlugCount() << std::endl;
                        for (const auto& plug : music.getMusicSourcePlugs()) {
                            std::cout << "  [Music Source Plug " << (int)plug->getPlugNum() << "] Usage: " << plug->getPlugUsageString();
                            if (plug->getCurrentStreamFormat())
                                std::cout << ", Format: " << plug->getCurrentStreamFormat()->toString();
                            std::cout << std::endl;
                        }
                    }
                    // Audio Subunit Plugs
                    if (info.hasAudioSubunit()) {
                        const auto& audio = info.getAudioSubunit();
                        std::cout << "Audio Subunit Dest Plugs:  " << audio.getAudioDestPlugCount() << std::endl;
                        for (const auto& plug : audio.getAudioDestPlugs()) {
                            std::cout << "  [Audio Dest Plug " << (int)plug->getPlugNum() << "] Usage: " << plug->getPlugUsageString();
                            if (plug->getCurrentStreamFormat())
                                std::cout << ", Format: " << plug->getCurrentStreamFormat()->toString();
                            std::cout << std::endl;
                        }
                        std::cout << "Audio Subunit Source Plugs: " << audio.getAudioSourcePlugCount() << std::endl;
                        for (const auto& plug : audio.getAudioSourcePlugs()) {
                            std::cout << "  [Audio Source Plug " << (int)plug->getPlugNum() << "] Usage: " << plug->getPlugUsageString();
                            if (plug->getCurrentStreamFormat())
                                std::cout << ", Format: " << plug->getCurrentStreamFormat()->toString();
                            std::cout << std::endl;
                        }
                    }
                    std::cout << "==== End Device Capabilities ====" << std::endl;

                    // --- NEW: Print Music Subunit Status Descriptor Info Block Tree ---
                    if (info.hasMusicSubunit()) {
                        const auto& musicSubunit = info.getMusicSubunit();
                        const auto& topLevelBlocks = musicSubunit.getParsedStatusInfoBlocks();
                        if (!topLevelBlocks.empty()) {
                            std::cout << "\n---- Music Subunit Status Descriptor Info Blocks Tree ----" << std::endl;
                            for (const auto& blockPtr : topLevelBlocks) {
                                if (blockPtr) {
                                    printInfoBlockTree(*blockPtr, 1);
                                }
                            }
                            std::cout << "---- End Info Blocks Tree ----" << std::endl;
                        } else {
                            if (musicSubunit.getStatusDescriptorData() && !musicSubunit.getStatusDescriptorData()->empty()) {
                                spdlog::warn("Music Subunit Status Descriptor data was fetched but no top-level info blocks were parsed.");
                            } else {
                                spdlog::info("No Music Subunit Status Descriptor info blocks found or parsed.");
                            }
                        }
                    }
                    // --- End NEW ---

                }

                // --- Streaming temporarily disabled ---
                // g_streamHandler = std::make_shared<FWA::IsoStreamHandler>(device, logger, commandInterface, device->getDeviceInterface());
                // auto streamResult = g_streamHandler->start();
                // if (!streamResult) {
                //     spdlog::error("Failed to start IsoStreamHandler");
                //     g_streamHandler.reset();
                //     return;
                // }
                // spdlog::info("IsoStreamHandler started successfully");
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

// Helper function to recursively print the info block tree
void printInfoBlockTree(const FWA::AVCInfoBlock& block, int indentLevel) {
    std::string indent(indentLevel * 2, ' ');
    std::cout << indent << "+ AVCInfoBlock:\n";
    std::cout << indent << "  Type: 0x" << std::hex << std::setw(4) << std::setfill('0') << block.getType()
              << " (" << MusicSubunitInfoBlockTypeDescriptions(block.getType()) << ")\n";
    std::cout << indent << "  Compound Length: " << std::dec << block.getCompoundLength()
              << " (Total Size: " << block.getCompoundLength() + 2 << ")\n";
    std::cout << indent << "  Primary Fields Length: " << std::dec << block.getPrimaryFieldsLength() << "\n";
    if (block.getPrimaryFieldsLength() > 0) {
        auto bytes = block.getPrimaryFieldsBytes();
        if (!bytes.empty()) {
            std::cout << indent << "  Primary Fields (" << std::dec << block.getPrimaryFieldsLength() << " bytes): ";
            std::cout << FWA::Helpers::formatHexBytes(bytes) << "\n";
        } else {
            std::cout << indent << "  Primary Fields: (Error: Could not get data pointer)\n";
        }
    } else {
        std::cout << indent << "  Primary Fields: (None)\n";
    }
    const auto& nestedBlocks = block.getNestedBlocks();
    if (!nestedBlocks.empty()) {
        std::cout << indent << "  Nested Blocks (" << std::dec << nestedBlocks.size() << "):\n";
        for (const auto& nestedBlockPtr : nestedBlocks) {
            if (nestedBlockPtr) {
                printInfoBlockTree(*nestedBlockPtr, indentLevel + 1);
            }
        }
    } else {
        std::cout << indent << "  Nested Blocks: (None)\n";
    }
}

static const char* MusicSubunitInfoBlockTypeDescriptions(uint16_t infoBlockType)
{
    switch (static_cast<FWA::InfoBlockType>(infoBlockType))
    {
        case FWA::InfoBlockType::GeneralMusicStatus: return "General Music Subunit Status Area Info Block";
        case FWA::InfoBlockType::MusicOutputPlugStatus: return "Music Output Plug Status Area Info Block";
        case FWA::InfoBlockType::SourcePlugStatus: return "Source Plug Status Info Block";
        case FWA::InfoBlockType::AudioInfo: return "Audio Info Block";
        case FWA::InfoBlockType::MidiInfo: return "MIDI Info Block";
        case FWA::InfoBlockType::SmpteTimeCodeInfo: return "SMPTE Time Code Info Block";
        case FWA::InfoBlockType::SampleCountInfo: return "Sample Count Info Block";
        case FWA::InfoBlockType::AudioSyncInfo: return "Audio SYNC Info Block";
        case FWA::InfoBlockType::RoutingStatus: return "Routing Status Info Block";
        case FWA::InfoBlockType::SubunitPlugInfo: return "Subunit Plug Info Block";
        case FWA::InfoBlockType::ClusterInfo: return "Cluster Info Block";
        case FWA::InfoBlockType::MusicPlugInfo: return "Music Plug Info Block";
        case FWA::InfoBlockType::Name: return "Name Info Block";
        case FWA::InfoBlockType::RawText: return "Raw Text Info Block";
        default: return "Unknown";
    }
}
