#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "Isoch/core/CIPPreCalculator.hpp"
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include <random>

using namespace FWA::Isoch;

// Concurrency test fixture
class ConcurrencyTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.numGroups = 16;
        config_.packetsPerGroup = 8;
        config_.sampleRate = 48000.0;
        config_.clientBufferSize = 4096;
        config_.transmissionType = TransmissionType::NonBlocking;
        config_.logger = spdlog::default_logger();
        
        preCalc_ = std::make_unique<CIPPreCalculator>();
        preCalc_->initialize(config_, 0x3F, nullptr);
    }
    
    void TearDown() override {
        if (preCalc_) {
            preCalc_->stop();
        }
    }
    
    TransmitterConfig config_;
    std::unique_ptr<CIPPreCalculator> preCalc_;
};

// Test concurrent readers and writers
TEST_F(ConcurrencyTest, ConcurrentReadersWriters_NoDataRaces) {
    preCalc_->start();
    
    std::atomic<bool> stopTest{false};
    std::atomic<uint32_t> readerSuccesses{0};
    std::atomic<uint32_t> readerFailures{0};
    std::atomic<uint32_t> writerOperations{0};
    std::atomic<uint32_t> dataRaces{0};
    
    const int numReaderThreads = 4;
    const int numWriterThreads = 1; // Pre-calculator is the main writer
    
    std::vector<std::thread> readerThreads;
    std::random_device rd;
    
    // Start reader threads (simulating DCL callbacks)
    for (int i = 0; i < numReaderThreads; ++i) {
        readerThreads.emplace_back([&, i]() {
            std::mt19937 gen(rd() + i);
            std::uniform_int_distribution<> groupDist(0, config_.numGroups - 1);
            
            while (!stopTest.load()) {
                uint32_t group = groupDist(gen);
                const auto* state = preCalc_->getGroupState(group);
                
                if (state) {
                    // Verify data consistency
                    uint32_t groupNum = state->groupNumber.load();
                    bool ready = state->ready.load();
                    uint8_t packetCount = state->packetCount;
                    
                    if (ready && groupNum == group && packetCount == config_.packetsPerGroup) {
                        readerSuccesses.fetch_add(1);
                        
                        // Simulate processing the group
                        for (uint8_t p = 0; p < packetCount; ++p) {
                            const auto& pkt = state->packets[p];
                            volatile uint8_t dbc = pkt.header.dbc;  // Force read
                            volatile bool isNoData = pkt.isNoData;
                            (void)dbc; (void)isNoData;  // Suppress warnings
                        }
                        
                        // Mark as consumed
                        preCalc_->markGroupConsumed(group);
                    } else if (ready) {
                        // Data inconsistency detected
                        dataRaces.fetch_add(1);
                    } else {
                        // Group not ready - normal condition
                        readerFailures.fetch_add(1);
                    }
                } else {
                    readerFailures.fetch_add(1);
                }
                
                // Simulate 8kHz callback rate (125µs period)
                std::this_thread::sleep_for(std::chrono::microseconds(125));
            }
        });
    }
    
    // Monitor writer operations (pre-calculator thread)
    std::thread writerMonitor([&]() {
        uint32_t lastGroup = 0;
        while (!stopTest.load()) {
            const auto* state = preCalc_->getGroupState(lastGroup % config_.numGroups);
            if (state && state->ready.load()) {
                writerOperations.fetch_add(1);
                lastGroup++;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });
    
    // Run test for a reasonable duration
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    stopTest.store(true);
    
    // Wait for all threads
    for (auto& t : readerThreads) {
        t.join();
    }
    writerMonitor.join();
    
    // Verify results
    uint32_t totalReads = readerSuccesses.load() + readerFailures.load();
    uint32_t totalWrites = writerOperations.load();
    uint32_t races = dataRaces.load();
    
    std::cout << "Concurrency Test Results:" << std::endl;
    std::cout << "  Total reads: " << totalReads << std::endl;
    std::cout << "  Successful reads: " << readerSuccesses.load() << std::endl;
    std::cout << "  Failed reads: " << readerFailures.load() << std::endl;
    std::cout << "  Writer operations: " << totalWrites << std::endl;
    std::cout << "  Data races detected: " << races << std::endl;
    
    // Assertions
    EXPECT_GT(readerSuccesses.load(), 0) << "Should have successful reads";
    EXPECT_GT(totalWrites, 0) << "Should have writer operations";
    EXPECT_EQ(races, 0) << "No data races should be detected";
    
    // Performance expectations
    double successRate = static_cast<double>(readerSuccesses.load()) / totalReads;
    EXPECT_GT(successRate, 0.1) << "Should have reasonable success rate: " << successRate;
}

// Test stress conditions with high frequency access
TEST_F(ConcurrencyTest, HighFrequencyAccess_StressTest) {
    preCalc_->start();
    
    std::atomic<bool> stopTest{false};
    std::atomic<uint64_t> totalOperations{0};
    std::atomic<uint64_t> consistencyErrors{0};
    
    const int numThreads = 8;
    std::vector<std::thread> threads;
    
    // High-frequency access threads
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&, i]() {
            while (!stopTest.load()) {
                uint32_t group = i % config_.numGroups;
                const auto* state = preCalc_->getGroupState(group);
                
                if (state) {
                    // Rapid consistency checks
                    uint32_t groupNum1 = state->groupNumber.load();
                    bool ready1 = state->ready.load();
                    uint32_t groupNum2 = state->groupNumber.load();
                    bool ready2 = state->ready.load();
                    
                    if (ready1 && ready2) {
                        if (groupNum1 != groupNum2) {
                            consistencyErrors.fetch_add(1);
                        }
                        if (groupNum1 != group && groupNum2 != group) {
                            consistencyErrors.fetch_add(1);
                        }
                    }
                    
                    preCalc_->markGroupConsumed(group);
                }
                
                totalOperations.fetch_add(1);
                
                // Minimal delay for maximum stress
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        });
    }
    
    // Run stress test
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stopTest.store(true);
    
    // Wait for completion
    for (auto& t : threads) {
        t.join();
    }
    
    uint64_t ops = totalOperations.load();
    uint64_t errors = consistencyErrors.load();
    
    std::cout << "Stress Test Results:" << std::endl;
    std::cout << "  Total operations: " << ops << std::endl;
    std::cout << "  Consistency errors: " << errors << std::endl;
    std::cout << "  Error rate: " << (static_cast<double>(errors) / ops * 100.0) << "%" << std::endl;
    
    EXPECT_GT(ops, 10000) << "Should perform many operations under stress";
    EXPECT_EQ(errors, 0) << "Should have no consistency errors";
}

