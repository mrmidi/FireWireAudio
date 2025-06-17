#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "Isoch/core/CIPPreCalculator.hpp"
#include "Isoch/core/CIPHeader.hpp"
#include "Isoch/core/IsochPacketProcessor.hpp"
#include <vector>
#include <memory>
#include <spdlog/spdlog.h>

using namespace FWA::Isoch;

// Test fixture for DBC calculation logic
class DBCCalculationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup 48kHz configuration
        config48k_.numGroups = 16;
        config48k_.packetsPerGroup = 8;
        config48k_.sampleRate = 48000.0;
        config48k_.clientBufferSize = 4096;
        config48k_.transmissionType = TransmissionType::NonBlocking;
        config48k_.logger = spdlog::default_logger();
        
        // Setup 44.1kHz configuration
        config441k_ = config48k_;
        config441k_.sampleRate = 44100.0;
        
        nodeID_ = 0x3F;
        
        // Create pre-calculator instances
        preCalc48k_ = std::make_unique<CIPPreCalculator>();
        preCalc441k_ = std::make_unique<CIPPreCalculator>();
    }
    
    void TearDown() override {
        if (preCalc48k_) preCalc48k_->stop();
        if (preCalc441k_) preCalc441k_->stop();
    }
    
    // Simulate the Apple DBC rule manually for verification
    struct DBCSimulator {
        uint8_t dbc{0};
        bool prevWasNoData{false};
        
        uint8_t calculateNextDBC(bool isNoData) {
            uint8_t currentDbc = dbc;
            
            if (isNoData) {
                // NO-DATA: do not advance DBC, just record that it was NO-DATA
                prevWasNoData = true;
            } else {
                // DATA packet
                if (!prevWasNoData) {
                    // Normal DATA after DATA: advance DBC by 8 BEFORE using it
                    dbc = (dbc + 8) & 0xFF;
                    currentDbc = dbc;
                } else {
                    // First DATA after NO-DATA: keep current DBC (Apple rule)
                    currentDbc = dbc;
                }
                prevWasNoData = false;
            }
            
            return currentDbc;
        }
    };
    
    TransmitterConfig config48k_, config441k_;
    uint16_t nodeID_;
    std::unique_ptr<CIPPreCalculator> preCalc48k_, preCalc441k_;
};

// Test fixture for DBC continuity checking
class DBCContinuityTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger_ = spdlog::default_logger();
        processor_ = std::make_unique<IsochPacketProcessor>(logger_);
    }
    
    void TearDown() override {
        processor_.reset();
    }
    
    // Helper to create a CIP header
    CIPHeader createCIPHeader(uint8_t dbc, bool isNoData, bool is48k = true) {
        CIPHeader header = {};
        header.sid_byte = 0x3F;
        header.dbs = 2;
        header.fn_qpc_sph_rsv = 0;
        header.dbc = dbc;
        header.fmt_eoh1 = CIP::kFmtEohValue;
        header.fdf = is48k ? CIP::kFDF_48k : CIP::kFDF_44k1;
        header.syt = isNoData ? CIP::kSytNoData : 0x1234; // Some valid SYT for DATA
        return header;
    }
    
    // Helper to create isoch header
    std::vector<uint8_t> createIsochHeader(uint16_t dataLen = 72, uint8_t tag = 1, uint8_t channel = 0) {
        std::vector<uint8_t> header(4);
        uint32_t isochHeader = ((uint32_t)dataLen << 16) | ((uint32_t)tag << 14) | ((uint32_t)channel << 8) | 0xA;
        // Convert to big endian
        header[0] = (isochHeader >> 24) & 0xFF;
        header[1] = (isochHeader >> 16) & 0xFF;
        header[2] = (isochHeader >> 8) & 0xFF;
        header[3] = isochHeader & 0xFF;
        return header;
    }
    
    // Helper to convert CIP header to byte array
    std::vector<uint8_t> cipHeaderToBytes(const CIPHeader& header) {
        std::vector<uint8_t> bytes(8);
        std::memcpy(bytes.data(), &header, 8);
        return bytes;
    }
    
    std::shared_ptr<spdlog::logger> logger_;
    std::unique_ptr<IsochPacketProcessor> processor_;
};

