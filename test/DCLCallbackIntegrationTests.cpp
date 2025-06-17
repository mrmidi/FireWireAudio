#include <gtest/gtest.h>
#include "Isoch/core/CIPPreCalculator.hpp"
#include "Isoch/core/CIPHeader.hpp"
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <spdlog/spdlog.h>

using namespace FWA::Isoch;

// Copy the DBC continuity checker from AmdtpTransmitter.cpp for testing
namespace {
    constexpr uint16_t DBC_WRAP = 256;
    constexpr uint8_t NO_DATA_INCREMENT = 8;
    constexpr uint8_t SYT_INTERVAL = 8;
    
    inline bool checkDbcContinuity(uint8_t currentDbc, bool isNoData,
                                   uint8_t& lastDataPacketDbc, uint8_t& lastPacketDbc,
                                   bool& prevPacketWasNoData, bool& hasValidState,
                                   const std::shared_ptr<spdlog::logger>& logger) {
        
        if (isNoData) {
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
            if (!hasValidState || lastDataPacketDbc == 0xFF) {
                lastDataPacketDbc = currentDbc;
                hasValidState = true;
            } else {
                uint8_t expectedDbc;
                if (prevPacketWasNoData) {
                    expectedDbc = lastPacketDbc;
                } else {
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
            
            lastDataPacketDbc = currentDbc;
            lastPacketDbc = currentDbc;
            prevPacketWasNoData = false;
        }
        
        return true;
    }
}

// Test fixture that simulates the DCL callback integration
class DCLCallbackIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger_ = spdlog::default_logger();
        
        config_.numGroups = 16;
        config_.packetsPerGroup = 8;
        config_.sampleRate = 48000.0;
        config_.clientBufferSize = 4096;
        config_.transmissionType = TransmissionType::NonBlocking;
        config_.logger = logger_;
        config_.callbackGroupInterval = 8;
        
        resetState();
    }
    
    void TearDown() override {
        if (preCalc_) {
            preCalc_->stop();
        }
    }
    
    void resetState() {
        lastDataPacketDbc_ = 0xFF;
        lastPacketDbc_ = 0xFF;
        prevPacketWasNoData_ = false;
        hasValidDbcState_ = false;
        totalCallbacks_ = 0;
        totalPacketsProcessed_ = 0;
        dbcErrors_ = 0;
        missedPrecalc_ = 0;
    }
    
    // Simulate the core logic of handleDCLCompleteFastPath
    void simulateHandleDCLCompleteFastPath(uint32_t completedGroupIndex) {
        totalCallbacks_++;
        
        if (!preCalc_) {
            logger_->error("No pre-calculator available");
            return;
        }
        
        // Step 0: Verify DBC on the group we just sent (post-transmission validation)
        // This simulates reading from buffer manager and validating sent packets
        simulatePostTransmissionValidation(completedGroupIndex);
        
        // Step 1: Determine number of groups to process
        uint32_t numGroupsToProcess = config_.callbackGroupInterval;
        
        // Step 2: Handle first-time execution logic
        static bool isFirstTimeExecution = true;
        if (isFirstTimeExecution) {
            logger_->info("First DCL callback received (group {}). Priming pipeline.", completedGroupIndex);
            numGroupsToProcess = 2;  // kGroupsPerCallback
            isFirstTimeExecution = false;
        }
        
        // Step 3: Calculate group processing range
        uint32_t firstGroupInCompletedBatch;
        if (completedGroupIndex >= (numGroupsToProcess - 1)) {
            firstGroupInCompletedBatch = completedGroupIndex - (numGroupsToProcess - 1);
        } else {
            firstGroupInCompletedBatch = config_.numGroups + completedGroupIndex - (numGroupsToProcess - 1);
        }
        
        // Step 4: Process the next batch of groups
        for (uint32_t i = 0; i < numGroupsToProcess; ++i) {
            uint32_t processedGroup = (firstGroupInCompletedBatch + i) % config_.numGroups;
            uint32_t fillGroup = (processedGroup + 2) % config_.numGroups;  // kGroupsPerCallback = 2
            
            simulateProcessAndQueueGroup(fillGroup);
        }
    }
    
    // Simulate the post-transmission DBC validation from handleDCLCompleteFastPath
    void simulatePostTransmissionValidation(uint32_t groupIndex) {
        // FIXED: Validate only the header we actually have (Fix A from IEEE-1394 expert)
        // The original code was checking the same packet 8 times, causing artificial errors
        if (!sentPackets_.empty()) {
            const auto& pkt = sentPackets_.back();         // last packet we really sent
            bool ok = checkDbcContinuity(pkt.dbc, pkt.isNoData,
                                         lastDataPacketDbc_, lastPacketDbc_,
                                         prevPacketWasNoData_, hasValidDbcState_, logger_);
            if (!ok) dbcErrors_++;
        }
    }
    
