#include "Isoch/core/CIPPreCalculator.hpp"
#include <os/log.h>
#include <mach/thread_policy.h>
#include <CoreServices/CoreServices.h>

using namespace FWA::Isoch;

CIPPreCalculator::~CIPPreCalculator() {
    stop();
}

void CIPPreCalculator::initialize(const TransmitterConfig& config, uint16_t nodeID) {
    config_  = config;
    nodeID_  = nodeID & 0x3F;
    calcState_ = {};

    for (auto& g : groupStates_) {
        g.version.store(0, std::memory_order_relaxed);           // even = ready, odd = writing
        g.preparedAtTime.store(0, std::memory_order_relaxed);
        g.groupNumber.store(UINT32_MAX, std::memory_order_relaxed);
        g.finalDbc = 0;
        g.packetCount = config.packetsPerGroup;
    }
    nextGroup_.store(0, std::memory_order_relaxed);
    lastConsumed_.store(0, std::memory_order_relaxed);  // Start with absolute 0

    os_log(OS_LOG_DEFAULT,
           "CIPPreCalculator init: %u×%u pkts @%.1fkHz",
           config.numGroups,
           config.packetsPerGroup,
           config.sampleRate/1000.0);
}

void CIPPreCalculator::start() {
    if (running_.exchange(true)) return;
    calcThread_ = std::thread([this]{
        pthread_setname_np("FWA_CIP_Calc");
        configureCPUAffinity();
        os_log(OS_LOG_DEFAULT, "CIP pre-calc thread started");
        while (running_.load()) {
            calculateNextGroup();
            auto sl = getSleepDuration();
            if (sl.count()>0) std::this_thread::sleep_for(sl);
        }
        os_log(OS_LOG_DEFAULT, "CIP pre-calc thread stopped");
    });
}

void CIPPreCalculator::stop() {
    if (!running_.exchange(false)) return;
    if (calcThread_.joinable()) calcThread_.join();
}

const CIPPreCalculator::GroupState* CIPPreCalculator::getGroupState(uint32_t requestedGroup) const {
    uint32_t bufferSlot = requestedGroup % kBufferDepth;
    const auto* state = &groupStates_[bufferSlot];
    
    // Version-based check with retry loop to handle torn reads
    for (int retry = 0; retry < 3; ++retry) {
        uint32_t v1 = state->version.load(std::memory_order_acquire);
        if (v1 & 1) continue;  // Writer in progress (odd version)
        
        // Verify group number matches
        uint32_t actualGroup = state->groupNumber.load(std::memory_order_relaxed);
        if (actualGroup != requestedGroup) {
            return nullptr;  // Group mismatch
        }
        
        // Memory barrier to ensure we see consistent packet data
        std::atomic_thread_fence(std::memory_order_acquire);
        
        // Check version hasn't changed during read
        uint32_t v2 = state->version.load(std::memory_order_acquire);
        if (v1 == v2) {
            return state;  // Consistent read
        }
        // Version changed, retry
    }
    
    return nullptr;  // Failed to get consistent read after retries
}

void CIPPreCalculator::markGroupConsumed(uint32_t groupIdx) {
    // Mark group as consumed using absolute counter increment
    lastConsumed_.fetch_add(1, std::memory_order_relaxed);
}

void CIPPreCalculator::forceSync(uint8_t dbc, bool prevWasNoData) {
    std::lock_guard<std::mutex> lock(syncMutex_);
    calcState_.dbc = dbc;
    calcState_.prevWasNoData = prevWasNoData;
    
    // Reset all group states to prevent stale data
    for (auto& g : groupStates_) {
        g.version.store(0, std::memory_order_relaxed);  // Mark as ready but stale
        g.groupNumber.store(UINT32_MAX, std::memory_order_relaxed);
    }
    
    // Reset absolute counters to start fresh
    nextGroup_.store(0, std::memory_order_relaxed);
    lastConsumed_.store(0, std::memory_order_relaxed);
    
    os_log(OS_LOG_DEFAULT, "CIPPreCalculator force sync: DBC=%u, prevWasNoData=%d", dbc, prevWasNoData);
}

