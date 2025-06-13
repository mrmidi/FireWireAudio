#include <catch2/catch_test_macros.hpp>
#include "Isoch/core/CIPPreCalculator.hpp"
#include "Isoch/core/TransmitterTypes.hpp"
#include <spdlog/spdlog.h>
#include <memory>

using namespace FWA::Isoch;

// Simple Catch2 tests for basic functionality
TEST_CASE("CIPPreCalculator - Basic Construction", "[cip_precalc]") {
    CIPPreCalculator preCalc;
    
    SECTION("Can construct and destruct") {
        // Just verify we can create and destroy without crashing
        REQUIRE(true);
    }
}

TEST_CASE("CIPPreCalculator - Basic Configuration", "[cip_precalc]") {
    CIPPreCalculator preCalc;
    
    TransmitterConfig config;
    config.numGroups = 16;
    config.packetsPerGroup = 8;
    config.sampleRate = 48000.0;
    config.clientBufferSize = 4096;
    config.transmissionType = TransmissionType::NonBlocking;
    auto logger = spdlog::get("default");
    if (!logger) {
        logger = spdlog::default_logger();
    }
    config.logger = logger;
    
    SECTION("Can initialize") {
        REQUIRE_NOTHROW(preCalc.initialize(config, 0x3F));
    }
    
    SECTION("Can start and stop") {
        preCalc.initialize(config, 0x3F);
        REQUIRE_NOTHROW(preCalc.start());
        REQUIRE_NOTHROW(preCalc.stop());
    }
}

TEST_CASE("CIPPreCalculator - Group State Access", "[cip_precalc]") {
    CIPPreCalculator preCalc;
    
    TransmitterConfig config;
    config.numGroups = 16;
    config.packetsPerGroup = 8;
    config.sampleRate = 48000.0;
    config.clientBufferSize = 4096;
    config.transmissionType = TransmissionType::NonBlocking;
    auto logger = spdlog::get("default");
    if (!logger) {
        logger = spdlog::default_logger();
    }
    config.logger = logger;
    
    preCalc.initialize(config, 0x3F);
    
    SECTION("getGroupState returns nullptr for unready groups") {
        const auto* state = preCalc.getGroupState(0);
        // Should be nullptr initially since no groups have been calculated yet
        REQUIRE(state == nullptr);
    }
    
    SECTION("Can mark groups as consumed") {
        REQUIRE_NOTHROW(preCalc.markGroupConsumed(0));
        REQUIRE_NOTHROW(preCalc.markGroupConsumed(5));
        REQUIRE_NOTHROW(preCalc.markGroupConsumed(15));
    }
}

// Note: More comprehensive tests are in the Google Test suite
// This Catch2 suite focuses on basic functionality and integration
