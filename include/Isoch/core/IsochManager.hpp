#pragma once

#include <memory>
#include <vector>
#include <functional>
#include <expected>
#include <atomic>
#include <mutex>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/firewire/IOFireWireLibIsoch.h>
#include <spdlog/logger.h>
#include "FWA/Error.h"

namespace FWA {
namespace Isoch {

/**
 * @brief Callback function for DCL completion events
 */
using DCLCompleteCallback = void(*)(uint32_t segment, void* refCon);

/**
 * @brief Callback function for DCL overrun events
 */
using DCLOverrunCallback = void(*)(void* refCon);

/**
 * @brief Manager for isoch operations combining DCL and port management
 * 
 * This class handles the FireWire isochronous communication infrastructure
 * including DCL program creation, port management, and channel configuration.
 */
class IsochManager {
public:
    /**
     * @brief Construct a new IsochManager instance
     * 
     * @param logger Logger for diagnostic information
     */
    explicit IsochManager(std::shared_ptr<spdlog::logger> logger);
    
    /**
     * @brief Destructor - handles cleanup of FireWire resources
     */
    ~IsochManager();
    
    // Prevent copying
    IsochManager(const IsochManager&) = delete;
    IsochManager& operator=(const IsochManager&) = delete;
    
    /**
     * @brief Initialize the IsochManager with a FireWire interface
     * 
     * @param interface The FireWire interface to use
     * @param isTalker Whether this is a talker (true) or listener (false)
     * @param runLoop The RunLoop to use for callbacks, or nullptr for current
     * @return std::expected<void, IOKitError> Success or error code
     */
    std::expected<void, IOKitError> initialize(
        IOFireWireLibNubRef interface,
        bool isTalker,
        CFRunLoopRef runLoop = nullptr);
    
    /**
     * @brief Create a DCL program for isochronous communication
     * 
     * @param cyclesPerSegment Number of cycles per segment
     * @param numSegments Number of segments in the DCL program
     * @param cycleBufferSize Size of each cycle buffer in bytes
     * @param bufferRange Memory range for the buffers
     * @return std::expected<void, IOKitError> Success or error code
     */
    std::expected<void, IOKitError> createDCLProgram(
        uint32_t cyclesPerSegment, 
        uint32_t numSegments, 
        uint32_t cycleBufferSize,
        IOVirtualRange& bufferRange);
    
    /**
     * @brief Configure the isochronous channel with speed and channel number
     * 
     * @param speed The FireWire speed to use
     * @param channel The channel number to use, or kAnyAvailableIsochChannel
     * @return std::expected<void, IOKitError> Success or error code
     */
    std::expected<void, IOKitError> configure(IOFWSpeed speed, uint32_t channel);
    
    /**
     * @brief Get the DCL program for use with local port
     * 
     * @return std::expected<DCLCommandPtr, IOKitError> DCL program or error code
     */
    std::expected<DCLCommandPtr, IOKitError> getProgram() const;
    
    /**
     * @brief Get the isoch channel
     * 
     * @return IOFireWireLibIsochChannelRef The FireWire isoch channel
     */
    IOFireWireLibIsochChannelRef getIsochChannel() const { return isochChannel_; }
    
    /**
     * @brief Get the active isoch channel number
     * 
     * @return std::expected<uint32_t, IOKitError> Active channel number or error
     */
    std::expected<uint32_t, IOKitError> getActiveChannel() const;
    
    /**
     * @brief Set the callback for DCL completion events
     * 
     * @param callback Function to call when a segment completes
     * @param refCon Context pointer to pass to the callback
     */
    void setDCLCompleteCallback(DCLCompleteCallback callback, void* refCon) {
        dclCompleteCallback_ = callback;
        dclCompleteRefCon_ = refCon;
    }
    
    /**
     * @brief Set the callback for DCL overrun events
     * 
     * @param callback Function to call on DCL overrun
     * @param refCon Context pointer to pass to the callback
     */
    void setDCLOverrunCallback(DCLOverrunCallback callback, void* refCon) {
        dclOverrunCallback_ = callback;
        dclOverrunRefCon_ = refCon;
    }
    
    /**
     * @brief Handle segment completion
     * 
     * @param segment Segment that completed
     * @return std::expected<void, IOKitError> Success or error code
     */
    std::expected<void, IOKitError> handleSegmentComplete(uint32_t segment);
    
    /**
     * @brief Fix up DCL jump targets
     * 
     * This updates the DCL branch instructions to maintain the proper flow
     * through the DCL program, especially after segment completion.
     * 
     * @return std::expected<void, IOKitError> Success or error code
     */
    std::expected<void, IOKitError> fixupDCLJumpTargets();
    
    /**
     * @brief Reset the IsochManager state
     */
    void reset();
    
    /**
     * @brief Special value for any available isoch channel
     */
    static constexpr uint32_t kAnyAvailableIsochChannel = 0xFFFFFFFF;
    
    /**
     * @brief Get the timestamps buffer address for a segment
     * 
     * @param segment Segment number
     * @return std::expected<uint32_t*, IOKitError> Pointer to timestamp or error
     */
    std::expected<uint32_t*, IOKitError> getTimestampPtr(uint32_t segment) const;