void CIPPreCalculator::calculateNextGroup() {
    uint64_t t0 = mach_absolute_time();
    
    // Use absolute group counter
    uint64_t absoluteGroup = nextGroup_.load(std::memory_order_relaxed);
    uint32_t targetGroup = absoluteGroup % config_.numGroups;  // Convert to ring position
    uint32_t bufferSlot = targetGroup % kBufferDepth;
    auto& st = groupStates_[bufferSlot];
    
    std::lock_guard<std::mutex> lock(syncMutex_);

    // ─── PHASE 1: Begin Update ───
    uint32_t currentVersion = st.version.load(std::memory_order_relaxed);
    if (currentVersion & 1) {
        // Already being written by another thread, skip
        return;
    }
    
    // Mark as "writing" (odd version)
    st.version.store(currentVersion + 1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_seq_cst);  // Full barrier

    // Flow control using absolute counters
    uint64_t lastConsumed = lastConsumed_.load(std::memory_order_relaxed);
    uint64_t groupsAhead = absoluteGroup - lastConsumed;
    if (groupsAhead >= kBufferDepth) {
        // Too far ahead, mark as ready without update and return
        st.version.store(currentVersion, std::memory_order_release);
        return;
    }

    // Store the target group number
    st.groupNumber.store(targetGroup, std::memory_order_relaxed);

    const bool is48 = (config_.sampleRate == 48000.0);
    for (uint32_t p = 0; p < config_.packetsPerGroup; ++p) {
        auto& pkt = st.packets[p];
        bool noData;

        if (is48) {
            noData = ((calcState_.phase480++ & (SYT_INTERVAL - 1)) == (SYT_INTERVAL - 1));
        } else {
            // 44.1 kHz blocking
            noData = (calcState_.sytOffset >= TICKS_PER_CYCLE);
            // advance offset and phase
            if (calcState_.sytOffset < TICKS_PER_CYCLE) {
                uint32_t inc = BASE_INC_441;
                uint32_t idx = calcState_.sytPhase % 13;
                if ((idx && !(idx & 3)) || (calcState_.sytPhase == PHASE_MOD - 1)) {
                     ++inc;                // ← ensures the phase-146 +1
                }
                // advance the offset
                calcState_.sytOffset += inc;
            } else {
                calcState_.sytOffset -= TICKS_PER_CYCLE;
            }
            if (++calcState_.sytPhase >= PHASE_MOD) calcState_.sytPhase = 0;
        }

        // --- prepare header ---
        auto& H = pkt.header;
        H.sid_byte       = nodeID_;
        H.dbs            = 2;
        H.fn_qpc_sph_rsv = 0;
        H.fmt_eoh1       = CIP::kFmtEohValue;
        H.fdf            = is48 ? CIP::kFDF_48k : CIP::kFDF_44k1;

        const uint8_t blocksPerPkt = SYT_INTERVAL;
        if (noData) {
            H.syt            = CIP::kSytNoData;
            pkt.isNoData     = true;
            // NO-DATA packets DO increment DBC
            calcState_.dbc = (calcState_.dbc + blocksPerPkt) & 0xFF;
            pkt.dbcIncrement = blocksPerPkt;
            calcState_.prevWasNoData = true;
        } else {
            pkt.isNoData = false;
            if (calcState_.prevWasNoData) {
                // First DATA packet after NO-DATA: DO NOT increment DBC
                pkt.dbcIncrement = 0;
                calcState_.prevWasNoData = false;
            } else {
                // Normal DATA packet: increment DBC
                calcState_.dbc = (calcState_.dbc + blocksPerPkt) & 0xFF;
                pkt.dbcIncrement = blocksPerPkt;
            }
            H.syt = CIP::makeBigEndianSyt(static_cast<uint16_t>(calcState_.sytOffset));
        }
        H.dbc = calcState_.dbc;
    }

    st.finalDbc = calcState_.dbc;
    st.packetCount = config_.packetsPerGroup;
    st.preparedAtTime.store(mach_absolute_time(), std::memory_order_relaxed);
    
    // ─── PHASE 2: Publish Update ───
    std::atomic_thread_fence(std::memory_order_release);
    st.version.store(currentVersion + 2, std::memory_order_release);  // Mark as ready (even)
    
    nextGroup_.fetch_add(1, std::memory_order_relaxed);

    uint64_t dt = mach_absolute_time() - t0;
    perfStats_.totalCalcs.fetch_add(1);
    if (dt > 100000) perfStats_.slowCalcs.fetch_add(1);
    uint64_t mx = perfStats_.maxNs.load();
    while (dt > mx && !perfStats_.maxNs.compare_exchange_weak(mx, dt));
}

std::chrono::microseconds CIPPreCalculator::getSleepDuration() const {
    uint64_t p = nextGroup_.load(std::memory_order_relaxed);
    uint64_t c = lastConsumed_.load(std::memory_order_relaxed);
    
    // Calculate available buffer slots using absolute counters
    uint64_t available = p - c;  // Both are absolute counters now
    
    if (available >= kBufferDepth - 1) {
        // Buffer nearly full - longer sleep
        return std::chrono::microseconds(200);
    } else if (available >= kBufferDepth / 2) {
        // Comfortable buffer - medium sleep  
        return std::chrono::microseconds(50);
    } else {
        // Buffer getting low - very short sleep
        return std::chrono::microseconds(5);
    }
}

