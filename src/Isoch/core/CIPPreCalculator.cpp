#include "Isoch/core/CIPPreCalculator.hpp"
#include <os/log.h>
#include <mach/thread_policy.h>
#include <CoreServices/CoreServices.h>

//--- optional silent-fill fallback ------------------
#define SILENT_FALLBACK 0

using namespace FWA::Isoch;

CIPPreCalculator::~CIPPreCalculator() {
    stop();
}

void CIPPreCalculator::initialize(const TransmitterConfig& config, uint16_t nodeID) {
    // Safety check: prevent buffer overrun
    if (config.packetsPerGroup > PreCalcGroup::MaxPacketsPerGroup) {
        os_log(OS_LOG_DEFAULT, "FATAL: config.packetsPerGroup=%u > MaxPacketsPerGroup=%u", 
               config.packetsPerGroup, PreCalcGroup::MaxPacketsPerGroup);
        throw std::invalid_argument("packetsPerGroup exceeds PreCalcGroup::MaxPacketsPerGroup");
    }
    
    config_  = config;
    nodeID_  = nodeID & 0x3F;
    calcState_ = {};

    nextGroup_.store(0, std::memory_order_relaxed);

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
    // Legacy method - still needed temporarily for compatibility
    // Will be removed once all consumers use SPSC ring
    return nullptr;
}

void CIPPreCalculator::markGroupConsumed(uint32_t groupIdx) {
    // Legacy method - no longer needed with SPSC ring
    // Ring automatically manages consumption via pop()
}

void CIPPreCalculator::forceSync(uint8_t dbc, bool prevWasNoData) {
    std::lock_guard<std::mutex> lock(syncMutex_);
    calcState_.dbc = dbc;
    calcState_.prevWasNoData = prevWasNoData;
    
    // With SPSC ring, just reset the state - no need to clear old data
    // Ring will naturally flush as new groups are calculated
    nextGroup_.store(0, std::memory_order_relaxed);
    
    os_log(OS_LOG_DEFAULT, "CIPPreCalculator force sync: DBC=%u, prevWasNoData=%d", dbc, prevWasNoData);
}

void CIPPreCalculator::calculateNextGroup() {
    uint64_t t0 = mach_absolute_time();
    
    // Check if ring is full BEFORE acquiring mutex to avoid deadlock
    if (groupRing_.full()) {
        std::this_thread::sleep_for(std::chrono::microseconds(1));
        return;
    }
    
    std::lock_guard<std::mutex> lock(syncMutex_);

    // Create a new group
    PreCalcGroup grp;
    
    const bool is48 = (config_.sampleRate == 48000.0);
    for (uint32_t p = 0; p < config_.packetsPerGroup; ++p) {
        auto& pkt = grp.packets[p];
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

        const uint8_t blocksPerPkt = SYT_INTERVAL;   // == 8

        uint8_t currentDbc = calcState_.dbc; // snapshot for this header

        if (noData) {
            /* ----------  NO-DATA branch  ---------- */
            H.fdf = is48 ? CIP::kFDF_48k : CIP::kFDF_44k1;
            H.syt = CIP::kSytNoData;
            H.dbc = currentDbc;

            pkt.isNoData     = true;
            pkt.dbcIncrement = blocksPerPkt;     // observer sees +8

            /* ✱✱  DO NOT bump calcState_.dbc here  ✱✱ */
            calcState_.prevWasNoData = true;
        } else {
            /* ----------  DATA branch  ---------- */
            H.fdf = is48 ? CIP::kFDF_48k : CIP::kFDF_44k1;
            H.syt = CIP::makeBigEndianSyt(static_cast<uint16_t>(calcState_.sytOffset));
            H.dbc = currentDbc;

            pkt.isNoData = false;

            if (calcState_.prevWasNoData) {
                /* first DATA after NO-DATA : header same DBC */
                pkt.dbcIncrement = 0;
            } else {
                /* DATA after DATA : header already shows +8 */
                pkt.dbcIncrement = blocksPerPkt;
            }

            /* ✱✱  ALWAYS bump after writing a DATA header  ✱✱ */
            calcState_.dbc = (calcState_.dbc + blocksPerPkt) & 0xFF;
            calcState_.prevWasNoData = false;
        }
        
#if SILENT_FALLBACK
        // alternative to NO-DATA: same header + all-zero payload
        // (uncomment if you prefer to avoid 8-byte packets)
        //
        // if (noData) {
        //     uint8_t oldDbc = calcState_.dbc;
        //     H.fdf = CIP::kFDF_48k; // Use sample rate, not 0xFF
        //     H.dbc = oldDbc;
        //     // zeroFillPayload(kAudioDataBytes); // Would be handled in transmitter
        //     pkt.isNoData = false; // Mark as DATA packet with silent payload
        //     calcState_.prevWasNoData = false;
        // }
#endif
    }

    // Store final state in group
    grp.finalDbc = calcState_.dbc;
    grp.finalWasNoData = calcState_.prevWasNoData;
    
    // Try to push to ring - should succeed since we checked full() above
    if (!groupRing_.push(grp)) {
        // Extremely rare: ring became full between check and push
        return;  // Skip this calculation cycle
    }
    
    nextGroup_.fetch_add(1, std::memory_order_relaxed);

    uint64_t dt = mach_absolute_time() - t0;
    perfStats_.totalCalcs.fetch_add(1);
    if (dt > 100000) perfStats_.slowCalcs.fetch_add(1);
    uint64_t mx = perfStats_.maxNs.load();
    while (dt > mx && !perfStats_.maxNs.compare_exchange_weak(mx, dt));
}