// Test memory ordering and synchronization
TEST_F(ConcurrencyTest, MemoryOrdering_Synchronization) {
    preCalc_->start();
    
    std::atomic<bool> stopTest{false};
    std::atomic<uint32_t> observedInconsistencies{0};
    
    // Thread that observes group state transitions
    std::thread observer([&]() {
        uint32_t lastObservedGroup = UINT32_MAX;
        
        while (!stopTest.load()) {
            for (uint32_t g = 0; g < config_.numGroups; ++g) {
                const auto* state = preCalc_->getGroupState(g);
                if (!state) continue;
                
                // Check for proper memory ordering
                std::atomic_thread_fence(std::memory_order_acquire);
                
                bool ready = state->ready.load(std::memory_order_acquire);
                if (ready) {
                    uint32_t groupNum = state->groupNumber.load(std::memory_order_relaxed);
                    uint8_t packetCount = state->packetCount;
                    
                    // Verify consistency after acquire fence
                    if (groupNum != g) {
                        observedInconsistencies.fetch_add(1);
                    }
                    
                    if (packetCount != config_.packetsPerGroup) {
                        observedInconsistencies.fetch_add(1);
                    }
                    
                    // Check packet data consistency
                    for (uint8_t p = 0; p < packetCount; ++p) {
                        const auto& pkt = state->packets[p];
                        if (pkt.header.dbs != 2) {  // Should always be 2 for stereo
                            observedInconsistencies.fetch_add(1);
                        }
                    }
                    
                    lastObservedGroup = g;
                }
            }
            
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });
    
    // Run observation
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    stopTest.store(true);
    observer.join();
    
    uint32_t inconsistencies = observedInconsistencies.load();
    
    std::cout << "Memory Ordering Test Results:" << std::endl;
    std::cout << "  Observed inconsistencies: " << inconsistencies << std::endl;
    
    EXPECT_EQ(inconsistencies, 0) << "Memory ordering should prevent inconsistencies";
}

// Test flow control and backpressure
TEST_F(ConcurrencyTest, FlowControl_Backpressure) {
    preCalc_->start();
    
    std::atomic<bool> stopTest{false};
    std::atomic<uint32_t> groupsProduced{0};
    std::atomic<uint32_t> groupsConsumed{0};
    std::atomic<uint32_t> maxBufferDepth{0};
    
    // Fast producer (pre-calculator)
    std::thread producer([&]() {
        while (!stopTest.load()) {
            // Count ready groups
            uint32_t readyCount = 0;
            for (uint32_t g = 0; g < config_.numGroups; ++g) {
                const auto* state = preCalc_->getGroupState(g);
                if (state && state->ready.load()) {
                    readyCount++;
                }
            }
            
            groupsProduced.store(readyCount);
            
            // Track maximum buffer depth
            uint32_t currentMax = maxBufferDepth.load();
            while (readyCount > currentMax && 
                   !maxBufferDepth.compare_exchange_weak(currentMax, readyCount)) {
                // Retry
            }
            
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    });
    
    // Slow consumer
    std::thread consumer([&]() {
        uint32_t consumed = 0;
        while (!stopTest.load()) {
            uint32_t group = consumed % config_.numGroups;
            const auto* state = preCalc_->getGroupState(group);
            
            if (state && state->ready.load()) {
                preCalc_->markGroupConsumed(group);
                consumed++;
                groupsConsumed.store(consumed);
            }
            
            // Intentionally slower than producer
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });
    
    // Run flow control test
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stopTest.store(true);
    
    producer.join();
    consumer.join();
    
    uint32_t produced = groupsProduced.load();
    uint32_t consumed = groupsConsumed.load();
    uint32_t maxDepth = maxBufferDepth.load();
    
    std::cout << "Flow Control Test Results:" << std::endl;
    std::cout << "  Groups in buffer: " << produced << std::endl;
    std::cout << "  Groups consumed: " << consumed << std::endl;
    std::cout << "  Max buffer depth: " << maxDepth << std::endl;
    
    // Flow control should prevent unlimited buffering
    EXPECT_LT(maxDepth, config_.numGroups) << "Buffer depth should be limited";
    EXPECT_GT(consumed, 0) << "Consumer should make progress";
}
