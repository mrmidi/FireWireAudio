// TODO: REMOVE

// #pragma once
// #include <cstdint>
// #include <memory>
// #include "FWA/Error.h"
// #include <expected>
// #include <spdlog/spdlog.h>

// namespace FWA {
// namespace Isoch {

// /**
//  * @brief Constants for AMDTP timing and synchronization
//  */
// constexpr uint32_t TICKS_PER_CYCLE = 3072;      ///< 125 microseconds (1 / 8000)
// constexpr uint32_t CYCLES_PER_SECOND = 8000;    ///< 8 kHz
// constexpr uint32_t TICKS_PER_SECOND = TICKS_PER_CYCLE * CYCLES_PER_SECOND;
// constexpr uint32_t BASE_TICKS_48K = 1024;       ///< For 48kHz
// constexpr uint32_t BASE_TICKS_44K = 1386;       ///< For 44.1kHz
// constexpr uint32_t SYT_PHASE_MOD = 147;         ///< For 44.1kHz phase calculation
// constexpr uint32_t SYT_PHASE_RESET = 147;       ///< Reset phase after this many cycles

// /**
//  * @brief IEC61883 format constants for AMDTP
//  */
// constexpr uint8_t IEC61883_FMT_AMDTP = 0x10;          ///< AMDTP format identifier
// constexpr uint8_t IEC61883_FDF_NODATA = 0xFF;         ///< No data format
// constexpr uint8_t IEC61883_FDF_SFC_44K1HZ = 0x01;     ///< 44.1kHz sample rate
// constexpr uint8_t IEC61883_FDF_SFC_48KHZ = 0x02;      ///< 48kHz sample rate

// /**
//  * @brief Control and Information Protocol (CIP) header structure
//  */
// struct CIPHeader {
//     uint8_t sid;    ///< Source ID
//     uint8_t dbs;    ///< Data block size
//     uint8_t fmt;    ///< Format
//     uint8_t fdf;    ///< Format dependent field
//     uint16_t syt;   ///< Synchronization timestamp
//     uint8_t dbc;    ///< Data block count
//     uint8_t fn;     ///< Fraction number
//     uint8_t qpc;    ///< Quadlet padding count
//     uint8_t sph;    ///< Source packet header
// };

// /**
//  * @brief Parameters for CIP header updates
//  */
// struct CIPUpdateParams {
//     bool isNoData{true};      ///< Whether this packet contains data
//     bool wasNoData{true};     ///< Whether previous packet contained data
//     uint32_t dbc{0};         ///< Data block count
//     uint32_t syt{0xFFFF};    ///< Synchronization timestamp value
// };

// /**
//  * @brief Handles CIP header calculations and management
//  */
// class CIPHeaderHandler {
// public:
//     /**
//      * @brief Construct a new CIPHeaderHandler object
//      * @param logger Shared pointer to logger instance
//      */
//     explicit CIPHeaderHandler(std::shared_ptr<spdlog::logger> logger);
    
//     /**
//      * @brief Initialize state with current FireWire cycle time
//      * @param currentFireWireCycleTime Current cycle time from FireWire bus
//      * @return Success or error status
//      */
//     std::expected<void, IOKitError> initialize(uint32_t currentFireWireCycleTime);
    
//     /**
//      * @brief Calculate packet parameters for current cycle
//      * @param segment Current segment number
//      * @param cycle Current cycle number
//      * @return Packet parameters or error
//      */
//     std::expected<CIPUpdateParams, IOKitError> calculatePacketParams(uint32_t segment, uint32_t cycle);
    
//     /**
//      * @brief Update CIP header with current parameters
//      * @param header Pointer to CIP header to update
//      * @param nodeID Current node ID
//      * @param params Parameters to use for update
//      */
//     void updateCIPHeader(CIPHeader* header, uint16_t nodeID, const CIPUpdateParams& params) noexcept;
    
//     /**
//      * @brief Set the sample rate for timing calculations
//      * @param newRate New sample rate (44100 or 48000)
//      */
//     void setSampleRate(uint32_t newRate) noexcept;
    
//     /**
//      * @brief Check if first callback has occurred
//      * @return bool True if first callback has occurred
//      */
//     bool isFirstCallbackOccurred() const noexcept { return firstCallbackOccurred_; }
    
//     /**
//      * @brief Set first callback occurred flag
//      * @param value New value for the flag
//      */
//     void setFirstCallbackOccurred(bool value) noexcept { firstCallbackOccurred_ = value; }

// private:
//     std::shared_ptr<spdlog::logger> logger_;
    
//     uint32_t sytOffset_{0};         ///< Current SYT offset within cycle
//     uint32_t sytPhase_{0};          ///< Phase for 44.1kHz calculation
//     uint8_t dbcCount_{0};           ///< Data block counter
//     bool wasNoData_{true};          ///< Previous packet state
//     bool firstCallbackOccurred_{false}; ///< First DCL callback occurred
//     uint32_t sampleRate_{48000};    ///< Current sample rate
//     uint32_t baseTicks_{BASE_TICKS_48K}; ///< Ticks per packet
    
//     /**
//      * @brief Initialize transfer delay based on current cycle time
//      * @param currentFireWireCycleTime Current FireWire cycle time
//      */
//     void initializeTransferDelay(uint32_t currentFireWireCycleTime) noexcept;

//     /**
//      * @brief Handle 44.1kHz mode timing calculations
//      */
//     void handle44100Mode() noexcept;

//     /**
//      * @brief Handle 48kHz mode timing calculations
//      */
//     void handle48000Mode() noexcept;

//     /**
//      * @brief Update SYT offset for timing calculations
//      */
//     void updateSYTOffset() noexcept;
// };

// } // namespace Isoch
// } // namespace FWA