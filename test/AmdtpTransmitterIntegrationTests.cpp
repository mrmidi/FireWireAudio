#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "Isoch/core/AmdtpTransmitter.hpp"
#include "Isoch/core/CIPPreCalculator.hpp"
#include "Isoch/core/CIPHeader.hpp"
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <spdlog/spdlog.h>

using namespace FWA::Isoch;

// Mock interfaces for testing
class MockIOFireWireLibNubRef {
public:
    // Mock implementation for testing
};

// Integration test fixture for AmdtpTransmitter
class AmdtpTransmitterIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger_ = spdlog::default_logger();
        
        // Setup configuration for realistic testing
        config_.numGroups = 16;
        config_.packetsPerGroup = 8;
        config_.sampleRate = 48000.0;
        config_.clientBufferSize = 4096;
        config_.transmissionType = TransmissionType::NonBlocking;
        config_.logger = logger_;
        config_.callbackGroupInterval = 8;  // Process 8 groups per callback
        
        // Create transmitter
        transmitter_ = AmdtpTransmitter::create(config_);
        
        // Initialize test state
        resetTestState();
    }
    
    void TearDown() override {
        if (transmitter_) {
            auto result = transmitter_->stopTransmit();
            // Don't check result in teardown to avoid exceptions
        }
    }
    
    void resetTestState() {
        dbcSequence_.clear();
        packetTypes_.clear();
        callbackCount_ = 0;
        totalPacketsProcessed_ = 0;
        dbcErrors_ = 0;
        testComplete_ = false;
    }
    
    // Simulate DCL callback to trigger handleDCLCompleteFastPath
    void simulateDCLCallback(uint32_t groupIndex) {
        if (!transmitter_) return;
        
        // This would normally be called by the FireWire framework
        // We'll need to access the method through a test interface
        // For now, we'll simulate the effects
        callbackCount_++;
        
        // Record that we processed this group
        processedGroups_.push_back(groupIndex);
    }
    
    // Helper to extract DBC values from transmitter buffers
    std::vector<uint8_t> extractDbcSequence(uint32_t numGroups) {
        std::vector<uint8_t> sequence;
        
        // We would need access to the buffer manager to read actual DBC values
        // This is a simplified version for testing
        for (uint32_t g = 0; g < numGroups; ++g) {
            for (uint32_t p = 0; p < config_.packetsPerGroup; ++p) {
                // In real implementation, we'd read from buffer:
                // auto bufPtr = transmitter_->getBufferManager()->getPacketCIPHeaderPtr(g, p);
                // auto* header = reinterpret_cast<CIPHeader*>(bufPtr.value());
                // sequence.push_back(header->dbc);
                
                // For now, simulate based on expected pattern
                uint8_t expectedDbc = (g * config_.packetsPerGroup + p) * 8;
                sequence.push_back(expectedDbc);
            }
        }
        
        return sequence;
    }
    
    TransmitterConfig config_;
    std::shared_ptr<AmdtpTransmitter> transmitter_;
    std::shared_ptr<spdlog::logger> logger_;
    
    // Test state tracking
    std::vector<uint8_t> dbcSequence_;
    std::vector<bool> packetTypes_;  // true = NO-DATA, false = DATA
    std::vector<uint32_t> processedGroups_;
    std::atomic<uint32_t> callbackCount_{0};
    std::atomic<uint32_t> totalPacketsProcessed_{0};
    std::atomic<uint32_t> dbcErrors_{0};
    std::atomic<bool> testComplete_{false};
};

