#include "Isoch/core/IsochPacketProcessor.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/fmt/bin_to_hex.h>
#include <vector>
#include <cstring> // For memcpy
#include <CoreServices/CoreServices.h> // For endian conversion functions

// Define bytes per sample for clarity
constexpr size_t BYTES_PER_AM824_SAMPLE = 4;
// Define max value for 24-bit signed int normalization
constexpr float MAX_24BIT_SIGNED_FLOAT = 8388607.0f; // 2^23 - 1
    // Define common header sizes for reference
constexpr size_t kIsochHeaderSize = 4;
constexpr size_t kCIPHeaderSize = 8;

namespace FWA {
namespace Isoch {

IsochPacketProcessor::IsochPacketProcessor(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)),
      expectedDBC_(0), // Initialize expected DBC
      dbcInitialized_(false),
      currentAbsSampleIndex_(0), // Start absolute count at 0
      sampleIndexInitialized_(false),
      lastPacketNumDataBlocks_(0), // Initialize correctly
      lastPacketWasNoData_(false)  // Assume first packet is not preceded by NO_DATA
{
    if (logger_) {
        logger_->debug("IsochPacketProcessor created");
    }
}

void IsochPacketProcessor::setProcessedDataCallback(ProcessedDataCallback callback, void* refCon) {
    processedDataCallback_ = callback;
    processedDataCallbackRefCon_ = refCon;
}

void IsochPacketProcessor::setOverrunCallback(OverrunCallback callback, void* refCon) {
    overrunCallback_ = callback;
    overrunCallbackRefCon_ = refCon;
}

std::expected<void, IOKitError> IsochPacketProcessor::processPacket(
    uint32_t groupIndex,
    uint32_t packetIndexInGroup,
    const uint8_t* isochHeader,
    const uint8_t* cipHeader,
    const uint8_t* packetData,
    size_t packetDataLength,
    uint32_t fwTimestamp)
{
    if (!isochHeader || !cipHeader || !packetData) {
        return std::unexpected(IOKitError::BadArgument);
    }

    if (logger_ && logger_->should_log(spdlog::level::trace)) { // Reduce log spam slightly
        // --- Log Raw Headers ---
        std::vector<uint8_t> raw_iso(kIsochHeaderSize);
        std::memcpy(raw_iso.data(), isochHeader, kIsochHeaderSize);
        logger_->trace("Packet G:{} P:{} - Raw Isoch Header @ {:p}: {}",
                      groupIndex, packetIndexInGroup, (void*)isochHeader, spdlog::to_hex(raw_iso));

        std::vector<uint8_t> raw_cip(kCIPHeaderSize);
        std::memcpy(raw_cip.data(), cipHeader, kCIPHeaderSize);
        logger_->trace("Packet G:{} P:{} - Raw CIP Header @ {:p}: {}",
                      groupIndex, packetIndexInGroup, (void*)cipHeader, spdlog::to_hex(raw_cip));
        logger_->trace("Packet G:{} P:{} - Packet Data @ {:p}, Length: {}",
                      groupIndex, packetIndexInGroup, (void*)packetData, packetDataLength);
        logger_->trace("Packet G:{} P:{} - FW Timestamp: {:#010x} ({})",
                      groupIndex, packetIndexInGroup, fwTimestamp, fwTimestamp);
    }

    // --- 1. Parse Isoch Header ---
    uint32_t isochHeaderVal = 0;
    std::memcpy(&isochHeaderVal, isochHeader, sizeof(isochHeaderVal));
    isochHeaderVal = OSSwapBigToHostInt32(isochHeaderVal); // Use CoreServices
    uint16_t dataLenFromIsoch = (isochHeaderVal >> 16) & 0xFFFF;
    uint8_t tag = (isochHeaderVal >> 14) & 0x03;
    uint8_t channel = (isochHeaderVal >> 8) & 0x3F;
    uint8_t tcode = (isochHeaderVal >> 4) & 0x0F;
    uint8_t sy = isochHeaderVal & 0x0F;

    if (logger_) logger_->trace("Packet G:{} P:{} - Parsed Isoch: Len={}, Tag={}, Ch={}, TCode={:#x}, Sy={}",
                               groupIndex, packetIndexInGroup, dataLenFromIsoch, tag, channel, tcode, sy);

    // --- 2. Parse CIP Header ---
    uint32_t cipQuadlets[2];
    std::memcpy(cipQuadlets, cipHeader, sizeof(cipQuadlets));
    uint32_t cip0 = OSSwapBigToHostInt32(cipQuadlets[0]);
    uint32_t cip1 = OSSwapBigToHostInt32(cipQuadlets[1]);

    uint8_t sid = (cip0 >> 24) & 0x3F; // Source ID
    uint8_t dbs = (cip0 >> 16) & 0xFF; // Data Block Size (quadlets)
    uint8_t dbc = cip0 & 0xFF;         // Data Block Counter
    uint8_t fmt = (cip1 >> 24) & 0x3F; // Format ID
    uint8_t fdf = (cip1 >> 16) & 0xFF; // Format Dependent Field
    uint16_t syt = cip1 & 0xFFFF;      // SYT field

    if (logger_) logger_->trace("Packet G:{} P:{} - Parsed CIP: SID={}, DBS={}, DBC={}, FMT={:#x}, FDF={:#x}, SYT={:#06x}",
                               groupIndex, packetIndexInGroup, sid, dbs, dbc, fmt, fdf, syt);

    // --- 3. Check for AMDTP format ---
    const uint8_t EXPECTED_FMT = 0x10;
    if (fmt != EXPECTED_FMT) {
        if (logger_) {
            std::vector<uint8_t> cip_bytes(8);
            std::memcpy(cip_bytes.data(), cipHeader, 8);
            logger_->warn("Packet G:{} P:{} - Unexpected CIP FMT: {:#04x} (Expected {:#04x}). Full CIP Header (BE): {}",
                          groupIndex, packetIndexInGroup, fmt, EXPECTED_FMT, spdlog::to_hex(cip_bytes));
        }
        return {}; // Skip non-AMDTP packets
    }

    // --- 4. Calculate block/sample info ---
    uint32_t dbs_bytes = dbs * 4;
    uint32_t samplesPerBlock = 0; // AM824 samples per CIP Data Block
    uint32_t numDataBlocks = 0;   // CIP Data Blocks in this FW packet

    if (dbs == 0 && fdf != 0xFF) {
        // If DBS is 0 but it's not a NO_DATA packet, something is wrong.
        if (logger_) logger_->warn("Packet G:{} P:{} - DBS is 0 but FDF is not NO_DATA ({:#x})!", 
                                  groupIndex, packetIndexInGroup, fdf);
        // Assume 0 blocks/samples and proceed to update DBC state based on this packet's DBC
        dbs_bytes = 0;
        samplesPerBlock = 0;
        numDataBlocks = 0; // Explicitly 0 blocks
    } else if (dbs > 0) {
        if ((dbs_bytes % BYTES_PER_AM824_SAMPLE) != 0) {
            if (logger_) logger_->error("Packet G:{} P:{} - DBS bytes ({}) not multiple of AM824 sample size ({})!", 
                                       groupIndex, packetIndexInGroup, dbs_bytes, BYTES_PER_AM824_SAMPLE);
            return std::unexpected(IOKitError::BadArgument); // Cannot process this packet
        }
        samplesPerBlock = dbs_bytes / BYTES_PER_AM824_SAMPLE;

        if (packetDataLength > 0 && dbs_bytes > 0) {
            if ((packetDataLength % dbs_bytes) != 0) {
                if (logger_) logger_->warn("Packet G:{} P:{} - Packet data length ({}) not multiple of DBS bytes ({})! Processing only full blocks.", 
                                          groupIndex, packetIndexInGroup, packetDataLength, dbs_bytes);
                // Only process full blocks contained within the packet data length
                numDataBlocks = packetDataLength / dbs_bytes;
            } else {
                numDataBlocks = packetDataLength / dbs_bytes;
            }
        } else {
            numDataBlocks = 0; // No data or zero DBS
        }
    } // else dbs is 0 and FDF is NO_DATA, numDataBlocks remains 0

    uint32_t totalSamplesInPacket = numDataBlocks * samplesPerBlock;

    // --- ADD EXTRA LOGGING HERE ---
    // if (logger_) logger_->debug("Packet G:{} P:{} - CALC: dbs={}, dbs_bytes={}, packetDataLength={}, samplesPerBlock={}, ***numDataBlocks={}***",
    //                            groupIndex, packetIndexInGroup, dbs, dbs_bytes, packetDataLength, samplesPerBlock, numDataBlocks);
    // // --- END EXTRA LOGGING ---

    // if (logger_) logger_->trace("Packet G:{} P:{} - Calculated: DBS_Bytes={}, SamplesPerBlock={}, NumDataBlocks={}, TotalSamples={}",
    //                            groupIndex, packetIndexInGroup, dbs_bytes, samplesPerBlock, numDataBlocks, totalSamplesInPacket);

    // --- State for callback ---
    std::vector<ProcessedSample> packetSamples;
    packetSamples.reserve(totalSamplesInPacket / 2); // Reserve for stereo frames
    uint64_t packetStartAbsSampleIndex = 0; // Will be set later
    bool discontinuityDetected = false;

    // Determine if current packet is NO_DATA
    bool currentPacketIsNoData = (fdf == 0xFF);

    // --- 5. Handle Packet Processing ---
    if (!dbcInitialized_) {
        // --- First Packet Initialization ---
        if (currentPacketIsNoData) {
            if (logger_) logger_->debug("First packet is NO_DATA, waiting for data to init DBC.");
            // Don't initialize state yet
            lastPacketWasNoData_ = true; // Mark that this packet was NO_DATA
        } else { // First packet is DATA
            expectedDBC_ = dbc;
            lastPacketNumDataBlocks_ = numDataBlocks; // Store blocks from THIS packet
            lastPacketWasNoData_ = false;             // THIS packet was DATA
            dbcInitialized_ = true;
            if (logger_) logger_->info("Packet G:{} P:{} - Initialized DBC tracking. First DBC={}, Blocks={}, Expecting next after {} blocks.",
                                      groupIndex, packetIndexInGroup, dbc, numDataBlocks, numDataBlocks);
            
            // Initialize Sample Index if it's the first DATA packet
            packetStartAbsSampleIndex = 0; // Since it's the first DATA packet
            if (!sampleIndexInitialized_) {
                currentAbsSampleIndex_ = 0;
                sampleIndexInitialized_ = true;
                if (logger_) logger_->info("Packet G:{} P:{} - Initialized absolute sample index to 0", groupIndex, packetIndexInGroup);
                
                // Initialize PLL here using fwTimestamp and SYT (if valid)
                if (processedDataCallback_ && syt != 0xFFFF) { // Only if callback set and SYT valid
                    PacketTimingInfo initTiming = { 
                        .fwTimestamp = fwTimestamp,
                        .syt = syt,
                        .firstDBC = dbc, 
                        .numSamplesInPacket = 0, // Pass 0 samples for init
                        .fdf = fdf,
                        .sfc = getSFCFromFDF(fdf),
                        .firstAbsSampleIndex = 0
                    };
                    std::vector<ProcessedSample> emptySamples;
                    processedDataCallback_(emptySamples, initTiming, processedDataCallbackRefCon_); // Signal for PLL init
                }
            }
        }
    } else {
        // --- Subsequent Packet Processing ---
        uint8_t nextExpectedDBC;

        // --- Calculate Correct Expectation ---
        if (lastPacketWasNoData_) {
            // If previous was NO_DATA, expect the SAME DBC it carried
            nextExpectedDBC = expectedDBC_;
            if (logger_) logger_->trace("Packet G:{} P:{} - Expecting SAME DBC {} (after NO_DATA)", 
                                       groupIndex, packetIndexInGroup, nextExpectedDBC);
        } else {
            // If previous was DATA, expect DBC + blocks from previous DATA packet
            nextExpectedDBC = (expectedDBC_ + lastPacketNumDataBlocks_) & 0xFF;
            if (logger_) logger_->trace("Packet G:{} P:{} - Expecting DBC {} + {} = {} (after DATA)", 
                                       groupIndex, packetIndexInGroup, expectedDBC_, lastPacketNumDataBlocks_, nextExpectedDBC);
        }

        // --- Compare Received DBC with Expectation ---
        if (dbc != nextExpectedDBC) {
            // --- DISCONTINUITY ---
            int8_t diff_s8 = static_cast<int8_t>(dbc - nextExpectedDBC);
            if (diff_s8 != -8) {
                // TODO: fix dbc continuity check
                // if (logger_) logger_->warn("Packet G:{} P:{} ({}) - DBC DISCONTINUITY! PrevDBC={}, PrevWasNoData={}, PrevBlocks={}, Expected {}, Got {}. Diff={}",
                //     groupIndex, packetIndexInGroup, (currentPacketIsNoData?"NO_DATA":"DATA"),
                //     expectedDBC_, lastPacketWasNoData_, lastPacketNumDataBlocks_, nextExpectedDBC, dbc, (int)diff_s8);

            }

            // --- Adjust sample index FORWARD only if diff > 0 ---
            if (sampleIndexInitialized_ && !currentPacketIsNoData) { // Only adjust for DATA packets after discontinuity
                if (diff_s8 > 0 && diff_s8 < 128) {
                    if (samplesPerBlock > 0) {
                        uint64_t lostSamples = static_cast<uint64_t>(diff_s8) * samplesPerBlock;
                        currentAbsSampleIndex_ += lostSamples / 2;
                        // TODO: fix dbc continuity check
                        // if (logger_) logger_->warn("  Adjusted sample index FORWARD by {} frames (~{} blocks of {} samples)", 
                        //                           lostSamples / 2, diff_s8, samplesPerBlock);
                    } else {
                        if (logger_) logger_->warn("  Cannot adjust sample index: samplesPerBlock is 0 for this packet.");
                    }
                } else {
                    // if (logger_) logger_->warn("  Negative or large DBC jump ({}), not adjusting sample index.", (int)diff_s8);
                }
            }

            // --- RESYNC State based on CURRENT packet ---
            expectedDBC_ = dbc;                       // Base NEXT expectation on THIS packet's DBC
            lastPacketNumDataBlocks_ = numDataBlocks;  // Use blocks from THIS packet
            lastPacketWasNoData_ = currentPacketIsNoData; // Store type of THIS packet
            discontinuityDetected = true;
            // if (logger_) logger_->debug("Packet G:{} P:{} - RESYNC state: Next expected after PrevDBC={}, PrevBlocks={}, PrevWasNoData={}",
            //                             groupIndex, packetIndexInGroup, expectedDBC_, lastPacketNumDataBlocks_, lastPacketWasNoData_);

        } else {
            // --- DBC OK ---
            if (logger_) logger_->trace("Packet G:{} P:{} ({}) - DBC OK (Expected {})",
                                        groupIndex, packetIndexInGroup, (currentPacketIsNoData?"NO_DATA":"DATA"), nextExpectedDBC);

            // --- Update state based on CURRENT packet for NEXT check ---
            expectedDBC_ = dbc;                        // Base NEXT expectation on THIS packet's DBC
            lastPacketNumDataBlocks_ = numDataBlocks;   // Use blocks from THIS packet
            lastPacketWasNoData_ = currentPacketIsNoData; // Store type of THIS packet
            if (logger_) logger_->trace("Packet G:{} P:{} - Updated state for next check: Next expected after PrevDBC={}, PrevBlocks={}, PrevWasNoData={}",
                                        groupIndex, packetIndexInGroup, expectedDBC_, lastPacketNumDataBlocks_, lastPacketWasNoData_);
        }

        // Set start sample index for potential processing
        packetStartAbsSampleIndex = currentAbsSampleIndex_;

        // Initialize sample index if it hadn't been initialized yet and this is the first DATA packet
        if (!sampleIndexInitialized_ && !currentPacketIsNoData) {
            currentAbsSampleIndex_ = 0;
            packetStartAbsSampleIndex = 0; // Adjust start index as well
            sampleIndexInitialized_ = true;
            if (logger_) logger_->info("Packet G:{} P:{} - Initialized absolute sample index to 0 (on first valid data packet after init)", 
                                      groupIndex, packetIndexInGroup);
                
            // Initialize PLL here using fwTimestamp and SYT (if valid)
            if (processedDataCallback_ && syt != 0xFFFF) { // Only if callback set and SYT valid
                PacketTimingInfo initTiming = { 
                    .fwTimestamp = fwTimestamp,
                    .syt = syt,
                    .firstDBC = dbc, 
                    .numSamplesInPacket = 0, // Pass 0 samples for init
                    .fdf = fdf,
                    .sfc = getSFCFromFDF(fdf),
                    .firstAbsSampleIndex = 0
                };
                std::vector<ProcessedSample> emptySamples;
                processedDataCallback_(emptySamples, initTiming, processedDataCallbackRefCon_); // Signal for PLL init
            }
        }
    } // End if (Subsequent Packet)

    // --- 6. Process Samples (Only for DATA packets) ---
    if (!currentPacketIsNoData && totalSamplesInPacket > 0) {
        for (uint32_t blockIdx = 0; blockIdx < numDataBlocks; ++blockIdx) {
            const uint8_t* blockPtr = packetData + (blockIdx * dbs_bytes);
            uint8_t currentBlockDBC = (dbc + blockIdx) & 0xFF; // DBC for this specific block

            for (uint32_t sampleIdx = 0; sampleIdx < samplesPerBlock; sampleIdx += 2) { // Stereo pairs
                if ((sampleIdx + 1) >= samplesPerBlock) {
                    if (logger_) logger_->warn("Packet G:{} P:{} B:{} - Odd number of samples in block? Skipping last sample.",
                                             groupIndex, packetIndexInGroup, currentBlockDBC);
                    break; // Avoid reading past end
                }

                uint64_t frameAbsSampleIndex = packetStartAbsSampleIndex + (blockIdx * samplesPerBlock + sampleIdx) / 2;

                // Extract Left Sample (AM824 format)
                uint32_t am824_be_L;
                std::memcpy(&am824_be_L, blockPtr + (sampleIdx * BYTES_PER_AM824_SAMPLE), sizeof(uint32_t));
                uint32_t am824_le_L = OSSwapBigToHostInt32(am824_be_L); // To Host Endian
                int32_t sample24_L = am824_le_L & 0x00FFFFFF;
                if (sample24_L & 0x00800000) { sample24_L |= 0xFF000000; }
                float sampleFloatL = static_cast<float>(sample24_L) / MAX_24BIT_SIGNED_FLOAT;

                // Extract Right Sample
                uint32_t am824_be_R;
                std::memcpy(&am824_be_R, blockPtr + ((sampleIdx + 1) * BYTES_PER_AM824_SAMPLE), sizeof(uint32_t));
                uint32_t am824_le_R = OSSwapBigToHostInt32(am824_be_R);
                int32_t sample24_R = am824_le_R & 0x00FFFFFF;
                if (sample24_R & 0x00800000) { sample24_R |= 0xFF000000; }
                float sampleFloatR = static_cast<float>(sample24_R) / MAX_24BIT_SIGNED_FLOAT;

                packetSamples.emplace_back(sampleFloatL, sampleFloatR, frameAbsSampleIndex);
            }
        }
        // Increment absolute sample counter AFTER processing samples
        currentAbsSampleIndex_ += totalSamplesInPacket / 2;
    } else if (!currentPacketIsNoData) {
        if (logger_) logger_->trace("Packet G:{} P:{} - No samples to process in DATA packet (NumDataBlocks={}, SamplesPerBlock={})",
                                   groupIndex, packetIndexInGroup, numDataBlocks, samplesPerBlock);
    } // No processing needed for NO_DATA packets here

    // --- 7. Prepare Timing Info ---
    PacketTimingInfo timingInfo = {
        .fwTimestamp = fwTimestamp,
        .syt = syt,
        .firstDBC = dbc, // DBC of the first block in *this* packet
        .numSamplesInPacket = totalSamplesInPacket,
        .fdf = fdf,
        .sfc = getSFCFromFDF(fdf),
        .firstAbsSampleIndex = packetStartAbsSampleIndex // Start index for samples in *this* packet
    };

    // --- 8. Send data upstream ---
    if (processedDataCallback_) {
        // Call with samples (even if empty for NO_DATA packets or on discontinuity)
        processedDataCallback_(packetSamples, timingInfo, processedDataCallbackRefCon_);
    } else if (logger_) {
        logger_->warn("Packet G:{} P:{} - No processed data callback set!", groupIndex, packetIndexInGroup);
    }

    return {};
}

// --- Legacy processPacket (Marked Deprecated) ---
std::expected<void, IOKitError> IsochPacketProcessor::processPacket(
    uint32_t segment, uint32_t cycle, const uint8_t* data, size_t length)
{
    // DEPRECATED - This function assumes a combined buffer.
    if (logger_) logger_->warn("Deprecated IsochPacketProcessor::processPacket called!");

    if (!data || length < (IsochBufferManager::kIsochHeaderSize + IsochBufferManager::kCIPHeaderSize)) {
        return std::unexpected(IOKitError::BadArgument);
    }

    const uint8_t* isochHeader = data;
    const uint8_t* cipHeader = data + IsochBufferManager::kIsochHeaderSize;
    const uint8_t* packetData = data + IsochBufferManager::kIsochHeaderSize + IsochBufferManager::kCIPHeaderSize;
    size_t packetDataLength = length - IsochBufferManager::kIsochHeaderSize - IsochBufferManager::kCIPHeaderSize;

    // Cannot get real FW Timestamp here. Pass 0.
    return processPacket(segment, cycle, isochHeader, cipHeader, packetData, packetDataLength, 0);
}

std::expected<void, IOKitError> IsochPacketProcessor::handleOverrun() {
    if (logger_) {
        logger_->error("IsochPacketProcessor::handleOverrun detected - Resetting DBC/SampleIndex state.");
    }
    // Reset tracking state on overrun to force re-sync
    dbcInitialized_ = false;
    sampleIndexInitialized_ = false; // Force re-sync of sample index
    lastPacketNumDataBlocks_ = 0;    // Reset this too
    lastPacketWasNoData_ = false;    // Reset this state too
    currentAbsSampleIndex_ = 0;      // Reset sample index on overrun

    if (overrunCallback_) {
        overrunCallback_(overrunCallbackRefCon_);
    }
    return {};
}

} // namespace Isoch
} // namespace FWA