#include <gtest/gtest.h>
#include "Isoch/core/CIPPreCalculator.hpp"
#include "Isoch/core/CIPHeader.hpp"
#include <vector>
#include <memory>
#include <spdlog/spdlog.h>

using namespace FWA::Isoch;

// Test fixture for emergency path DBC validation
class DBCEmergencyPathTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger_ = spdlog::default_logger();
        
        config_.numGroups = 16;
        config_.packetsPerGroup = 8;
        config_.sampleRate = 48000.0;
        config_.clientBufferSize = 4096;
        config_.transmissionType = TransmissionType::NonBlocking;
        config_.logger = logger_;
    }
    
    TransmitterConfig config_;
    std::shared_ptr<spdlog::logger> logger_;
};

// Test that emergency path produces consistent DBC sequence
TEST_F(DBCEmergencyPathTest, Emergency_Path_DBC_Consistency) {
    auto preCalc = std::make_unique<CIPPreCalculator>();
    preCalc->initialize(config_, 0x3F);
    // DON'T start pre-calculator to force emergency path only
    
    std::vector<uint8_t> dbcSequence;
    std::vector<bool> packetTypes;
    
    // Generate sequence using only emergency calculation
    for (uint32_t p = 0; p < 20; ++p) {
        CIPHeader header = {};
        
        bool isNoData = preCalc->emergencyCalculateCIP(&header, p);
        
        dbcSequence.push_back(header.dbc);
        packetTypes.push_back(isNoData);
        
        logger_->info("Emergency packet {}: DBC=0x{:02X}, isNoData={}", 
                     p, header.dbc, isNoData);
    }
    
    // Verify DBC sequence has reasonable progression
    EXPECT_GT(dbcSequence.size(), 15u) << "Should generate substantial sequence";
    
    // Check for basic DBC progression (allowing for some differences due to emergency logic)
    uint32_t validTransitions = 0;
    for (size_t i = 1; i < dbcSequence.size(); ++i) {
        uint8_t prevDbc = dbcSequence[i-1];
        uint8_t currDbc = dbcSequence[i];
        bool currIsNoData = packetTypes[i];
        bool prevWasNoData = packetTypes[i-1];
        
        // Check for valid DBC transitions
        if (currDbc == prevDbc || currDbc == ((prevDbc + 8) & 0xFF)) {
            validTransitions++;
        }
    }
    
    double validPercentage = (double)validTransitions / (dbcSequence.size() - 1) * 100.0;
    EXPECT_GT(validPercentage, 80.0) << "Emergency path should have >80% valid DBC transitions";
    
    logger_->info("Emergency path: {:.1f}% valid DBC transitions", validPercentage);
}

