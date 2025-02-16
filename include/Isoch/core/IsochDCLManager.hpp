#pragma once

#include <memory>
#include <vector>
#include <functional>
#include <expected>
#include <atomic>
#include <mutex>
#include <CoreFoundation/CoreFoundation.h> // For CFMutableSetRef
#include <IOKit/firewire/IOFireWireLibIsoch.h>
#include <spdlog/logger.h>
#include "FWA/Error.h"
#include "Isoch/core/IsochBufferManager.hpp" // Include buffer manager directly

namespace FWA {
namespace Isoch {

/**
 * @brief Callback function for DCL completion events
 * @param groupIndex The group index that completed.
 * @param refCon User-provided context pointer.
 */
using DCLCompleteCallback = void(*)(uint32_t groupIndex, void* refCon);

/**
 * @brief Callback function for DCL overrun events
 * @param refCon User-provided context pointer.
 */
using DCLOverrunCallback = void(*)(void* refCon);

/**
 * @brief Manages the creation and lifecycle of a FireWire NuDCL program.
 *
 * This class focuses specifically on allocating NuDCL commands, linking them
 * into packet groups, handling buffer associations, managing jump targets,
 * and processing DCL-level callbacks (completion, overrun).
 */
class IsochDCLManager {
public:
    /**
     * @brief Configuration for the DCL Manager.
     */
    struct Config {
        uint32_t numGroups{8};             // Total number of buffer groups
        uint32_t packetsPerGroup{16};      // Number of FW packets per group
        uint32_t callbackGroupInterval{1}; // Callback after every N groups (1 = every group)
    };

    /**
     * @brief Structure to hold per-group metadata for callbacks
     */
    struct BufferGroupInfo {
        IsochDCLManager* manager{nullptr}; // Pointer back to the manager
        uint32_t groupIndex{0};            // Index of this group
        // Add any other relevant info needed by the callback handler
    };

    /**
     * @brief Construct a new IsochDCLManager instance.
     *
     * @param logger Logger for diagnostic information.
     * @param nuDCLPool The NuDCL pool provided by the Port/Channel Manager.
     * @param bufferManager Reference to the IsochBufferManager for buffer layout.
     * @param config DCL program configuration.
     */
    explicit IsochDCLManager(
        std::shared_ptr<spdlog::logger> logger,
        IOFireWireLibNuDCLPoolRef nuDCLPool, // Assumes pool is created elsewhere
        const IsochBufferManager& bufferManager, // Use const ref
        const Config& config);

    /**
     * @brief Destructor - handles cleanup of DCL resources.
     */
    ~IsochDCLManager();

    // Prevent copying
    IsochDCLManager(const IsochDCLManager&) = delete;
    IsochDCLManager& operator=(const IsochDCLManager&) = delete;

    /**
     * @brief Creates the full DCL program structure.
     * Does not fix jump targets initially.
     *
     * @return std::expected<DCLCommand*, IOKitError> The DCL program handle or an error.
     */
    std::expected<DCLCommand*, IOKitError> createDCLProgram();

    /**
     * @brief Fixes up the jump targets between the last and first DCL.
     * Notifies the local port.
     *
     * @param localPort The local port reference needed for notifications.
     * @return std::expected<void, IOKitError> Success or error code.
     */
    std::expected<void, IOKitError> fixupDCLJumpTargets(IOFireWireLibLocalIsochPortRef localPort);

    /**
     * @brief Sets the callback for DCL group completion events.
     *
     * @param callback Function to call when a group completes.
     * @param refCon Context pointer to pass to the callback.
     */
    void setDCLCompleteCallback(DCLCompleteCallback callback, void* refCon);

    /**
     * @brief Sets the callback for DCL overrun events.
     *
     * @param callback Function to call on DCL overrun.
     * @param refCon Context pointer to pass to the callback.
     */
    void setDCLOverrunCallback(DCLOverrunCallback callback, void* refCon);

    /**
     * @brief Gets the starting DCL command pointer of the created program.
     *
     * @return std::expected<DCLCommandPtr, IOKitError> DCL program start or error.
     */
    std::expected<DCLCommandPtr, IOKitError> getProgram() const;

    /**
     * @brief Resets the DCL manager state, releasing DCL resources.
     * Does not release the NuDCL pool itself (owned externally).
     */
    void reset();

private:
    // Internal helper to notify the port about jump updates
    IOReturn notifyJumpUpdate(IOFireWireLibLocalIsochPortRef localPort, NuDCLRef* dclRefPtr);

    // Static callback handlers for FireWire DCL callbacks
    static void DCLComplete_Helper(void* refcon, NuDCLRef dcl);
    static void DCLOverrun_Helper(void* refcon, NuDCLRef dcl);

    // Instance methods called by the static helpers
    void handleDCLComplete(NuDCLRef dcl, BufferGroupInfo* groupInfo);
    void handleDCLOverrun(NuDCLRef dcl);

    // --- Member Variables ---
    std::shared_ptr<spdlog::logger> logger_;
    IOFireWireLibNuDCLPoolRef nuDCLPool_{nullptr}; // Non-owning reference
    const IsochBufferManager& bufferManager_;      // Reference to buffer info
    Config config_;
    uint32_t totalPackets_{0};

    // Store refs to the very first and very last DCLs created
    NuDCLRef firstDCLRef_{nullptr};
    NuDCLRef lastDCLRef_{nullptr};

    // Store per-group callback info
    std::vector<BufferGroupInfo> groupInfos_;

    // State
    std::atomic<uint32_t> currentSegment_{0}; // Rename this later
    bool dclProgramCreated_{false};

    // Callbacks
    DCLCompleteCallback dclCompleteCallback_{nullptr};
    void* dclCompleteRefCon_{nullptr};
    DCLOverrunCallback dclOverrunCallback_{nullptr};
    void* dclOverrunRefCon_{nullptr};

    // Synchronization
    std::mutex stateMutex_; // Protects DCL structure modification, callbacks
};

} // namespace Isoch
} // namespace FWA