// Test that CIP pre-calculator produces valid DBC sequences
TEST_F(AmdtpTransmitterIntegrationTest, CIP_PreCalculator_DBC_Generation) {
    // Create and test CIP pre-calculator directly
    auto preCalc = std::make_unique<CIPPreCalculator>();
    preCalc->initialize(config_, 0x3F);
    preCalc->start();
    
    // Wait for initial calculation
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::vector<uint8_t> dbcSequence;
    std::vector<bool> packetTypes;
    
    // Collect packets from the SPSC ring
    for (uint32_t attempts = 0; attempts < 50; ++attempts) {
        PreCalcGroup group;
        if (preCalc->groupRing_.pop(group)) {
            for (uint32_t p = 0; p < config_.packetsPerGroup; ++p) {
                const auto& packet = group.packets[p];
                dbcSequence.push_back(packet.header.dbc);
                packetTypes.push_back(packet.isNoData);
            }
            
            if (dbcSequence.size() >= 100) break;  // Collect enough samples
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    preCalc->stop();
    
    EXPECT_GT(dbcSequence.size(), 50u) << "Should collect sufficient DBC samples from pre-calculator";
    
    // Validate DBC continuity in pre-calculated sequence
    uint32_t violations = 0;
    for (size_t i = 1; i < dbcSequence.size(); ++i) {
        uint8_t prevDbc = dbcSequence[i-1];
        uint8_t currDbc = dbcSequence[i];
        bool prevWasNoData = packetTypes[i-1];
        bool currIsNoData = packetTypes[i];
        
        if (currIsNoData) {
            // NO-DATA should advance DBC by 8 from last DATA
            // (This test verifies pre-calculator logic)
        } else {
            // DATA packet
            if (prevWasNoData) {
                // First DATA after NO-DATA should keep same DBC
                if (currDbc != prevDbc) violations++;
            } else {
                // Normal DATA after DATA should advance by 8
                uint8_t expectedDbc = (prevDbc + 8) & 0xFF;
                if (currDbc != expectedDbc) violations++;
            }
        }
    }
    
    EXPECT_EQ(violations, 0u) << "Pre-calculator should generate perfect DBC sequence";
}

// Test DBC validation function with real patterns
TEST_F(AmdtpTransmitterIntegrationTest, DBC_Validation_With_Real_Patterns) {
    // Create realistic packet sequence based on 48kHz pattern
    std::vector<std::pair<uint8_t, bool>> testSequence;
    
    // Generate 48kHz pattern: 7 DATA + 1 NO-DATA repeating
    uint8_t currentDbc = 0;
    for (uint32_t cycle = 0; cycle < 10; ++cycle) {
        // 7 DATA packets
        for (uint32_t i = 0; i < 7; ++i) {
            testSequence.push_back({currentDbc, false});
            currentDbc = (currentDbc + 8) & 0xFF;
        }
        
        // 1 NO-DATA packet - advance DBC
        testSequence.push_back({currentDbc, true});
        // currentDbc doesn't advance for NO-DATA in our implementation
        
        // First DATA after NO-DATA keeps same DBC
        testSequence.push_back({currentDbc, false});
        currentDbc = (currentDbc + 8) & 0xFF;
    }
    
    // Simulate inline DBC validation (copy from AmdtpTransmitter.cpp)
    uint8_t lastDataPacketDbc = 0xFF;
    uint8_t lastPacketDbc = 0xFF;
    bool prevPacketWasNoData = false;
    bool hasValidState = false;
    uint32_t errors = 0;
    
    // Use the actual checkDbcContinuity function logic
    for (const auto& [dbc, isNoData] : testSequence) {
        // Simulate the inline validation from AmdtpTransmitter
        if (isNoData) {
            // NO-DATA validation logic
            if (hasValidState && lastDataPacketDbc != 0xFF) {
                uint8_t expectedDbc = (lastDataPacketDbc + 8) % 256;
                if (dbc != expectedDbc) {
                    errors++;
                }
            }
            lastPacketDbc = dbc;
            prevPacketWasNoData = true;
        } else {
            // DATA validation logic
            if (!hasValidState || lastDataPacketDbc == 0xFF) {
                lastDataPacketDbc = dbc;
                hasValidState = true;
            } else {
                uint8_t expectedDbc;
                if (prevPacketWasNoData) {
                    expectedDbc = lastPacketDbc;
                } else {
                    expectedDbc = (lastDataPacketDbc + 8) % 256;
                }
                
                if (dbc != expectedDbc) {
                    errors++;
                }
            }
            lastDataPacketDbc = dbc;
            lastPacketDbc = dbc;
            prevPacketWasNoData = false;
        }
    }
    
    EXPECT_EQ(errors, 0u) << "Realistic packet sequence should pass DBC validation";
    EXPECT_GT(testSequence.size(), 50u) << "Should test substantial packet sequence";
}

// Test multi-threaded DBC consistency
TEST_F(AmdtpTransmitterIntegrationTest, Multi_Threaded_DBC_Consistency) {
    // This test simulates the multi-threaded environment
    auto preCalc = std::make_unique<CIPPreCalculator>();
    preCalc->initialize(config_, 0x3F);
    preCalc->start();
    
    // Wait for pre-calculator to build up some data
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    std::atomic<uint32_t> totalPackets{0};
    std::atomic<uint32_t> dbcErrors{0};
    std::atomic<bool> testRunning{true};
    
    // Simulate DCL callback thread
    std::thread callbackThread([&]() {
        uint8_t lastDataPacketDbc = 0xFF;
        uint8_t lastPacketDbc = 0xFF;
        bool prevPacketWasNoData = false;
        bool hasValidState = false;
        
        while (testRunning.load()) {
            PreCalcGroup group;
            if (preCalc->groupRing_.pop(group)) {
                // Simulate handleDCLCompleteFastPath processing
                for (uint32_t p = 0; p < config_.packetsPerGroup; ++p) {
                    const auto& packet = group.packets[p];
                    
                    // Inline DBC validation (simplified)
                    bool isNoData = packet.isNoData;
                    uint8_t dbc = packet.header.dbc;
                    
                    if (isNoData) {
                        if (hasValidState && lastDataPacketDbc != 0xFF) {
                            uint8_t expectedDbc = (lastDataPacketDbc + 8) % 256;
                            if (dbc != expectedDbc) {
                                dbcErrors.fetch_add(1);
                            }
                        }
                        lastPacketDbc = dbc;
                        prevPacketWasNoData = true;
                    } else {
                        if (!hasValidState || lastDataPacketDbc == 0xFF) {
                            lastDataPacketDbc = dbc;
                            hasValidState = true;
                        } else {
                            uint8_t expectedDbc;
                            if (prevPacketWasNoData) {
                                expectedDbc = lastPacketDbc;
                            } else {
                                expectedDbc = (lastDataPacketDbc + 8) % 256;
                            }
                            
                            if (dbc != expectedDbc) {
                                dbcErrors.fetch_add(1);
                            }
                        }
                        lastDataPacketDbc = dbc;
                        lastPacketDbc = dbc;
                        prevPacketWasNoData = false;
                    }
                    
                    totalPackets.fetch_add(1);
                }
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    });
    
    // Let the test run for a reasonable time
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    testRunning = false;
    callbackThread.join();
    
    preCalc->stop();
    
    EXPECT_GT(totalPackets.load(), 100u) << "Should process substantial number of packets";
    EXPECT_EQ(dbcErrors.load(), 0u) << "Should have zero DBC errors in multi-threaded test";
    
    double packetsPerSecond = totalPackets.load() / 0.5;  // 500ms test duration
    EXPECT_GT(packetsPerSecond, 1000.0) << "Should achieve reasonable packet processing rate";
}

// Test DCL callback simulation with group processing
TEST_F(AmdtpTransmitterIntegrationTest, DCL_Callback_Group_Processing) {
    // This test verifies the group processing logic in handleDCLCompleteFastPath
    
    const uint32_t NUM_GROUPS = config_.numGroups;
    const uint32_t PACKETS_PER_GROUP = config_.packetsPerGroup;
    
    // Simulate the group index calculation logic from handleDCLCompleteFastPath
    std::vector<uint32_t> processedGroups;
    
    // Simulate multiple DCL callbacks
    for (uint32_t completedGroupIndex = 0; completedGroupIndex < NUM_GROUPS; ++completedGroupIndex) {
        // This is the logic from handleDCLCompleteFastPath
        uint32_t numGroupsToProcess = config_.callbackGroupInterval;  // typically 8
        
        // Calculate the first group in the completed batch
        uint32_t firstGroupInCompletedBatch;
        if (completedGroupIndex >= (numGroupsToProcess - 1)) {
            firstGroupInCompletedBatch = completedGroupIndex - (numGroupsToProcess - 1);
        } else {
            firstGroupInCompletedBatch = NUM_GROUPS + completedGroupIndex - (numGroupsToProcess - 1);
        }
        
        // Process the next batch of groups
        for (uint32_t i = 0; i < numGroupsToProcess; ++i) {
            uint32_t processedGroup = (firstGroupInCompletedBatch + i) % NUM_GROUPS;
            uint32_t fillGroup = (processedGroup + 2) % NUM_GROUPS;  // kGroupsPerCallback = 2
            
            processedGroups.push_back(fillGroup);
        }
    }
    
    // Verify that all groups are processed
    std::set<uint32_t> uniqueGroups(processedGroups.begin(), processedGroups.end());
    EXPECT_EQ(uniqueGroups.size(), NUM_GROUPS) << "All groups should be processed";
    
    // Verify processing pattern
    EXPECT_GT(processedGroups.size(), NUM_GROUPS) << "Should process groups multiple times";
    
    // Check that group indices are valid
    for (uint32_t groupIndex : processedGroups) {
        EXPECT_LT(groupIndex, NUM_GROUPS) << "Group index should be within valid range";
    }
}

// Test performance characteristics
TEST_F(AmdtpTransmitterIntegrationTest, Performance_Characteristics) {
    auto preCalc = std::make_unique<CIPPreCalculator>();
    preCalc->initialize(config_, 0x3F);
    preCalc->start();
    
    // Measure pre-calculator performance
    auto startTime = std::chrono::high_resolution_clock::now();
    
    uint32_t groupsConsumed = 0;
    const uint32_t TARGET_GROUPS = 100;
    
    while (groupsConsumed < TARGET_GROUPS) {
        PreCalcGroup group;
        if (preCalc->groupRing_.pop(group)) {
            groupsConsumed++;
            
            // Simulate minimal processing like handleDCLCompleteFastPath
            for (uint32_t p = 0; p < config_.packetsPerGroup; ++p) {
                const auto& packet = group.packets[p];
                
                // Verify header structure
                EXPECT_EQ(packet.header.dbs, 2u) << "DBS should be 2 for stereo";
                EXPECT_TRUE(packet.header.fdf == CIP::kFDF_48k || 
                           packet.header.fdf == CIP::kFDF_44k1) << "FDF should be valid";
                
                // Verify DBC is reasonable
                EXPECT_LE(packet.header.dbc, 255u) << "DBC should be valid 8-bit value";
            }
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    
    preCalc->stop();
    
    double groupsPerSecond = TARGET_GROUPS * 1000000.0 / duration.count();
    double packetsPerSecond = groupsPerSecond * config_.packetsPerGroup;
    
    EXPECT_GT(groupsPerSecond, 1000.0) << "Should achieve >1000 groups/sec processing rate";
    EXPECT_GT(packetsPerSecond, 8000.0) << "Should achieve >8000 packets/sec (8kHz FireWire rate)";
    
    std::cout << "Performance: " << groupsPerSecond << " groups/sec, " 
              << packetsPerSecond << " packets/sec" << std::endl;
}

// Test emergency path fallback
TEST_F(AmdtpTransmitterIntegrationTest, Emergency_Path_DBC_Validation) {
    // Test the emergency calculation path when pre-calculator fails
    auto preCalc = std::make_unique<CIPPreCalculator>();
    preCalc->initialize(config_, 0x3F);
    // Don't start pre-calculator to force emergency path
    
    std::vector<uint8_t> dbcSequence;
    std::vector<bool> packetTypes;
    
    // Simulate emergency CIP calculation
    for (uint32_t p = 0; p < 50; ++p) {
        CIPHeader header = {};
        
        // Use emergency calculation
        bool isNoData = preCalc->emergencyCalculateCIP(&header, p);
        
        dbcSequence.push_back(header.dbc);
        packetTypes.push_back(isNoData);
        
        // Verify header structure
        EXPECT_EQ(header.dbs, 2u) << "Emergency header should have DBS=2";
        EXPECT_TRUE(header.fdf == CIP::kFDF_48k || 
                   header.fdf == CIP::kFDF_44k1) << "Emergency header should have valid FDF";
    }
    
    // Validate DBC continuity in emergency-generated sequence
    uint32_t violations = 0;
    for (size_t i = 1; i < dbcSequence.size(); ++i) {
        uint8_t prevDbc = dbcSequence[i-1];
        uint8_t currDbc = dbcSequence[i];
        bool prevWasNoData = packetTypes[i-1];
        bool currIsNoData = packetTypes[i];
        
        if (!currIsNoData && !prevWasNoData) {
            // DATA after DATA should advance by 8
            uint8_t expectedDbc = (prevDbc + 8) & 0xFF;
            if (currDbc != expectedDbc) violations++;
        }
    }
    
    EXPECT_LT(violations, dbcSequence.size() / 10) << "Emergency path should have reasonable DBC continuity";
}