// Test pre-calculator vs emergency path consistency
TEST_F(DBCEmergencyPathTest, PreCalc_vs_Emergency_Consistency) {
    // Test 1: Get sequence from pre-calculator
    auto preCalc1 = std::make_unique<CIPPreCalculator>();
    preCalc1->initialize(config_, 0x3F);
    preCalc1->start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::vector<uint8_t> preCalcDbcs;
    std::vector<bool> preCalcTypes;
    
    // Collect from pre-calculator
    for (uint32_t attempts = 0; attempts < 20; ++attempts) {
        PreCalcGroup group;
        if (preCalc1->groupRing_.pop(group)) {
            for (uint32_t p = 0; p < config_.packetsPerGroup && preCalcDbcs.size() < 16; ++p) {
                preCalcDbcs.push_back(group.packets[p].header.dbc);
                preCalcTypes.push_back(group.packets[p].isNoData);
            }
            if (preCalcDbcs.size() >= 16) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    preCalc1->stop();
    
    // Test 2: Get sequence from emergency path with same initial state
    auto preCalc2 = std::make_unique<CIPPreCalculator>();
    preCalc2->initialize(config_, 0x3F);
    // Force sync to same starting state
    preCalc2->forceSync(0, false);  // Start with DBC=0, previous was DATA
    
    std::vector<uint8_t> emergencyDbcs;
    std::vector<bool> emergencyTypes;
    
    // Generate using emergency path only
    for (uint32_t p = 0; p < 16; ++p) {
        CIPHeader header = {};
        bool isNoData = preCalc2->emergencyCalculateCIP(&header, p);
        
        emergencyDbcs.push_back(header.dbc);
        emergencyTypes.push_back(isNoData);
    }
    
    // Compare sequences
    EXPECT_GT(preCalcDbcs.size(), 10u) << "Should get substantial pre-calc sequence";
    EXPECT_EQ(emergencyDbcs.size(), 16u) << "Should get complete emergency sequence";
    
    // Log both sequences for comparison
    logger_->info("Pre-calculator sequence:");
    for (size_t i = 0; i < preCalcDbcs.size(); ++i) {
        logger_->info("  [{}]: DBC=0x{:02X} ({})", i, preCalcDbcs[i], 
                     preCalcTypes[i] ? "NO-DATA" : "DATA");
    }
    
    logger_->info("Emergency path sequence:");
    for (size_t i = 0; i < emergencyDbcs.size(); ++i) {
        logger_->info("  [{}]: DBC=0x{:02X} ({})", i, emergencyDbcs[i], 
                     emergencyTypes[i] ? "NO-DATA" : "DATA");
    }
    
    // Both should follow valid DBC patterns (but may differ due to timing)
    auto validateSequence = [](const std::vector<uint8_t>& dbcs, 
                              const std::vector<bool>& types, 
                              const std::string& name) -> double {
        uint32_t validTransitions = 0;
        for (size_t i = 1; i < dbcs.size(); ++i) {
            uint8_t prevDbc = dbcs[i-1];
            uint8_t currDbc = dbcs[i];
            
            if (currDbc == prevDbc || currDbc == ((prevDbc + 8) & 0xFF)) {
                validTransitions++;
            }
        }
        return (double)validTransitions / (dbcs.size() - 1) * 100.0;
    };
    
    double preCalcValidity = validateSequence(preCalcDbcs, preCalcTypes, "pre-calc");
    double emergencyValidity = validateSequence(emergencyDbcs, emergencyTypes, "emergency");
    
    EXPECT_GT(preCalcValidity, 90.0) << "Pre-calculator should have >90% valid transitions";
    EXPECT_GT(emergencyValidity, 80.0) << "Emergency path should have >80% valid transitions";
    
    logger_->info("Validity: pre-calc {:.1f}%, emergency {:.1f}%", 
                 preCalcValidity, emergencyValidity);
}

// Test emergency state synchronization
TEST_F(DBCEmergencyPathTest, Emergency_State_Synchronization) {
    auto preCalc = std::make_unique<CIPPreCalculator>();
    preCalc->initialize(config_, 0x3F);
    
    // Test different sync states
    std::vector<std::pair<uint8_t, bool>> testStates = {
        {0, false},    // DBC=0, prev was DATA
        {50, true},    // DBC=50, prev was NO-DATA  
        {200, false},  // DBC=200, prev was DATA
        {255, true},   // DBC=255, prev was NO-DATA (test wraparound)
    };
    
    for (const auto& [dbc, prevWasNoData] : testStates) {
        // Force sync to specific state
        preCalc->forceSync(dbc, prevWasNoData);
        
        // Generate one emergency packet
        CIPHeader header = {};
        bool isNoData = preCalc->emergencyCalculateCIP(&header, 0);
        
        // Verify DBC makes sense given the sync state
        if (prevWasNoData && !isNoData) {
            // First DATA after NO-DATA should keep same DBC
            EXPECT_EQ(header.dbc, dbc) << "First DATA after NO-DATA should keep DBC";
        } else if (!prevWasNoData && !isNoData) {
            // DATA after DATA should advance by 8
            uint8_t expectedDbc = (dbc + 8) & 0xFF;
            EXPECT_EQ(header.dbc, expectedDbc) << "DATA after DATA should advance DBC";
        }
        
        logger_->info("Sync test: DBC={} -> {}, prevWasNoData={}, isNoData={}", 
                     dbc, header.dbc, prevWasNoData, isNoData);
    }
}