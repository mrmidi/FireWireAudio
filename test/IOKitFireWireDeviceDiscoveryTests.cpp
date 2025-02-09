// FireWireAudioDaemonTests/IOKitFireWireDeviceDiscoveryTests.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>
#include "FWA/IOKitFireWireDeviceDiscovery.h"
#include "FWA/AudioDevice.h"
#include <memory>
#include <IOKit/IOKitLib.h>
#include <thread> // Required for std::this_thread
#include <chrono> // Required for std::chrono
#include <iostream>


TEST_CASE("IOKitFireWireDeviceDiscovery - Initial Tests", "[discovery]") {
    FWA::IOKitFireWireDeviceDiscovery discovery;

    SECTION("Start and Stop Discovery - No Crash") {
        auto startResult = discovery.startDiscovery(nullptr);
        REQUIRE(startResult.has_value());

        auto stopResult = discovery.stopDiscovery();
        REQUIRE(stopResult.has_value());
    }

    SECTION("getConnectedDevices returns empty vector") {
        auto result = discovery.getConnectedDevices();
        REQUIRE(result.has_value());
        REQUIRE(result.value().empty() == true); // should be empty, because we didn't add any device.
    }
    SECTION("getDeviceByGuid returns no such device") {
        auto result = discovery.getDeviceByGuid(0);
        REQUIRE(!result.has_value());
        REQUIRE(result.error().iokit_return() == kIOReturnNotFound);
    }

    SECTION("startDiscovery - Attempts IOKit Initialization") {
        auto result = discovery.startDiscovery(nullptr);
        REQUIRE(result.has_value());
        CHECK(discovery.isMasterPortValid());
        CHECK(discovery.isNotificationPortValid());
    }
}

TEST_CASE("IOKitFireWireDeviceDiscovery - Device Arrival and Removal", "[discovery]") {
    FWA::IOKitFireWireDeviceDiscovery discovery;

    SECTION("startDiscovery - Sets up matching dictionary and notifications") {
        auto startResult = discovery.startDiscovery(nullptr);
        REQUIRE(startResult.has_value());
        CHECK(discovery.isMasterPortValid());
        CHECK(discovery.isNotificationPortValid());
    }

    SECTION("Callback is called when a device is added and removed") {
        std::shared_ptr<FWA::AudioDevice> addedDevice = nullptr;
        std::shared_ptr<FWA::AudioDevice> removedDevice = nullptr;
        bool deviceConnected = false;
        bool deviceDisconnected = false;

        FWA::DeviceNotificationCallback callback = [&](std::shared_ptr<FWA::AudioDevice> device, bool connected) {
            if (connected) {
                addedDevice = device;
                deviceConnected = true;
            } else {
                removedDevice = device;
                deviceDisconnected = true;
            }
            CFRunLoopStop(CFRunLoopGetCurrent());
        };

        discovery.setTestCallback(callback);
        auto startResult = discovery.startDiscovery(callback);
        REQUIRE(startResult.has_value());

        std::cout << "Please connect a FireWire audio device and press Enter..." << std::endl;
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 5, false);

        REQUIRE(addedDevice != nullptr);
        REQUIRE(deviceConnected == true);

        std::cout << "Please disconnect the FireWire audio device and press Enter..." << std::endl;
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 5, false);

        REQUIRE(removedDevice != nullptr);
        REQUIRE(deviceDisconnected == true);
        REQUIRE(addedDevice->getGuid() == removedDevice->getGuid());

        auto stopResult = discovery.stopDiscovery();
        REQUIRE(stopResult.has_value());
        discovery.setTestCallback(nullptr);
    }

    SECTION("Adding and removing Device to internal storage") {
        std::shared_ptr<FWA::AudioDevice> addedDevice = nullptr;
        std::shared_ptr<FWA::AudioDevice> removedDevice = nullptr;
        bool deviceConnected = false;
        bool deviceDisconnected = false;

        FWA::DeviceNotificationCallback callback = [&](std::shared_ptr<FWA::AudioDevice> device, bool connected) {
            if (connected) {
                addedDevice = device;
                deviceConnected = true;
            } else {
                removedDevice = device;
                deviceDisconnected = true;
            }
            CFRunLoopStop(CFRunLoopGetCurrent());
        };

        discovery.setTestCallback(callback);
        auto startResult = discovery.startDiscovery(callback);
        REQUIRE(startResult.has_value());

        std::cout << "Please connect a FireWire audio device, test will continue automatically..." << std::endl;
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 5, false);

        bool deviceAdded = false;
        for (int i = 0; i < 100; ++i) {
            auto devicesResult = discovery.getConnectedDevices();
            if (devicesResult.has_value() && !devicesResult.value().empty()) {
                deviceAdded = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        REQUIRE(deviceAdded == true);

        auto devicesResult = discovery.getConnectedDevices();
        REQUIRE(devicesResult.has_value());
        REQUIRE(devicesResult.value().size() == 1);

        std::cout << "Please disconnect the FireWire audio device, test will continue automatically..." << std::endl;
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 5, false);

        bool deviceRemoved = false;
        for (int i = 0; i < 100; ++i) {
            auto devicesResultAfter = discovery.getConnectedDevices();
            if (devicesResultAfter.has_value() && devicesResultAfter.value().empty()) {
                deviceRemoved = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        REQUIRE(deviceRemoved == true);

        auto devicesResultAfterRemoval = discovery.getConnectedDevices();
        REQUIRE(devicesResultAfterRemoval.has_value());
        REQUIRE(devicesResultAfterRemoval.value().empty());

        auto stopResult = discovery.stopDiscovery();
        REQUIRE(stopResult.has_value());
        discovery.setTestCallback(nullptr);
    }
}