    // Simulate processAndQueueGroup from handleDCLCompleteFastPath
    void simulateProcessAndQueueGroup(uint32_t fillGroup) {
        // Step 1: Try to get pre-calculated group from SPSC ring
        PreCalcGroup grp;
        bool havePrecalc = preCalc_->groupRing_.pop(grp);
        
        if (!havePrecalc) {
            // EMERGENCY PATH: Use emergency calculation
            logger_->warn("No pre-calc data for group {}, using emergency", fillGroup);
            missedPrecalc_++;
            
            for (uint32_t p = 0; p < config_.packetsPerGroup; ++p) {
                CIPHeader header = {};
                
                // Use emergency calculation
                bool isNoData = preCalc_->emergencyCalculateCIP(&header, p);
                
                // DBC continuity check (emergency path)
                bool continuityOk = checkDbcContinuity(
                    header.dbc, isNoData,
                    lastDataPacketDbc_, lastPacketDbc_,
                    prevPacketWasNoData_, hasValidDbcState_, logger_);
                
                if (!continuityOk) {
                    dbcErrors_++;
                }
                
                // Record processed packet
                PacketInfo pkt = {header.dbc, isNoData, fillGroup, p};
                processedPackets_.push_back(pkt);
                totalPacketsProcessed_++;
            }
        } else {
            // FAST PATH: Use pre-calculated headers
            for (uint32_t p = 0; p < config_.packetsPerGroup; ++p) {
                const auto& pktInfo = grp.packets[p];
                
                // DBC continuity check (fast path)
                bool continuityOk = checkDbcContinuity(
                    pktInfo.header.dbc, pktInfo.isNoData,
                    lastDataPacketDbc_, lastPacketDbc_,
                    prevPacketWasNoData_, hasValidDbcState_, logger_);
                
                if (!continuityOk) {
                    dbcErrors_++;
                }
                
                // Record processed packet
                PacketInfo pkt = {pktInfo.header.dbc, pktInfo.isNoData, fillGroup, p};
                processedPackets_.push_back(pkt);
                totalPacketsProcessed_++;
                
                // Simulate sending packet (for post-transmission validation)
                sentPackets_.push_back(pkt);
                if (sentPackets_.size() > 100) {
                    sentPackets_.erase(sentPackets_.begin(), sentPackets_.begin() + 50);
                }
            }
        }
    }
    
    struct PacketInfo {
        uint8_t dbc;
        bool isNoData;
        uint32_t groupIndex;
        uint32_t packetIndex;
    };
    
    TransmitterConfig config_;
    std::unique_ptr<CIPPreCalculator> preCalc_;
    std::shared_ptr<spdlog::logger> logger_;
    
    // DBC validation state (mirrors AmdtpTransmitter)
    uint8_t lastDataPacketDbc_;
    uint8_t lastPacketDbc_;
    bool prevPacketWasNoData_;
    bool hasValidDbcState_;
    
    // Test metrics
    std::atomic<uint32_t> totalCallbacks_{0};
    std::atomic<uint32_t> totalPacketsProcessed_{0};
    std::atomic<uint32_t> dbcErrors_{0};
    std::atomic<uint32_t> missedPrecalc_{0};
    
    std::vector<PacketInfo> processedPackets_;
    std::vector<PacketInfo> sentPackets_;
};

// Test the complete integration: pre-calculator + DCL callback + DBC validation
TEST_F(DCLCallbackIntegrationTest, Complete_Integration_Test) {
    // Setup and start CIP pre-calculator
    preCalc_ = std::make_unique<CIPPreCalculator>();
    preCalc_->initialize(config_, 0x3F);
    preCalc_->start();
    
    // Wait for pre-calculator to build up some data
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Simulate multiple DCL callbacks (as would happen in real FireWire operation)
    const uint32_t NUM_CALLBACKS = 20;
    for (uint32_t callback = 0; callback < NUM_CALLBACKS; ++callback) {
        uint32_t completedGroupIndex = callback % config_.numGroups;
        
        simulateHandleDCLCompleteFastPath(completedGroupIndex);
        
        // Small delay to simulate real-time constraints
        std::this_thread::sleep_for(std::chrono::microseconds(125));  // 8kHz = 125μs per cycle
    }
    
    // Verify results
    EXPECT_EQ(totalCallbacks_.load(), NUM_CALLBACKS) << "Should process all callbacks";
    EXPECT_GT(totalPacketsProcessed_.load(), NUM_CALLBACKS * config_.packetsPerGroup) 
        << "Should process multiple packets per callback";
    EXPECT_EQ(dbcErrors_.load(), 0u) << "Should have zero DBC continuity errors";
    EXPECT_LT(missedPrecalc_.load(), NUM_CALLBACKS / 4) 
        << "Should not miss pre-calc data too often";
    
    logger_->info("Integration test results: {} callbacks, {} packets, {} DBC errors, {} missed pre-calc",
                 totalCallbacks_.load(), totalPacketsProcessed_.load(), 
                 dbcErrors_.load(), missedPrecalc_.load());
}