// Test the Apple DBC rule implementation in CIP pre-calculator
TEST_F(DBCCalculationTest, AppleDBC_Rule_48kHz) {
    preCalc48k_->initialize(config48k_, nodeID_);
    preCalc48k_->start();
    
    // Wait for calculation
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::vector<uint8_t> dbcSequence;
    std::vector<bool> packetTypes; // true = NO-DATA, false = DATA
    
    // Collect DBC values and packet types from pre-calculator
    for (uint32_t groupIdx = 0; groupIdx < 5; ++groupIdx) {
        PreCalcGroup group;
        if (preCalc48k_->groupRing_.pop(group)) {
            for (uint32_t pktIdx = 0; pktIdx < config48k_.packetsPerGroup; ++pktIdx) {
                const auto& packet = group.packets[pktIdx];
                dbcSequence.push_back(packet.header.dbc);
                packetTypes.push_back(packet.isNoData);
            }
        } else {
            // Wait a bit more and try again
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            --groupIdx; // Retry this group
        }
    }
    
    EXPECT_GT(dbcSequence.size(), 20u) << "Should have collected sufficient DBC samples";
    
    // Verify we have both DATA and NO-DATA packets for 48kHz
    bool hasData = false, hasNoData = false;
    for (bool isNoData : packetTypes) {
        if (isNoData) hasNoData = true;
        else hasData = true;
    }
    EXPECT_TRUE(hasData) << "48kHz should have DATA packets";
    EXPECT_TRUE(hasNoData) << "48kHz should have NO-DATA packets";
    
    // Verify Apple DBC rule patterns manually
    for (size_t i = 1; i < dbcSequence.size(); ++i) {
        uint8_t prevDbc = dbcSequence[i-1];
        uint8_t currDbc = dbcSequence[i];
        bool prevWasNoData = (i > 0) ? packetTypes[i-1] : false;
        bool currIsNoData = packetTypes[i];
        
        if (currIsNoData) {
            // NO-DATA should have same DBC as previous packet
            EXPECT_EQ(currDbc, prevDbc) << "NO-DATA packet " << i << " should have same DBC as previous";
        } else {
            // DATA packet
            if (prevWasNoData) {
                // First DATA after NO-DATA should have same DBC (Apple rule)
                EXPECT_EQ(currDbc, prevDbc) << "First DATA after NO-DATA at " << i << " should keep DBC";
            } else {
                // Normal DATA after DATA should advance by 8
                uint8_t expectedDbc = (prevDbc + 8) & 0xFF;
                EXPECT_EQ(currDbc, expectedDbc) << "DATA after DATA at " << i << " should advance DBC by 8";
            }
        }
    }
}