    /**
 * @brief Get the timestamp for a segment
 * 
 * @param segment Segment number
 * @return std::expected<uint32_t, IOKitError> Timestamp or error
 */
std::expected<uint32_t, IOKitError> getSegmentTimestamp(uint32_t segment) const;

// testing only
    // Methods to get segment info for data processing
    uint32_t getLastProcessedSegment() const {
        return processedSegments_.load(std::memory_order_acquire);
    }
    
    uint32_t getLastProcessedTimestamp() const {
        return processedTimestamps_.load(std::memory_order_acquire);
    }
    
private:

std::atomic<bool> hasReceivedData_{false};
bool hasReceivedData() const {
    return hasReceivedData_.load(std::memory_order_acquire);
}

void processSegmentData(uint32_t segment, uint32_t timestamp);

    // testing only
    // Just two simple atomics for segment tracking
    std::atomic<uint32_t> processedSegments_{0};
    std::atomic<uint32_t> processedTimestamps_{0};

    /**
     * @brief Structure for DCL segment information
     */
    struct SegmentInfo {
        NuDCLRef startDCL{nullptr};     ///< First DCL in segment
        NuDCLRef endDCL{nullptr};       ///< Last DCL in segment
        CFMutableSetRef updateBag{nullptr}; ///< Bag for DCL updates
        bool isActive{false};           ///< Whether this segment is active
    };
    
    // Setup methods
    std::expected<void, IOKitError> setupNuDCLPool();
    std::expected<void, IOKitError> createRemotePort();
    std::expected<void, IOKitError> createLocalPort(IOVirtualRange& bufferRange);
    std::expected<void, IOKitError> createIsochChannel();
    std::expected<void, IOKitError> createSegmentDCLs(IOVirtualRange& bufferRange);
    std::expected<void, IOKitError> createOverrunDCL(IOVirtualRange& bufferRange);
    
    // Static callback handlers for FireWire
    static IOReturn RemotePort_GetSupported_Helper(
        IOFireWireLibIsochPortRef interface,
        IOFWSpeed *outMaxSpeed,
        UInt64 *outChanSupported);
        
    static IOReturn RemotePort_AllocatePort_Helper(
        IOFireWireLibIsochPortRef interface,
        IOFWSpeed maxSpeed,
        UInt32 channel);
        
    static IOReturn RemotePort_ReleasePort_Helper(
        IOFireWireLibIsochPortRef interface);
        
    static IOReturn RemotePort_Start_Helper(
        IOFireWireLibIsochPortRef interface);
        
    static IOReturn RemotePort_Stop_Helper(
        IOFireWireLibIsochPortRef interface);
        
    static IOReturn PortFinalize_Helper(void* refcon);
    
    static void DCLComplete_Helper(void* refcon, NuDCLRef dcl);
    static void DCLOverrun_Helper(void* refcon, NuDCLRef dcl);
    
    // Instance callback handlers
    void handleDCLComplete(NuDCLRef dcl);
    void handleDCLOverrun();
    void handlePortFinalize();
    
    // Helper methods
    bool isValidSegment(uint32_t segment) const;
    NuDCLRef getDCLForSegment(uint32_t segment, uint32_t cycle) const;
    IOReturn notifyJumpUpdate(NuDCLRef dcl);
    
    // FireWire resources
    IOFireWireLibNubRef interface_{nullptr};
    IOFireWireLibNuDCLPoolRef nuDCLPool_{nullptr};
    IOFireWireLibRemoteIsochPortRef remotePort_{nullptr};
    IOFireWireLibLocalIsochPortRef localPort_{nullptr};
    IOFireWireLibIsochChannelRef isochChannel_{nullptr};
    
    // DCL program state
    std::vector<SegmentInfo> segments_;
    NuDCLRef overrunDCL_{nullptr};
    std::atomic<uint32_t> currentSegment_{0};
    uint32_t cyclesPerSegment_{0};
    uint32_t numSegments_{0};
    uint32_t cycleBufferSize_{0};
    
    // Buffer and timestamp tracking
    uint8_t* baseBuffer_{nullptr};
    uint32_t* timestampBuffer_{nullptr};
    
    // Configuration state
    std::shared_ptr<spdlog::logger> logger_;
    CFRunLoopRef runLoop_{nullptr};
    bool isTalker_{false};
    bool initialized_{false};
    bool finalized_{false};
    std::atomic<bool> running_{false};
    IOFWSpeed configuredSpeed_{kFWSpeed100MBit};
    uint32_t configuredChannel_{kAnyAvailableIsochChannel};
    uint32_t activeChannel_{kAnyAvailableIsochChannel};
    
    // Callbacks with refcons
    DCLCompleteCallback dclCompleteCallback_{nullptr};
    void* dclCompleteRefCon_{nullptr};
    DCLOverrunCallback dclOverrunCallback_{nullptr};
    void* dclOverrunRefCon_{nullptr};
    
    // Thread synchronization
    std::mutex stateMutex_;
};

} // namespace Isoch
} // namespace FWA