// Test high-frequency DCL callbacks (stress test)
TEST_F(DCLCallbackIntegrationTest, High_Frequency_DCL_Callbacks) {
    preCalc_ = std::make_unique<CIPPreCalculator>();
    preCalc_->initialize(config_, 0x3F);
    preCalc_->start();
    
    // Wait for initial buffer fill
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Simulate high-frequency callbacks (8kHz FireWire rate)
    const uint32_t NUM_CALLBACKS = 100;
    const auto callbackInterval = std::chrono::microseconds(125);  // 8kHz
    
    for (uint32_t callback = 0; callback < NUM_CALLBACKS; ++callback) {
        auto callbackStart = std::chrono::high_resolution_clock::now();
        
        uint32_t completedGroupIndex = callback % config_.numGroups;
        simulateHandleDCLCompleteFastPath(completedGroupIndex);
        
        auto callbackEnd = std::chrono::high_resolution_clock::now();
        auto callbackDuration = std::chrono::duration_cast<std::chrono::microseconds>(callbackEnd - callbackStart);
        
        // Verify callback completes within reasonable time
        EXPECT_LT(callbackDuration.count(), 100) << "Callback should complete within 100μs";
        
        // Maintain timing (simulate real-time constraints)
        std::this_thread::sleep_until(callbackStart + callbackInterval);
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    // Verify performance
    EXPECT_EQ(dbcErrors_.load(), 0u) << "Should have zero DBC errors under high frequency";
    EXPECT_LT(missedPrecalc_.load(), NUM_CALLBACKS / 10) << "Should maintain pre-calc performance";
    
    double callbacksPerSecond = NUM_CALLBACKS * 1000.0 / totalDuration.count();
    EXPECT_GT(callbacksPerSecond, 7000.0) << "Should maintain >7kHz callback rate";
    
    logger_->info("High-frequency test: {:.1f} callbacks/sec, {} DBC errors, {} missed pre-calc",
                 callbacksPerSecond, dbcErrors_.load(), missedPrecalc_.load());
}

// Test DCL callback with varying group intervals
TEST_F(DCLCallbackIntegrationTest, Variable_Group_Intervals) {
    preCalc_ = std::make_unique<CIPPreCalculator>();
    preCalc_->initialize(config_, 0x3F);
    preCalc_->start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Test different callback group intervals
    std::vector<uint32_t> testIntervals = {1, 2, 4, 8, 16};
    
    for (uint32_t interval : testIntervals) {
        resetState();
        config_.callbackGroupInterval = interval;
        
        // Run callbacks with this interval
        for (uint32_t callback = 0; callback < 10; ++callback) {
            uint32_t completedGroupIndex = callback % config_.numGroups;
            simulateHandleDCLCompleteFastPath(completedGroupIndex);
        }
        
        EXPECT_EQ(dbcErrors_.load(), 0u) << "Should have zero DBC errors with interval " << interval;
        EXPECT_GT(totalPacketsProcessed_.load(), 0u) << "Should process packets with interval " << interval;
        
        logger_->info("Interval {} test: {} packets, {} DBC errors", 
                     interval, totalPacketsProcessed_.load(), dbcErrors_.load());
    }
}

// Test pre-calculator thread synchronization
TEST_F(DCLCallbackIntegrationTest, PreCalculator_Thread_Synchronization) {
    preCalc_ = std::make_unique<CIPPreCalculator>();
    preCalc_->initialize(config_, 0x3F);
    preCalc_->start();
    
    std::atomic<bool> testRunning{true};
    std::atomic<uint32_t> producerPackets{0};
    std::atomic<uint32_t> consumerPackets{0};
    std::atomic<uint32_t> syncErrors{0};
    
    // Producer thread (simulates CIP pre-calculator)
    std::thread producerThread([&]() {
        while (testRunning.load()) {
            // The actual pre-calculator is running, we just count what it produces
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            producerPackets.fetch_add(config_.packetsPerGroup);  // Estimate
        }
    });
    
    // Consumer thread (simulates DCL callbacks)
    std::thread consumerThread([&]() {
        uint32_t callbackCount = 0;
        while (testRunning.load() && callbackCount < 50) {
            uint32_t completedGroupIndex = callbackCount % config_.numGroups;
            
            auto beforePackets = totalPacketsProcessed_.load();
            simulateHandleDCLCompleteFastPath(completedGroupIndex);
            auto afterPackets = totalPacketsProcessed_.load();
            
            if (afterPackets > beforePackets) {
                consumerPackets.fetch_add(afterPackets - beforePackets);
            }
            
            if (dbcErrors_.load() > 0) {
                syncErrors.fetch_add(1);
            }
            
            callbackCount++;
            std::this_thread::sleep_for(std::chrono::microseconds(125));  // 8kHz
        }
    });
    
    // Run test for limited time
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    testRunning = false;
    
    producerThread.join();
    consumerThread.join();
    
    EXPECT_GT(consumerPackets.load(), 100u) << "Should consume substantial packets";
    EXPECT_EQ(syncErrors.load(), 0u) << "Should have no synchronization errors";
    EXPECT_EQ(dbcErrors_.load(), 0u) << "Should maintain DBC continuity across threads";
    
    logger_->info("Sync test: {} consumer packets, {} sync errors, {} DBC errors",
                 consumerPackets.load(), syncErrors.load(), dbcErrors_.load());
}

// Test emergency path performance
TEST_F(DCLCallbackIntegrationTest, Emergency_Path_Performance) {
    preCalc_ = std::make_unique<CIPPreCalculator>();
    preCalc_->initialize(config_, 0x3F);
    // Don't start pre-calculator to force emergency path
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Run callbacks that will all use emergency path
    const uint32_t NUM_EMERGENCY_CALLBACKS = 20;
    for (uint32_t callback = 0; callback < NUM_EMERGENCY_CALLBACKS; ++callback) {
        uint32_t completedGroupIndex = callback % config_.numGroups;
        simulateHandleDCLCompleteFastPath(completedGroupIndex);
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    
    // All callbacks should have used emergency path
    EXPECT_EQ(missedPrecalc_.load(), NUM_EMERGENCY_CALLBACKS) 
        << "All callbacks should use emergency path";
    EXPECT_GT(totalPacketsProcessed_.load(), 0u) << "Should still process packets";
    
    // DBC continuity should still be reasonable (not perfect due to emergency calc)
    double errorRate = (double)dbcErrors_.load() / totalPacketsProcessed_.load();
    EXPECT_LT(errorRate, 0.1) << "Emergency path should have <10% DBC error rate";
    
    double avgCallbackTime = (double)duration.count() / NUM_EMERGENCY_CALLBACKS;
    EXPECT_LT(avgCallbackTime, 1000.0) << "Emergency callbacks should complete within 1ms";
    
    logger_->info("Emergency path: {:.1f}μs avg callback, {:.2f}% DBC error rate",
                 avgCallbackTime, errorRate * 100.0);
}

// Test DBC wraparound in integration scenario
TEST_F(DCLCallbackIntegrationTest, DBC_Wraparound_Integration) {
    preCalc_ = std::make_unique<CIPPreCalculator>();
    preCalc_->initialize(config_, 0x3F);
    
    // Force sync to near wraparound
    preCalc_->forceSync(240, false);  // Start near DBC wraparound
    preCalc_->start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Run enough callbacks to trigger wraparound
    const uint32_t NUM_CALLBACKS = 30;
    bool wraparoundDetected = false;
    
    for (uint32_t callback = 0; callback < NUM_CALLBACKS; ++callback) {
        uint32_t completedGroupIndex = callback % config_.numGroups;
        
        auto beforePackets = processedPackets_.size();
        simulateHandleDCLCompleteFastPath(completedGroupIndex);
        auto afterPackets = processedPackets_.size();
        
        // Check for DBC wraparound in processed packets
        for (size_t i = beforePackets; i < afterPackets; ++i) {
            if (i > 0 && processedPackets_[i].dbc < processedPackets_[i-1].dbc) {
                if (processedPackets_[i-1].dbc > 240 && processedPackets_[i].dbc < 20) {
                    wraparoundDetected = true;
                    logger_->info("DBC wraparound detected: {} -> {}", 
                                 processedPackets_[i-1].dbc, processedPackets_[i].dbc);
                }
            }
        }
    }
    
    EXPECT_TRUE(wraparoundDetected) << "Should detect DBC wraparound during test";
    EXPECT_EQ(dbcErrors_.load(), 0u) << "Should handle DBC wraparound without errors";
    EXPECT_GT(totalPacketsProcessed_.load(), NUM_CALLBACKS * 2) 
        << "Should process substantial packets";
}