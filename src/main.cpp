/**
 * @file main.cpp
 * @brief Entry point for testing the FireWire Audio C++ Library directly.
 */

#include <iostream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "FWA/IOKitFireWireDeviceDiscovery.h"
#include "FWA/DeviceController.h"
#include "FWA/CommandInterface.h"
// #include "FWA/DeviceParser.hpp" // No longer need to explicitly create parser here
#include <CoreFoundation/CoreFoundation.h>
#include <thread>
#include <chrono>
#include <csignal> // Use csignal for C++ style
#include "FWA/Helpers.h"
// #include "Isoch/IsoStreamHandler.hpp" // Keep if testing streaming
#include "FWA/Error.h"
#include "FWA/AVCInfoBlock.hpp"
#include <iomanip>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <atomic>

// Forward declaration for helper
void printInfoBlockTree(const FWA::AVCInfoBlock& block, int indentLevel);
static const char* MusicSubunitInfoBlockTypeDescriptions(uint16_t infoBlockType);

// Global flag for shutdown
std::atomic<bool> g_shuttingDown = false;
// Global pointer to controller to stop run loop
std::weak_ptr<FWA::DeviceController> g_controller_wp;

void signalHandler(int signal) {
    if (g_shuttingDown.exchange(true)) {
        std::exit(1);
    }
    spdlog::info("Caught signal {} - shutting down...", signal);
    if (auto controller_sp = g_controller_wp.lock()) {
        CFRunLoopStop(CFRunLoopGetCurrent());
    } else {
        spdlog::warn("Controller already destroyed during shutdown.");
    }
}

int main() {
    std::shared_ptr<spdlog::logger> logger;
    std::shared_ptr<FWA::DeviceController> controller_sp;
    try {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        logger = std::make_shared<spdlog::logger>("daemon_logger", console_sink);
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::trace);
        spdlog::info("main.cpp starting...");

        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);

        controller_sp = std::make_shared<FWA::DeviceController>(nullptr);
        g_controller_wp = controller_sp;
        auto discovery = std::make_unique<FWA::IOKitFireWireDeviceDiscovery>(controller_sp);
        controller_sp->setDiscovery(std::move(discovery));

        FWA::DeviceNotificationCallback cpp_callback =
            [&logger](std::shared_ptr<FWA::AudioDevice> device, bool connected) {
            if (connected) {
                device->startStreams();
                // spdlog::info("Device connected: '{}' (Vendor: '{}', GUID: 0x{:x})",
                //              device->getDeviceName(), device->getVendorName(), device->getGuid());
                // // PARSING IS NOW DONE INTERNALLY DURING device->init() triggered by Discovery
                // // We just need to access the results stored in device->getDeviceInfo()
                // const auto& info = device->getDeviceInfo();
                // spdlog::info("--- Parsed Device Capabilities (from main.cpp callback) ---");
                // spdlog::info("  Iso In Plugs:  {}", info.getNumIsoInputPlugs());
                // spdlog::info("  Iso Out Plugs: {}", info.getNumIsoOutputPlugs());
                // spdlog::info("  Ext In Plugs:  {}", info.getNumExternalInputPlugs());
                // spdlog::info("  Ext Out Plugs: {}", info.getNumExternalOutputPlugs());
                // spdlog::info("  Has Music SU:  {}", info.hasMusicSubunit());
                // spdlog::info("  Has Audio SU:  {}", info.hasAudioSubunit());
                // try {
                //     nlohmann::json deviceInfoJson = info.toJson(*device);
                //     spdlog::debug("Device JSON:\n{}", deviceInfoJson.dump(2));
                // } catch (const std::exception& e) {
                //     spdlog::error("Error generating JSON in main.cpp callback: {}", e.what());
                // }
                // if (info.hasMusicSubunit()) {
                //     const auto& musicSubunit = info.getMusicSubunit();
                //     const auto& topLevelBlocks = musicSubunit.getParsedStatusInfoBlocks();
                //     if (!topLevelBlocks.empty()) {
                //         spdlog::info("--- Music Subunit Status Descriptor Info Blocks Tree (from main.cpp callback) ---");
                //         for (const auto& blockPtr : topLevelBlocks) {
                //             if (blockPtr) {
                //                 std::cout << "--- Block Tree Start ---" << std::endl;
                //                 printInfoBlockTree(*blockPtr, 1);
                //                 std::cout << "--- Block Tree End ---" << std::endl;
                //             }
                //         }
                //     }
                // }
            } else {
                spdlog::info("Device disconnected: GUID 0x{:x}", device ? device->getGuid() : 0);
            }
        };

        auto startResult = controller_sp->start(cpp_callback);
        if (!startResult) {
            spdlog::critical("Failed to start DeviceController: 0x{:x}", static_cast<int>(startResult.error()));
            return 1;
        }

        spdlog::info("Entering main run loop... Press Ctrl+C to exit.");
        CFRunLoopRun();
        spdlog::info("Exiting main run loop...");
        if(controller_sp) {
            spdlog::info("Explicitly stopping controller...");
            controller_sp->stop();
        }
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Log initialization failed: " << ex.what() << std::endl;
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "An error occurred: " << ex.what() << std::endl;
        if(controller_sp) { controller_sp->stop(); }
        return 1;
    } catch (...) {
        std::cerr << "An unknown error occurred." << std::endl;
        if(controller_sp) { controller_sp->stop(); }
        return 1;
    }

    spdlog::info("main.cpp finished cleanly.");
    spdlog::default_logger()->flush();
    return 0;
}

// Helper function to recursively print the info block tree
void printInfoBlockTree(const FWA::AVCInfoBlock& block, int indentLevel) {
    std::string indent(indentLevel * 2, ' ');
    std::cout << indent << "+ AVCInfoBlock:\n";
    std::cout << indent << "  Type: 0x" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<uint16_t>(block.getType())
              << " (" << MusicSubunitInfoBlockTypeDescriptions(static_cast<uint16_t>(block.getType())) << ")\n";
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