void CIPPreCalculator::configureCPUAffinity() {
    // Set thread affinity first
    thread_affinity_policy_data_t pol{1};
    thread_policy_set(mach_thread_self(), THREAD_AFFINITY_POLICY,
                      (thread_policy_t)&pol, THREAD_AFFINITY_POLICY_COUNT);

    // Calculate timing based on actual FireWire cycle timing
    // FireWire bus runs at 8kHz = 125µs per cycle
    // With packetsPerGroup typically 8, we get ~1ms between DCL callbacks
    uint32_t cycleTimeNs = 125000; // 125µs in nanoseconds
    uint32_t callbackPeriodNs = cycleTimeNs * config_.packetsPerGroup;
    
    thread_time_constraint_policy_data_t ttcPolicy;
    ttcPolicy.period      = callbackPeriodNs;           // Match DCL callback period
    ttcPolicy.computation = callbackPeriodNs / 8;       // ~12.5% of period for computation
    ttcPolicy.constraint  = callbackPeriodNs / 2;       // Must complete within 50% of period
    ttcPolicy.preemptible = 1;                          // Allow DCL callbacks to preempt
    
    kern_return_t result = thread_policy_set(mach_thread_self(),
                                            THREAD_TIME_CONSTRAINT_POLICY,
                                            (thread_policy_t)&ttcPolicy,
                                            THREAD_TIME_CONSTRAINT_POLICY_COUNT);
    if (result != KERN_SUCCESS) {
        os_log_error(OS_LOG_DEFAULT, "Failed to set real-time policy: %d", result);
    } else {
        os_log(OS_LOG_DEFAULT, "CIPPreCalculator: Set RT policy: period=%uµs, compute=%uµs, constraint=%uµs", 
               ttcPolicy.period/1000, ttcPolicy.computation/1000, ttcPolicy.constraint/1000);
    }

    // Also set precedence for additional priority boost within the policy
    thread_precedence_policy_data_t pre{10}; // Higher than default
    thread_policy_set(mach_thread_self(), THREAD_PRECEDENCE_POLICY,
                      (thread_policy_t)&pre, THREAD_PRECEDENCE_POLICY_COUNT);
}

bool CIPPreCalculator::emergencyCalculateCIP(CIPHeader* H, uint8_t idx) {
    os_log(OS_LOG_DEFAULT, "EMERGENCY CIP pkt %u", idx);
    const bool is48 = (config_.sampleRate == 48000.0);
    bool noData;
    if (is48) {
        noData = ((calcState_.phase480++ & (SYT_INTERVAL - 1)) == (SYT_INTERVAL - 1));
    } else {
        noData = (calcState_.sytOffset >= TICKS_PER_CYCLE);
        if (calcState_.sytOffset < TICKS_PER_CYCLE) {
            uint32_t inc = BASE_INC_441;
            uint32_t idx2 = calcState_.sytPhase % 13;
            if ((idx2 && !(idx2 & 3)) || (calcState_.sytPhase == PHASE_MOD - 1)) ++inc;
            calcState_.sytOffset += inc;
        } else {
            calcState_.sytOffset -= TICKS_PER_CYCLE;
        }
        if (++calcState_.sytPhase >= PHASE_MOD) calcState_.sytPhase = 0;
    }

    H->sid_byte = nodeID_;
    H->dbs      = 2;
    H->fmt_eoh1 = CIP::kFmtEohValue;
    H->fdf      = is48 ? CIP::kFDF_48k : CIP::kFDF_44k1;
    
    const uint8_t blocksPerPkt = SYT_INTERVAL;
    if (noData) {
        H->syt = CIP::kSytNoData;
        // NO-DATA packets DO increment DBC
        calcState_.dbc = (calcState_.dbc + blocksPerPkt) & 0xFF;
        calcState_.prevWasNoData = true;
    } else {
        if (calcState_.prevWasNoData) {
            // First DATA packet after NO-DATA: DO NOT increment DBC
            calcState_.prevWasNoData = false;
        } else {
            // Normal DATA packet: increment DBC
            calcState_.dbc = (calcState_.dbc + blocksPerPkt) & 0xFF;
        }
        H->syt = CIP::makeBigEndianSyt(static_cast<uint16_t>(calcState_.sytOffset));
    }
    H->dbc = calcState_.dbc;
    return noData;
}