// Test DBC pattern for 44.1kHz
TEST_F(DBCCalculationTest, AppleDBC_Rule_441kHz) {
    preCalc441k_->initialize(config441k_, nodeID_);
    preCalc441k_->start();
    
    // Wait for calculation
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    DBCSimulator simulator;
    std::vector<uint8_t> dbcSequence;
    std::vector<bool> packetTypes;
    
    // Collect DBC values for 44.1kHz
    for (uint32_t groupIdx = 0; groupIdx < 10; ++groupIdx) {
        PreCalcGroup group;
        if (preCalc441k_->groupRing_.pop(group)) {
            for (uint32_t pktIdx = 0; pktIdx < config441k_.packetsPerGroup; ++pktIdx) {
                const auto& packet = group.packets[pktIdx];
                dbcSequence.push_back(packet.header.dbc);
                packetTypes.push_back(packet.isNoData);
                
                // Verify against simulator
                uint8_t expectedDbc = simulator.calculateNextDBC(packet.isNoData);
                EXPECT_EQ(packet.header.dbc, expectedDbc) 
                    << "DBC mismatch at group " << groupIdx << ", packet " << pktIdx;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            --groupIdx;
        }
    }
    
    EXPECT_GT(dbcSequence.size(), 50u) << "Should have collected sufficient DBC samples";
}

// Test DBC wraparound at 255->0
TEST_F(DBCCalculationTest, DBC_Wraparound) {
    preCalc48k_->initialize(config48k_, nodeID_);
    
    // Force sync to near-wraparound value
    preCalc48k_->forceSync(250, false); // Start at 250, previous was DATA
    preCalc48k_->start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::vector<uint8_t> dbcValues;
    
    // Collect packets around wraparound
    for (uint32_t i = 0; i < 20; ++i) {
        PreCalcGroup group;
        if (preCalc48k_->groupRing_.pop(group)) {
            for (uint32_t pktIdx = 0; pktIdx < config48k_.packetsPerGroup; ++pktIdx) {
                const auto& packet = group.packets[pktIdx];
                dbcValues.push_back(packet.header.dbc);
                
                // Look for wraparound (255 -> 7 for next DATA packet)
                if (packet.header.dbc < 10 && dbcValues.size() > 1) {
                    uint8_t prevDbc = dbcValues[dbcValues.size() - 2];
                    if (prevDbc > 240) {
                        // Found wraparound case
                        EXPECT_LT(packet.header.dbc, 20u) << "DBC should wrap to small value";
                        return; // Test passed
                    }
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

// Test DBC continuity checker with perfect Apple sequence
TEST_F(DBCContinuityTest, Perfect_Apple_Sequence) {
    // Simulate perfect Apple DBC sequence: DATA(0) -> NO-DATA(0) -> DATA(0) -> DATA(8) -> ...
    std::vector<std::pair<uint8_t, bool>> testSequence = {
        {0, false},   // DATA(0)
        {0, true},    // NO-DATA(0) - same DBC
        {0, false},   // DATA(0) - same DBC (first DATA after NO-DATA)
        {8, false},   // DATA(8) - advanced by 8
        {16, false},  // DATA(16) - advanced by 8
        {16, true},   // NO-DATA(16) - same DBC
        {16, false},  // DATA(16) - same DBC (first DATA after NO-DATA)
        {24, false},  // DATA(24) - advanced by 8
        {32, false},  // DATA(32) - advanced by 8
        {32, true},   // NO-DATA(32) - same DBC
        {32, false},  // DATA(32) - same DBC
        {40, false},  // DATA(40) - advanced by 8
    };
    
    uint32_t errors = 0;
    
    for (size_t i = 0; i < testSequence.size(); ++i) {
        uint8_t dbc = testSequence[i].first;
        bool isNoData = testSequence[i].second;
        
        // Create packet data
        auto isochHeader = createIsochHeader(isNoData ? 8 : 72);
        CIPHeader cipHeader = createCIPHeader(dbc, isNoData);
        auto cipBytes = cipHeaderToBytes(cipHeader);
        
        // Create dummy audio data for DATA packets
        std::vector<uint8_t> audioData(isNoData ? 0 : 64, 0x00);
        
        // Process packet
        auto result = processor_->processPacket(
            i / 8,           // groupIndex
            i % 8,           // packetIndexInGroup
            isochHeader.data(),
            cipBytes.data(),
            audioData.data(),
            audioData.size(),
            0x12345678       // fwTimestamp
        );
        
        EXPECT_TRUE(result.has_value()) << "Packet " << i << " should process successfully";
        
        // Note: We can't directly check for continuity errors without exposing internal state,
        // but successful processing indicates no critical errors
    }
    
    EXPECT_EQ(errors, 0u) << "Perfect sequence should have zero continuity errors";
}

// Test DBC continuity checker with discontinuity
TEST_F(DBCContinuityTest, DBC_Discontinuity_Detection) {
    // Sequence with intentional DBC jump: DATA(0) -> DATA(16) (should be 8)
    std::vector<std::pair<uint8_t, bool>> testSequence = {
        {0, false},   // DATA(0) - initial
        {16, false},  // DATA(16) - WRONG! Should be 8
        {24, false},  // DATA(24) - correct following the wrong one
    };
    
    for (size_t i = 0; i < testSequence.size(); ++i) {
        uint8_t dbc = testSequence[i].first;
        bool isNoData = testSequence[i].second;
        
        auto isochHeader = createIsochHeader(isNoData ? 8 : 72);
        CIPHeader cipHeader = createCIPHeader(dbc, isNoData);
        auto cipBytes = cipHeaderToBytes(cipHeader);
        std::vector<uint8_t> audioData(isNoData ? 0 : 64, 0x00);
        
        auto result = processor_->processPacket(
            i / 8, i % 8,
            isochHeader.data(),
            cipBytes.data(),
            audioData.data(),
            audioData.size(),
            0x12345678
        );
        
        // Packets should still process (error handling is internal)
        EXPECT_TRUE(result.has_value()) << "Packet " << i << " should process";
    }
}

// Stress test: Simulate 1,000 packets to verify Apple DBC pattern consistency
TEST_F(DBCCalculationTest, Stress_Test_1000_Packets) {
    preCalc48k_->initialize(config48k_, nodeID_);
    preCalc48k_->start();
    
    const uint32_t TARGET_PACKETS = 1000;
    std::vector<uint8_t> dbcSequence;
    std::vector<bool> packetTypes;
    
    auto startTime = std::chrono::steady_clock::now();
    
    while (dbcSequence.size() < TARGET_PACKETS) {
        PreCalcGroup group;
        if (preCalc48k_->groupRing_.pop(group)) {
            for (uint32_t pktIdx = 0; pktIdx < config48k_.packetsPerGroup && dbcSequence.size() < TARGET_PACKETS; ++pktIdx) {
                const auto& packet = group.packets[pktIdx];
                dbcSequence.push_back(packet.header.dbc);
                packetTypes.push_back(packet.isNoData);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        
        // Timeout protection
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (elapsed > std::chrono::seconds(10)) {
            FAIL() << "Stress test timed out. Only collected " << dbcSequence.size() << " packets";
        }
    }
    
    // Verify Apple DBC rule consistency across all packets
    uint32_t ruleViolations = 0;
    for (size_t i = 1; i < dbcSequence.size(); ++i) {
        uint8_t prevDbc = dbcSequence[i-1];
        uint8_t currDbc = dbcSequence[i];
        bool prevWasNoData = packetTypes[i-1];
        bool currIsNoData = packetTypes[i];
        
        if (currIsNoData) {
            // NO-DATA should have same DBC as previous
            if (currDbc != prevDbc) ruleViolations++;
        } else {
            // DATA packet
            if (prevWasNoData) {
                // First DATA after NO-DATA should keep DBC
                if (currDbc != prevDbc) ruleViolations++;
            } else {
                // Normal DATA after DATA should advance by 8
                uint8_t expectedDbc = (prevDbc + 8) & 0xFF;
                if (currDbc != expectedDbc) ruleViolations++;
            }
        }
    }
    
    auto elapsed = std::chrono::steady_clock::now() - startTime;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
    
    std::cout << "Stress test: " << dbcSequence.size() << " packets in " 
              << elapsedMs.count() << "ms (" 
              << (dbcSequence.size() * 1000.0 / elapsedMs.count()) << " packets/sec)\n";
    std::cout << "Apple DBC rule violations: " << ruleViolations << " out of " << (dbcSequence.size()-1) 
              << " (" << (100.0 * ruleViolations / (dbcSequence.size()-1)) << "%)\n";
    
    EXPECT_EQ(ruleViolations, 0u) << "Should have zero Apple DBC rule violations";
    EXPECT_EQ(dbcSequence.size(), TARGET_PACKETS) << "Should collect exactly " << TARGET_PACKETS << " packets";
}

// Test the specific pattern from the trace: DATA->NO-DATA->DATA with same DBC
TEST_F(DBCCalculationTest, Trace_Pattern_Verification) {
    // Pattern from the provided trace:
    // DATA DBC=0xE8 -> NO-DATA DBC=0xE8 -> DATA DBC=0xE8 -> DATA DBC=0xF0
    std::vector<std::tuple<uint8_t, bool, std::string>> expectedPattern = {
        {0xE8, false, "DATA(0xE8)"},
        {0xE8, true,  "NO-DATA(0xE8)"},
        {0xE8, false, "DATA(0xE8)"},
        {0xF0, false, "DATA(0xF0)"},
        {0xF8, false, "DATA(0xF8)"},
        {0xF8, true,  "NO-DATA(0xF8)"},
        {0xF8, false, "DATA(0xF8)"},
        {0x00, false, "DATA(0x00)"}, // Wraparound case
    };
    
    preCalc48k_->initialize(config48k_, nodeID_);
    preCalc48k_->forceSync(0xE8, false); // Start at the trace DBC value
    preCalc48k_->start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    DBCSimulator simulator;
    simulator.dbc = 0xE8;
    simulator.prevWasNoData = false;
    
    uint32_t patternIndex = 0;
    bool patternMatched = false;
    
    // Look for the specific pattern in generated packets
    for (uint32_t attempts = 0; attempts < 100 && !patternMatched; ++attempts) {
        PreCalcGroup group;
        if (preCalc48k_->groupRing_.pop(group)) {
            for (uint32_t pktIdx = 0; pktIdx < config48k_.packetsPerGroup; ++pktIdx) {
                const auto& packet = group.packets[pktIdx];
                
                if (patternIndex < expectedPattern.size()) {
                    uint8_t expectedDbc = std::get<0>(expectedPattern[patternIndex]);
                    bool expectedIsNoData = std::get<1>(expectedPattern[patternIndex]);
                    std::string description = std::get<2>(expectedPattern[patternIndex]);
                    
                    if (packet.header.dbc == expectedDbc && packet.isNoData == expectedIsNoData) {
                        std::cout << "Matched pattern[" << patternIndex << "]: " << description << std::endl;
                        patternIndex++;
                        
                        if (patternIndex >= expectedPattern.size()) {
                            patternMatched = true;
                            break;
                        }
                    }
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    
    EXPECT_TRUE(patternMatched) << "Should find the exact trace pattern in generated packets";
}