std::chrono::microseconds CIPPreCalculator::getSleepDuration() const {
    // Check ring fullness for new SPSC approach
    if (groupRing_.full()) {
        // Ring full - longer sleep
        return std::chrono::microseconds(200);
    } else if (groupRing_.empty()) {
        // Ring empty - very short sleep to fill quickly
        return std::chrono::microseconds(1);
    } else {
        // Ring has some items - medium sleep
        return std::chrono::microseconds(50);
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
    thread_precedence_policy_data_t pre{31}; // Boost to 31 to outrank normal tasks
    thread_policy_set(mach_thread_self(), THREAD_PRECEDENCE_POLICY,
                      (thread_policy_t)&pre, THREAD_PRECEDENCE_POLICY_COUNT);
}

// Global emergency state (thread-local to avoid race conditions)
static thread_local CIPPreCalculator::CalcState emergencyState_{};
static thread_local bool emergencyStateInitialized_ = false;

void CIPPreCalculator::syncEmergencyState() {
    std::lock_guard<std::mutex> lock(syncMutex_);
    emergencyState_ = calcState_;
    emergencyStateInitialized_ = true;
    os_log(OS_LOG_DEFAULT, "Emergency state synchronized: DBC=0x%02X, prevWasNoData=%d", 
           emergencyState_.dbc, emergencyState_.prevWasNoData);
}

bool CIPPreCalculator::emergencyCalculateCIP(CIPHeader* H, uint8_t idx) {
    os_log(OS_LOG_DEFAULT, "EMERGENCY CIP pkt %u", idx);
    
    // CRITICAL FIX: Sync emergency state only if not initialized yet
    if (!emergencyStateInitialized_) {
        syncEmergencyState();
    }
    
    const bool is48 = (config_.sampleRate == 48000.0);
    bool noData;
    if (is48) {
        noData = ((emergencyState_.phase480++ & (SYT_INTERVAL - 1)) == (SYT_INTERVAL - 1));
    } else {
        noData = (emergencyState_.sytOffset >= TICKS_PER_CYCLE);
        if (emergencyState_.sytOffset < TICKS_PER_CYCLE) {
            uint32_t inc = BASE_INC_441;
            uint32_t idx2 = emergencyState_.sytPhase % 13;
            if ((idx2 && !(idx2 & 3)) || (emergencyState_.sytPhase == PHASE_MOD - 1)) ++inc;
            emergencyState_.sytOffset += inc;
        } else {
            emergencyState_.sytOffset -= TICKS_PER_CYCLE;
        }
        if (++emergencyState_.sytPhase >= PHASE_MOD) emergencyState_.sytPhase = 0;
    }

    H->sid_byte = nodeID_;
    H->dbs      = 2;
    H->fmt_eoh1 = CIP::kFmtEohValue;
    H->fdf      = is48 ? CIP::kFDF_48k : CIP::kFDF_44k1;
    
    const uint8_t blocksPerPkt = SYT_INTERVAL;   // == 8

    uint8_t currentDbc = emergencyState_.dbc; // snapshot for this header

    if (noData) {
        /* ----------  NO-DATA branch  ---------- */
        H->fdf = is48 ? CIP::kFDF_48k : CIP::kFDF_44k1;
        H->syt = CIP::kSytNoData;
        H->dbc = currentDbc;

        /* ✱✱  DO NOT bump emergencyState_.dbc here  ✱✱ */
        emergencyState_.prevWasNoData = true;
    } else {
        /* ----------  DATA branch  ---------- */
        H->fdf = is48 ? CIP::kFDF_48k : CIP::kFDF_44k1;
        H->syt = CIP::makeBigEndianSyt(static_cast<uint16_t>(emergencyState_.sytOffset));
        H->dbc = currentDbc;

        if (emergencyState_.prevWasNoData) {
            /* first DATA after NO-DATA : header same DBC */
            // Don't advance DBC
        } else {
            /* DATA after DATA : header already shows +8 */
            // DBC was already advanced for previous packet
        }

        /* ✱✱  ALWAYS bump after writing a DATA header  ✱✱ */
        emergencyState_.dbc = (emergencyState_.dbc + blocksPerPkt) & 0xFF;
        emergencyState_.prevWasNoData = false;
    }
    
    os_log(OS_LOG_DEFAULT, "Emergency packet: DBC=0x%02X, isNoData=%d, nextDBC=0x%02X", 
           currentDbc, noData, emergencyState_.dbc);
    
    return noData;
}
