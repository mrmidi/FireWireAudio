#include <gtest/gtest.h>
#include "Isoch/core/CIPPreCalculator.hpp"
#include "Isoch/core/CIPHeader.hpp"
#include <vector>
#include <memory>
#include <spdlog/spdlog.h>
#include <iostream>
#include <iomanip>

using namespace FWA::Isoch;

// Copy the DBC continuity checker from AmdtpTransmitter.cpp for testing
namespace {
    constexpr uint16_t DBC_WRAP = 256;              // DBC is 8-bit, wraps at 256
    constexpr uint8_t NO_DATA_INCREMENT = 8;       // No-data packets always increment by 8
    constexpr uint8_t SYT_INTERVAL = 8;            // Normal data packet increment
    
    // Inline DBC continuity checker - optimized for fast path
    inline bool checkDbcContinuity(uint8_t currentDbc, bool isNoData,
                                   uint8_t& lastDataPacketDbc, uint8_t& lastPacketDbc,
                                   bool& prevPacketWasNoData, bool& hasValidState,
                                   const std::shared_ptr<spdlog::logger>& logger) {
        
        if (isNoData) {
            // No-data packets: DBC should be (last_data_packet_dbc + 8) mod 256
            if (hasValidState && lastDataPacketDbc != 0xFF) {
                uint8_t expectedDbc = (lastDataPacketDbc + NO_DATA_INCREMENT) % DBC_WRAP;
                if (currentDbc != expectedDbc) {
                    logger->critical("DBC CONTINUITY ERROR: No-data packet DBC=0x{:02X}, expected=0x{:02X} "
                                   "(last_data=0x{:02X})", currentDbc, expectedDbc, lastDataPacketDbc);
                    return false;
                }
            }
            
            lastPacketDbc = currentDbc;
            prevPacketWasNoData = true;
            
        } else {
            // Data packet
            if (!hasValidState || lastDataPacketDbc == 0xFF) {
                // First data packet - just record it
                lastDataPacketDbc = currentDbc;
                hasValidState = true;
            } else {
                // Determine expected DBC based on previous packet type
                uint8_t expectedDbc;
                if (prevPacketWasNoData) {
                    // After no-data: DBC should stay same as no-data packet
                    expectedDbc = lastPacketDbc;
                } else {
                    // Normal data-to-data: increment by SYT_INTERVAL
                    expectedDbc = (lastDataPacketDbc + SYT_INTERVAL) % DBC_WRAP;
                }
                
                if (currentDbc != expectedDbc) {
                    logger->critical("DBC CONTINUITY ERROR: Data packet DBC=0x{:02X}, expected=0x{:02X} "
                                   "(prev_no_data={}, last_data=0x{:02X}, last_pkt=0x{:02X})", 
                                   currentDbc, expectedDbc, prevPacketWasNoData, 
                                   lastDataPacketDbc, lastPacketDbc);
                    return false;
                }
            }
            
            // Update tracking variables for next iteration
            lastDataPacketDbc = currentDbc;
            lastPacketDbc = currentDbc;
            prevPacketWasNoData = false;
        }
        
        return true;
    }
}

// Simple test to observe and validate DBC patterns
class DBCPatternTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.numGroups = 16;
        config_.packetsPerGroup = 8;
        config_.sampleRate = 48000.0;
        config_.clientBufferSize = 4096;
        config_.transmissionType = TransmissionType::NonBlocking;
        config_.logger = spdlog::default_logger();
        
        nodeID_ = 0x3F;
        preCalc_ = std::make_unique<CIPPreCalculator>();
    }
    
    void TearDown() override {
        if (preCalc_) preCalc_->stop();
    }
    
    TransmitterConfig config_;
    uint16_t nodeID_;
    std::unique_ptr<CIPPreCalculator> preCalc_;
};

