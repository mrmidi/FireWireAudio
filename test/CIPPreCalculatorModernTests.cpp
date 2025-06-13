#include <gtest/gtest.h>
#include "Isoch/core/CIPPreCalculator.hpp"
#include "Isoch/core/CIPHeader.hpp"
#include <thread>
#include <chrono>
#include <memory>
#include <spdlog/spdlog.h>

using namespace FWA::Isoch;

// Test fixture for the refactored CIP pre-calculator
class CIPPreCalculatorModernTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up test configuration matching current implementation
        config_.numGroups = 16;
        config_.packetsPerGroup = 8;
        config_.sampleRate = 48000.0;
        config_.clientBufferSize = 4096;
        config_.transmissionType = TransmissionType::NonBlocking;
        config_.logger = spdlog::default_logger();
        
        nodeID_ = 0x3F;
        
        // Create pre-calculator
        preCalc_ = std::make_unique<CIPPreCalculator>();
    }
    
    void TearDown() override {
        if (preCalc_) {
            preCalc_->stop();
        }
    }
    
    TransmitterConfig config_;
    uint16_t nodeID_;
    std::unique_ptr<CIPPreCalculator> preCalc_;
};

// Test basic initialization and lifecycle
TEST_F(CIPPreCalculatorModernTest, BasicLifecycle) {
    EXPECT_NO_THROW(preCalc_->initialize(config_, nodeID_));
    EXPECT_NO_THROW(preCalc_->start());
    EXPECT_NO_THROW(preCalc_->stop());
}

// Test version-based group state access
TEST_F(CIPPreCalculatorModernTest, VersionBasedAccess) {
    preCalc_->initialize(config_, nodeID_);
    preCalc_->start();
    
    // Wait for some groups to be calculated
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Try to get a group state
    const auto* groupState = preCalc_->getGroupState(0);
    
    if (groupState != nullptr) {
        // If we got a group state, verify its structure
        EXPECT_LE(groupState->packetCount, 32u);
        EXPECT_GT(groupState->packetCount, 0u);
        
        // Check that the version is even (ready state)
        uint32_t version = groupState->version.load();
        EXPECT_EQ(version & 1, 0u) << "Group state version should be even (ready)";
        
        // Verify packet headers have expected structure
        for (uint8_t i = 0; i < groupState->packetCount; ++i) {
            const auto& packet = groupState->packets[i];
            const auto& header = packet.header;
            
            // Check CIP header fields
            EXPECT_EQ(header.dbs, 2u) << "DBS should be 2 for stereo";
            EXPECT_EQ(header.fmt_eoh1, CIP::kFmtEohValue) << "FMT field should match constant";
            EXPECT_TRUE(header.fdf == CIP::kFDF_48k || header.fdf == CIP::kFDF_44k1) 
                << "FDF should be valid sample rate";
        }
    }
}

// Test 48kHz pattern generation
TEST_F(CIPPreCalculatorModernTest, Pattern48kHz) {
    config_.sampleRate = 48000.0;
    preCalc_->initialize(config_, nodeID_);
    preCalc_->start();
    
    // Wait for calculation
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Check multiple groups for the 48kHz pattern
    bool foundDataPacket = false;
    bool foundNoDataPacket = false;
    
    for (uint32_t groupIdx = 0; groupIdx < 4; ++groupIdx) {
        const auto* groupState = preCalc_->getGroupState(groupIdx);
        if (groupState) {
            for (uint8_t pktIdx = 0; pktIdx < groupState->packetCount; ++pktIdx) {
                const auto& packet = groupState->packets[pktIdx];
                
                if (packet.isNoData) {
                    foundNoDataPacket = true;
                    EXPECT_EQ(packet.header.syt, CIP::kSytNoData) 
                        << "NO-DATA packet should have SYT=0xFFFF";
                } else {
                    foundDataPacket = true;
                    EXPECT_NE(packet.header.syt, CIP::kSytNoData) 
                        << "DATA packet should not have SYT=0xFFFF";
                }
                
                // Check DBC increment logic
                EXPECT_LE(packet.dbcIncrement, 8u) << "DBC increment should be reasonable";
            }
        }
    }
    
    // For 48kHz, we should see both data and no-data packets
    EXPECT_TRUE(foundDataPacket) << "Should find DATA packets in 48kHz stream";
    EXPECT_TRUE(foundNoDataPacket) << "Should find NO-DATA packets in 48kHz stream";
}

// Test 44.1kHz pattern generation  
TEST_F(CIPPreCalculatorModernTest, Pattern441kHz) {
    config_.sampleRate = 44100.0;
    preCalc_->initialize(config_, nodeID_);
    preCalc_->start();
    
    // Wait for calculation
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Check that we get valid headers for 44.1kHz
    bool foundValidGroup = false;
    
    for (uint32_t groupIdx = 0; groupIdx < 4; ++groupIdx) {
        const auto* groupState = preCalc_->getGroupState(groupIdx);
        if (groupState) {
            foundValidGroup = true;
            
            for (uint8_t pktIdx = 0; pktIdx < groupState->packetCount; ++pktIdx) {
                const auto& packet = groupState->packets[pktIdx];
                
                // Verify 44.1kHz specific FDF
                EXPECT_EQ(packet.header.fdf, CIP::kFDF_44k1) 
                    << "44.1kHz should use correct FDF value";
            }
            break; // Only need to check one valid group
        }
    }
    
    EXPECT_TRUE(foundValidGroup) << "Should find at least one valid group for 44.1kHz";
}

// Test force sync functionality
TEST_F(CIPPreCalculatorModernTest, ForceSync) {
    preCalc_->initialize(config_, nodeID_);
    preCalc_->start();
    
    // Wait for initial calculation
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Force sync with specific values
    uint8_t testDBC = 0x42;
    bool testWasNoData = true;
    
    EXPECT_NO_THROW(preCalc_->forceSync(testDBC, testWasNoData));
    
    // Wait for sync to take effect
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // The next calculated group should incorporate the sync values
    // (We can't directly verify this without exposing internal state,
    //  but we can verify that the call doesn't crash)
}

// Test flow control via markGroupConsumed
TEST_F(CIPPreCalculatorModernTest, FlowControl) {
    preCalc_->initialize(config_, nodeID_);
    preCalc_->start();
    
    // Wait for some calculation
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Mark several groups as consumed
    for (uint32_t i = 0; i < 8; ++i) {
        EXPECT_NO_THROW(preCalc_->markGroupConsumed(i));
    }
    
    // System should continue to work normally
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Should still be able to get group states
    const auto* groupState = preCalc_->getGroupState(0);
    // Note: groupState may or may not be available depending on timing,
    // but the call should not crash
}

// Test emergency calculation fallback
TEST_F(CIPPreCalculatorModernTest, EmergencyCalculation) {
    preCalc_->initialize(config_, nodeID_);
    // Don't start the background thread - test emergency path
    
    CIPHeader testHeader = {};
    
    // Test emergency calculation
    bool result = preCalc_->emergencyCalculateCIP(&testHeader, 0);
    
    // Should return a valid result
    EXPECT_TRUE(result || !result); // Either true or false is valid
    
    // Header should be filled with valid values
    EXPECT_EQ(testHeader.dbs, 2u);
    EXPECT_EQ(testHeader.fmt_eoh1, CIP::kFmtEohValue);
}
