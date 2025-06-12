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
        g.ready.store(false);
        g.preparedAtTime.store(0);
        g.groupNumber.store(UINT32_MAX);  // Initialize group number
        g.finalDbc = 0;
        g.packetCount = config.packetsPerGroup;
    }
    nextGroup_.store(0);
    lastConsumed_.store(UINT32_MAX);  // Start with "no groups consumed"

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
    
    // Quick check - if not ready, return nullptr
    if (!state->ready.load(std::memory_order_acquire)) {
        return nullptr;
    }
    
    // Verify group number matches (both now use modulo config_.numGroups)
    uint32_t actualGroup = state->groupNumber.load(std::memory_order_relaxed);
    if (actualGroup != requestedGroup) {
        return nullptr;  // Group mismatch
    }
    
    return state;
}

void CIPPreCalculator::markGroupConsumed(uint32_t i) {
    lastConsumed_.store(i, std::memory_order_relaxed);
}

void CIPPreCalculator::forceSync(uint8_t dbc, bool prevWasNoData) {
    std::lock_guard<std::mutex> lock(syncMutex_);
    calcState_.dbc = dbc;
    calcState_.prevWasNoData = prevWasNoData;
    
    // Reset all group states to prevent stale data
    for (auto& g : groupStates_) {
        g.ready.store(false, std::memory_order_relaxed);
        g.groupNumber.store(UINT32_MAX, std::memory_order_relaxed);
    }
    
    // Reset nextGroup to start fresh
    nextGroup_.store(0);
    lastConsumed_.store(UINT32_MAX);
    
    os_log(OS_LOG_DEFAULT, "CIPPreCalculator force sync: DBC=%u, prevWasNoData=%d", dbc, prevWasNoData);
}

void CIPPreCalculator::calculateNextGroup() {
    uint64_t t0 = mach_absolute_time();
    
    // Use modulo numbering to match transmitter
    uint32_t absoluteGroup = nextGroup_.load(std::memory_order_relaxed);
    uint32_t targetGroup = absoluteGroup % config_.numGroups;  // KEY FIX
    uint32_t bufferSlot = targetGroup % kBufferDepth;
    auto& st = groupStates_[bufferSlot];
    
    std::lock_guard<std::mutex> lock(syncMutex_);

    // Check if this slot already has fresh data for the target group
    if (st.ready.load(std::memory_order_acquire)) {
        uint32_t existingGroup = st.groupNumber.load(std::memory_order_relaxed);
        if (existingGroup == targetGroup) {
            uint64_t age = mach_absolute_time() - st.preparedAtTime.load();
            if (age < kMaxPreparedAge) return;  // Still fresh
        }
    }

    // Flow control using modulo numbering
    uint32_t lastConsumed = lastConsumed_.load(std::memory_order_relaxed);
    if (lastConsumed != UINT32_MAX) {
        uint32_t groupsAhead;
        if (targetGroup >= lastConsumed) {
            groupsAhead = targetGroup - lastConsumed;
        } else {
            // Handle wrap-around: target=2, consumed=14 in 16-group system
            groupsAhead = (config_.numGroups - lastConsumed) + targetGroup;
        }
        if (groupsAhead >= kBufferDepth) return;  // Too far ahead
    }

    // Store the target group number (modulo config_.numGroups)
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
    st.preparedAtTime.store(mach_absolute_time());
    std::atomic_thread_fence(std::memory_order_release);
    st.ready.store(true, std::memory_order_release);
    nextGroup_.fetch_add(1, std::memory_order_relaxed);

    uint64_t dt = mach_absolute_time() - t0;
    perfStats_.totalCalcs.fetch_add(1);
    if (dt > 100000) perfStats_.slowCalcs.fetch_add(1);
    uint64_t mx = perfStats_.maxNs.load();
    while (dt > mx && !perfStats_.maxNs.compare_exchange_weak(mx, dt));
}

std::chrono::microseconds CIPPreCalculator::getSleepDuration() const {
    uint32_t p = nextGroup_.load(), c = lastConsumed_.load();
    if (p > c + kBufferDepth - 1) return std::chrono::microseconds(500);
    if (p > c + 2)           return std::chrono::microseconds(100);
    return std::chrono::microseconds(10);
}

void CIPPreCalculator::configureCPUAffinity() {
    thread_affinity_policy_data_t pol{1};
    thread_policy_set(mach_thread_self(), THREAD_AFFINITY_POLICY,
                      (thread_policy_t)&pol, THREAD_AFFINITY_POLICY_COUNT);
    thread_precedence_policy_data_t pre{0};
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