// Test that simply observes the DBC pattern and verifies basic properties
TEST_F(DBCPatternTest, ObserveDBC_Pattern) {
    preCalc_->initialize(config_, nodeID_);
    preCalc_->start();
    
    // Wait for calculation
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::vector<uint8_t> dbcValues;
    std::vector<bool> isNoDataFlags;
    std::vector<std::string> packetTypes;
    
    // Collect first 50 packets to see the pattern
    const size_t TARGET_PACKETS = 50;
    
    while (dbcValues.size() < TARGET_PACKETS) {
        PreCalcGroup group;
        if (preCalc_->groupRing_.pop(group)) {
            for (uint32_t pktIdx = 0; pktIdx < config_.packetsPerGroup && dbcValues.size() < TARGET_PACKETS; ++pktIdx) {
                const auto& packet = group.packets[pktIdx];
                dbcValues.push_back(packet.header.dbc);
                isNoDataFlags.push_back(packet.isNoData);
                packetTypes.push_back(packet.isNoData ? "NO-DATA" : "DATA");
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    
    // Print the actual pattern for analysis
    std::cout << "\nObserved DBC Pattern (first " << dbcValues.size() << " packets):\n";
    std::cout << "Idx  DBC  Type     Notes\n";
    std::cout << "---  ---  -------  -----\n";
    
    for (size_t i = 0; i < dbcValues.size(); ++i) {
        std::cout << std::setw(3) << i 
                  << "  " << std::setw(3) << (int)dbcValues[i]
                  << "  " << std::setw(7) << packetTypes[i];
        
        if (i > 0) {
            uint8_t prevDbc = dbcValues[i-1];
            uint8_t currDbc = dbcValues[i];
            bool prevWasNoData = isNoDataFlags[i-1];
            
            if (currDbc == prevDbc) {
                std::cout << "  (same DBC)";
            } else if (currDbc == ((prevDbc + 8) & 0xFF)) {
                std::cout << "  (+8)";
            } else {
                std::cout << "  (unexpected change: " << (int)prevDbc << " -> " << (int)currDbc << ")";
            }
        }
        std::cout << "\n";
    }
    
    // Basic sanity checks
    EXPECT_EQ(dbcValues.size(), TARGET_PACKETS) << "Should collect expected number of packets";
    
    // Check that we have both data and no-data packets
    bool hasData = false, hasNoData = false;
    for (bool isNoData : isNoDataFlags) {
        if (isNoData) hasNoData = true;
        else hasData = true;
    }
    EXPECT_TRUE(hasData) << "Should have DATA packets";
    EXPECT_TRUE(hasNoData) << "Should have NO-DATA packets";
    
    // Check DBC values are in reasonable range (0-255)
    for (uint8_t dbc : dbcValues) {
        EXPECT_LE(dbc, 255u) << "DBC should be valid 8-bit value";
    }
    
    // Check that DBC only changes by 0 or +8 (modulo 256)
    for (size_t i = 1; i < dbcValues.size(); ++i) {
        uint8_t prevDbc = dbcValues[i-1];
        uint8_t currDbc = dbcValues[i];
        int diff = ((int)currDbc - (int)prevDbc + 256) % 256;
        
        EXPECT_TRUE(diff == 0 || diff == 8) 
            << "DBC should only change by 0 or +8 (packet " << i 
            << ": " << (int)prevDbc << " -> " << (int)currDbc << ", diff=" << diff << ")";
    }
}

// Test Apple DBC rule compliance specifically
TEST_F(DBCPatternTest, Apple_DBC_Rule_Compliance) {
    preCalc_->initialize(config_, nodeID_);
    preCalc_->start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::vector<uint8_t> dbcValues;
    std::vector<bool> isNoDataFlags;
    
    // Collect enough packets to see several NO-DATA transitions
    const size_t TARGET_PACKETS = 100;
    
    while (dbcValues.size() < TARGET_PACKETS) {
        PreCalcGroup group;
        if (preCalc_->groupRing_.pop(group)) {
            for (uint32_t pktIdx = 0; pktIdx < config_.packetsPerGroup && dbcValues.size() < TARGET_PACKETS; ++pktIdx) {
                const auto& packet = group.packets[pktIdx];
                dbcValues.push_back(packet.header.dbc);
                isNoDataFlags.push_back(packet.isNoData);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    
    // Count Apple rule violations
    uint32_t violations = 0;
    uint32_t noDataToDataTransitions = 0;
    uint32_t dataToDataTransitions = 0;
    
    for (size_t i = 1; i < dbcValues.size(); ++i) {
        uint8_t prevDbc = dbcValues[i-1];
        uint8_t currDbc = dbcValues[i];
        bool prevWasNoData = isNoDataFlags[i-1];
        bool currIsNoData = isNoDataFlags[i];
        
        if (currIsNoData) {
            // Rule: NO-DATA packets should have same DBC as previous packet
            if (currDbc != prevDbc) {
                violations++;
                std::cout << "Violation " << violations << ": NO-DATA packet " << i 
                          << " has DBC " << (int)currDbc << ", expected " << (int)prevDbc << "\n";
            }
        } else {
            // DATA packet
            if (prevWasNoData) {
                // Rule: First DATA after NO-DATA should keep same DBC
                noDataToDataTransitions++;
                if (currDbc != prevDbc) {
                    violations++;
                    std::cout << "Violation " << violations << ": First DATA after NO-DATA at packet " << i 
                              << " has DBC " << (int)currDbc << ", expected " << (int)prevDbc << "\n";
                }
            } else {
                // Rule: DATA after DATA should advance DBC by 8
                dataToDataTransitions++;
                uint8_t expectedDbc = (prevDbc + 8) & 0xFF;
                if (currDbc != expectedDbc) {
                    violations++;
                    std::cout << "Violation " << violations << ": DATA after DATA at packet " << i 
                              << " has DBC " << (int)currDbc << ", expected " << (int)expectedDbc << "\n";
                }
            }
        }
    }
    
    std::cout << "\nApple DBC Rule Analysis:\n";
    std::cout << "Total packets analyzed: " << (dbcValues.size() - 1) << "\n";
    std::cout << "NO-DATA to DATA transitions: " << noDataToDataTransitions << "\n";
    std::cout << "DATA to DATA transitions: " << dataToDataTransitions << "\n";
    std::cout << "Rule violations: " << violations << "\n";
    
    if (violations > 0) {
        double violationRate = 100.0 * violations / (dbcValues.size() - 1);
        std::cout << "Violation rate: " << std::fixed << std::setprecision(2) << violationRate << "%\n";
    }
    
    EXPECT_EQ(violations, 0u) << "Should have zero Apple DBC rule violations";
    EXPECT_GT(noDataToDataTransitions, 0u) << "Should have some NO-DATA to DATA transitions";
    EXPECT_GT(dataToDataTransitions, 0u) << "Should have some DATA to DATA transitions";
}

// Test DBC continuity across wraparound
TEST_F(DBCPatternTest, DBC_Wraparound_Test) {
    preCalc_->initialize(config_, nodeID_);
    
    // Start near wraparound boundary
    preCalc_->forceSync(248, false); // Start at DBC=248, previous was DATA
    preCalc_->start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::vector<uint8_t> dbcValues;
    std::vector<bool> isNoDataFlags;
    
    // Collect packets to see wraparound
    for (uint32_t attempts = 0; attempts < 50; ++attempts) {
        PreCalcGroup group;
        if (preCalc_->groupRing_.pop(group)) {
            for (uint32_t pktIdx = 0; pktIdx < config_.packetsPerGroup; ++pktIdx) {
                const auto& packet = group.packets[pktIdx];
                dbcValues.push_back(packet.header.dbc);
                isNoDataFlags.push_back(packet.isNoData);
                
                // Stop when we see wraparound (from high value to low value)
                if (dbcValues.size() > 1) {
                    uint8_t prev = dbcValues[dbcValues.size() - 2];
                    uint8_t curr = dbcValues[dbcValues.size() - 1];
                    if (prev > 240 && curr < 20) {
                        std::cout << "\nDBC Wraparound detected: " << (int)prev << " -> " << (int)curr << "\n";
                        break;
                    }
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    
    // Print the sequence around wraparound
    std::cout << "DBC sequence around wraparound:\n";
    for (size_t i = 0; i < dbcValues.size(); ++i) {
        std::cout << "  " << i << ": DBC=" << (int)dbcValues[i] 
                  << " (" << (isNoDataFlags[i] ? "NO-DATA" : "DATA") << ")\n";
    }
    
    EXPECT_GT(dbcValues.size(), 5u) << "Should collect some packets to observe wraparound";
    
    // Verify wraparound follows Apple rule
    for (size_t i = 1; i < dbcValues.size(); ++i) {
        uint8_t prevDbc = dbcValues[i-1];
        uint8_t currDbc = dbcValues[i];
        bool currIsNoData = isNoDataFlags[i];
        bool prevWasNoData = (i > 0) ? isNoDataFlags[i-1] : false;
        
        if (currIsNoData) {
            EXPECT_EQ(currDbc, prevDbc) << "NO-DATA should keep same DBC at wraparound";
        } else {
            if (prevWasNoData) {
                EXPECT_EQ(currDbc, prevDbc) << "First DATA after NO-DATA should keep DBC at wraparound";
            } else {
                uint8_t expectedDbc = (prevDbc + 8) & 0xFF;
                EXPECT_EQ(currDbc, expectedDbc) << "DATA after DATA should wrap correctly";
            }
        }
    }
}

// Test fixture for DBC continuity checker function
class DBCContinuityCheckerTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger_ = spdlog::default_logger();
        resetState();
    }
    
    void resetState() {
        lastDataPacketDbc_ = 0xFF;
        lastPacketDbc_ = 0xFF;
        prevPacketWasNoData_ = false;
        hasValidState_ = false;
    }
    
    bool checkContinuity(uint8_t dbc, bool isNoData) {
        return checkDbcContinuity(dbc, isNoData, lastDataPacketDbc_, lastPacketDbc_,
                                  prevPacketWasNoData_, hasValidState_, logger_);
    }
    
    std::shared_ptr<spdlog::logger> logger_;
    uint8_t lastDataPacketDbc_;
    uint8_t lastPacketDbc_;
    bool prevPacketWasNoData_;
    bool hasValidState_;
};

// Test perfect Apple DBC sequence validation
TEST_F(DBCContinuityCheckerTest, Perfect_Apple_Sequence) {
    // Test the ideal pattern: DATA(0) -> DATA(8) -> DATA(16) -> NO-DATA(24) -> DATA(24) -> DATA(32)
    std::vector<std::pair<uint8_t, bool>> sequence = {
        {0, false},   // DATA(0) - first packet
        {8, false},   // DATA(8) - advance by 8
        {16, false},  // DATA(16) - advance by 8
        {24, true},   // NO-DATA(24) - advance by 8 from last DATA
        {24, false},  // DATA(24) - same as NO-DATA (Apple rule)
        {32, false},  // DATA(32) - advance by 8
        {40, false},  // DATA(40) - advance by 8
        {48, true},   // NO-DATA(48) - advance by 8 from last DATA
        {48, false},  // DATA(48) - same as NO-DATA
        {56, false},  // DATA(56) - advance by 8
    };
    
    for (size_t i = 0; i < sequence.size(); ++i) {
        uint8_t dbc = sequence[i].first;
        bool isNoData = sequence[i].second;
        
        bool result = checkContinuity(dbc, isNoData);
        EXPECT_TRUE(result) << "Packet " << i << " should pass continuity check: "
                           << "DBC=0x" << std::hex << (int)dbc 
                           << " (" << (isNoData ? "NO-DATA" : "DATA") << ")";
    }
}

// Test first packet handling
TEST_F(DBCContinuityCheckerTest, First_Packet_Handling) {
    // First packet should always pass, regardless of DBC value
    EXPECT_TRUE(checkContinuity(42, false)) << "First DATA packet should always pass";
    
    resetState();
    EXPECT_TRUE(checkContinuity(100, false)) << "First DATA packet with any DBC should pass";
    
    resetState();
    EXPECT_TRUE(checkContinuity(0, false)) << "First DATA packet with DBC=0 should pass";
}

// Test NO-DATA packet validation
TEST_F(DBCContinuityCheckerTest, NoData_Packet_Validation) {
    // Start with DATA(16)
    EXPECT_TRUE(checkContinuity(16, false));
    
    // NO-DATA should be (16 + 8) = 24
    EXPECT_TRUE(checkContinuity(24, true)) << "NO-DATA should advance DBC by 8 from last DATA";
    
    // Wrong NO-DATA value should fail
    resetState();
    EXPECT_TRUE(checkContinuity(16, false)); // DATA(16)
    EXPECT_FALSE(checkContinuity(32, true)) << "NO-DATA with wrong DBC should fail";
}

// Test DATA after NO-DATA validation (Apple rule)
TEST_F(DBCContinuityCheckerTest, Data_After_NoData_Validation) {
    // Setup: DATA(32) -> NO-DATA(40)
    EXPECT_TRUE(checkContinuity(32, false)); // DATA(32)
    EXPECT_TRUE(checkContinuity(40, true));  // NO-DATA(40)
    
    // First DATA after NO-DATA should keep same DBC as NO-DATA
    EXPECT_TRUE(checkContinuity(40, false)) << "First DATA after NO-DATA should keep same DBC";
    
    // Wrong DBC should fail
    resetState();
    EXPECT_TRUE(checkContinuity(32, false)); // DATA(32)
    EXPECT_TRUE(checkContinuity(40, true));  // NO-DATA(40)
    EXPECT_FALSE(checkContinuity(48, false)) << "DATA after NO-DATA with wrong DBC should fail";
}

// Test normal DATA-to-DATA advancement
TEST_F(DBCContinuityCheckerTest, Data_To_Data_Advancement) {
    // Setup: DATA(64)
    EXPECT_TRUE(checkContinuity(64, false));
    
    // Next DATA should advance by 8
    EXPECT_TRUE(checkContinuity(72, false)) << "DATA after DATA should advance by 8";
    
    // Continue the sequence
    EXPECT_TRUE(checkContinuity(80, false)) << "Continued DATA advancement should work";
    
    // Wrong advancement should fail
    EXPECT_FALSE(checkContinuity(96, false)) << "DATA with wrong advancement should fail";
}

// Test DBC wraparound at 255->0
TEST_F(DBCContinuityCheckerTest, DBC_Wraparound) {
    // Setup: DATA(248)
    EXPECT_TRUE(checkContinuity(248, false));
    
    // Next DATA should wrap: (248 + 8) % 256 = 0
    EXPECT_TRUE(checkContinuity(0, false)) << "DATA should wrap correctly from 248 to 0";
    
    // Continue after wraparound
    EXPECT_TRUE(checkContinuity(8, false)) << "DATA should continue advancing after wraparound";
    
    // Test NO-DATA wraparound
    resetState();
    EXPECT_TRUE(checkContinuity(248, false)); // DATA(248)
    EXPECT_TRUE(checkContinuity(0, true));    // NO-DATA(0) - wrapped
    EXPECT_TRUE(checkContinuity(0, false));   // DATA(0) - same as NO-DATA
}

// Test complex mixed sequence
TEST_F(DBCContinuityCheckerTest, Complex_Mixed_Sequence) {
    // Real-world-like sequence with multiple NO-DATA transitions
    std::vector<std::pair<uint8_t, bool>> sequence = {
        {200, false}, // DATA(200) - start
        {208, false}, // DATA(208)
        {216, false}, // DATA(216)
        {224, false}, // DATA(224)
        {232, false}, // DATA(232)
        {240, false}, // DATA(240)
        {248, false}, // DATA(248)
        {0, true},    // NO-DATA(0) - wrapped
        {0, false},   // DATA(0) - same as NO-DATA
        {8, false},   // DATA(8)
        {16, false},  // DATA(16)
        {24, true},   // NO-DATA(24)
        {24, false},  // DATA(24) - same as NO-DATA
        {32, false},  // DATA(32)
    };
    
    for (size_t i = 0; i < sequence.size(); ++i) {
        uint8_t dbc = sequence[i].first;
        bool isNoData = sequence[i].second;
        
        bool result = checkContinuity(dbc, isNoData);
        EXPECT_TRUE(result) << "Complex sequence packet " << i << " failed: "
                           << "DBC=0x" << std::hex << (int)dbc 
                           << " (" << (isNoData ? "NO-DATA" : "DATA") << ")";
    }
}

// Test error detection - DBC discontinuity
TEST_F(DBCContinuityCheckerTest, Error_Detection_Discontinuity) {
    // Setup normal sequence
    EXPECT_TRUE(checkContinuity(16, false)); // DATA(16)
    EXPECT_TRUE(checkContinuity(24, false)); // DATA(24)
    
    // Introduce discontinuity - should be 32, but provide 40
    EXPECT_FALSE(checkContinuity(40, false)) << "DBC jump should be detected as error";
}

// Test error detection - wrong NO-DATA DBC
TEST_F(DBCContinuityCheckerTest, Error_Detection_Wrong_NoData) {
    // Setup: DATA(88)
    EXPECT_TRUE(checkContinuity(88, false));
    
    // NO-DATA should be 96, but provide 104
    EXPECT_FALSE(checkContinuity(104, true)) << "Wrong NO-DATA DBC should be detected";
}

// Test state management across multiple sequences
TEST_F(DBCContinuityCheckerTest, State_Management) {
    // First sequence
    EXPECT_TRUE(checkContinuity(100, false)); // DATA(100)
    EXPECT_TRUE(checkContinuity(108, true));  // NO-DATA(108)
    EXPECT_TRUE(checkContinuity(108, false)); // DATA(108)
    
    // Check internal state is correct for continuation
    EXPECT_TRUE(checkContinuity(116, false)) << "State should be maintained correctly";
    
    // Reset and start new sequence
    resetState();
    EXPECT_TRUE(checkContinuity(50, false)) << "After reset, any first DATA should work";
}

// Stress test with 1000 ideal packets
TEST_F(DBCContinuityCheckerTest, Stress_Test_1000_Ideal_Packets) {
    uint8_t currentDbc = 0;
    uint32_t packetCount = 0;
    const uint32_t TARGET_PACKETS = 1000;
    
    // Generate ideal pattern: 7 DATA + 1 NO-DATA repeating
    while (packetCount < TARGET_PACKETS) {
        // 7 DATA packets
        for (uint32_t i = 0; i < 7 && packetCount < TARGET_PACKETS; ++i) {
            bool result = checkContinuity(currentDbc, false);
            EXPECT_TRUE(result) << "DATA packet " << packetCount << " failed";
            currentDbc = (currentDbc + 8) & 0xFF;
            packetCount++;
        }
        
        // 1 NO-DATA packet
        if (packetCount < TARGET_PACKETS) {
            bool result = checkContinuity(currentDbc, true);
            EXPECT_TRUE(result) << "NO-DATA packet " << packetCount << " failed";
            // NO-DATA doesn't advance DBC in our current implementation
            packetCount++;
        }
        
        // First DATA after NO-DATA keeps same DBC
        if (packetCount < TARGET_PACKETS) {
            bool result = checkContinuity(currentDbc, false);
            EXPECT_TRUE(result) << "First DATA after NO-DATA packet " << packetCount << " failed";
            currentDbc = (currentDbc + 8) & 0xFF;
            packetCount++;
        }
    }
    
    EXPECT_EQ(packetCount, TARGET_PACKETS) << "Should process all packets successfully";
}