#pragma once

#include <memory>
#include <functional>
#include <expected>
#include <atomic>
#include <mutex>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/firewire/IOFireWireLib.h> // For NubRef
#include <IOKit/firewire/IOFireWireLibIsoch.h>
#include <spdlog/logger.h>
#include "FWA/Error.h"
#include "Isoch/core/Types.hpp" // For DCLCommandPtr, IOVirtualRange

namespace FWA {
namespace Isoch {

/**
 * @brief Manages FireWire Isochronous Ports (Local & Remote) and the Isoch Channel.
 *
 * This class handles the creation, configuration, and lifecycle of the
 * FireWire components responsible for establishing the isochronous connection
 * (ports and channel). It manages interactions with the remote peer via
 * remote port callbacks and interacts with the RunLoop for dispatching.
 */
class IsochPortChannelManager {
public:
    /**
     * @brief Special value indicating any available isochronous channel.
     */
    static constexpr uint32_t kAnyAvailableIsochChannel = 0xFFFFFFFF;

    /**
     * @brief Construct a new IsochPortChannelManager instance.
     *
     * @param logger Logger for diagnostic information.
     * @param interface The IOFireWireLibNubRef for the device interface.
     * @param runLoop The RunLoop to use for callbacks.
     * @param isTalker True if this endpoint is the talker, false for listener.
     */
    explicit IsochPortChannelManager(
        std::shared_ptr<spdlog::logger> logger,
        IOFireWireLibNubRef interface, // Takes ownership (AddRef/Release)
        CFRunLoopRef runLoop,
        bool isTalker);

    /**
     * @brief Destructor - handles cleanup of ports, channel, pool, and dispatchers.
     */
    ~IsochPortChannelManager();

    // Prevent copying
    IsochPortChannelManager(const IsochPortChannelManager&) = delete;
    IsochPortChannelManager& operator=(const IsochPortChannelManager&) = delete;

    /**
     * @brief Initializes the manager: adds dispatchers to RunLoop, creates NuDCL Pool,
     *        and creates the Remote Port.
     *
     * @return std::expected<void, IOKitError> Success or error code.
     */
    std::expected<void, IOKitError> initialize();

    /**
     * @brief Creates the Local Port and Isoch Channel, connecting them based on role.
     * Requires `initialize()` to have been called successfully.
     *
     * @param program The DCL program handle (DCLCommand*) from IsochDCLManager::GetProgram.
     * @param bufferRange The memory range used by the DCL program.
     * @return std::expected<void, IOKitError> Success or error code.
     */
    std::expected<void, IOKitError> setupLocalPortAndChannel(
        DCLCommand* program,
        const IOVirtualRange& bufferRange);

    /**
     * @brief Configures the desired speed and channel for the connection.
     * This information is used when responding to remote port requests.
     * Must be called before the connection is established (e.g., before `start` on TransportManager).
     *
     * @param speed The desired FireWire speed.
     * @param channel The desired channel number (or kAnyAvailableIsochChannel).
     * @return std::expected<void, IOKitError> Success or error code.
     */
    std::expected<void, IOKitError> configure(IOFWSpeed speed, uint32_t channel);

    /**
     * @brief Gets the NuDCL Pool reference created during initialization.
     *
     * @return IOFireWireLibNuDCLPoolRef The NuDCL pool, or nullptr if not initialized.
     */
    IOFireWireLibNuDCLPoolRef getNuDCLPool() const;

    /**
     * @brief Gets the Local Isoch Port reference.
     *
     * @return IOFireWireLibLocalIsochPortRef The local port, or nullptr if not created.
     */
    IOFireWireLibLocalIsochPortRef getLocalPort() const;

    /**
     * @brief Gets the Isoch Channel reference.
     *
     * @return IOFireWireLibIsochChannelRef The isoch channel, or nullptr if not created.
     */
    IOFireWireLibIsochChannelRef getIsochChannel() const;

    /**
     * @brief Gets the FireWire Nub interface reference.
     *
     * @return IOFireWireLibNubRef The nub interface used by this manager, or nullptr if not available.
     */
    IOFireWireLibNubRef getNubInterface() const { return interface_; }

    /**
     * @brief Gets the currently active isochronous channel number (negotiated).
     *
     * @return std::expected<uint32_t, IOKitError> Active channel or error code.
     */
    std::expected<uint32_t, IOKitError> getActiveChannel() const;

    /**
     * @brief Gets the local node ID for this device.
     *
     * @return std::expected<uint16_t, IOKitError> The node ID or error code.
     */
    std::expected<uint16_t, IOKitError> getLocalNodeID() const;

    /**
     * @brief Resets the manager state, releasing ports, channel, pool,
     *        and removing dispatchers from the RunLoop.
     */
    void reset();

    // Optional: Add a finalize callback if external notification is needed
    // using PortFinalizeCallback = void(*)(void* refCon);
    // void setFinalizeCallback(PortFinalizeCallback callback, void* refCon);

private:
    // Internal setup helpers
    std::expected<void, IOKitError> setupDispatchers();
    std::expected<void, IOKitError> setupNuDCLPool();
    std::expected<void, IOKitError> createRemotePort();
    std::expected<void, IOKitError> createLocalPort(DCLCommand* program, const IOVirtualRange& bufferRange);
    std::expected<void, IOKitError> createIsochChannel();

    // Internal cleanup helper
    void cleanupDispatchers() noexcept;
    void cleanupResources() noexcept;

    // Static callback handlers for FireWire Port callbacks
    static IOReturn RemotePort_GetSupported_Helper(
        IOFireWireLibIsochPortRef interface, IOFWSpeed *outMaxSpeed, UInt64 *outChanSupported);
    static IOReturn RemotePort_AllocatePort_Helper(
        IOFireWireLibIsochPortRef interface, IOFWSpeed maxSpeed, UInt32 channel);
    static IOReturn RemotePort_ReleasePort_Helper(
        IOFireWireLibIsochPortRef interface);
    static IOReturn RemotePort_Start_Helper(
        IOFireWireLibIsochPortRef interface);
    static IOReturn RemotePort_Stop_Helper(
        IOFireWireLibIsochPortRef interface);
    static IOReturn PortFinalize_Helper(void* refcon); // RefCon is IsochPortChannelManager*

    // Instance methods called by the static helpers
    IOReturn handleRemotePortGetSupported(IOFWSpeed *outMaxSpeed, UInt64 *outChanSupported);
    IOReturn handleRemotePortAllocatePort(IOFWSpeed maxSpeed, UInt32 channel);
    IOReturn handleRemotePortReleasePort();
    IOReturn handleRemotePortStart();
    IOReturn handleRemotePortStop();
    void handlePortFinalize();

    // --- Member Variables ---
    std::shared_ptr<spdlog::logger> logger_;
    IOFireWireLibNubRef interface_{nullptr}; // Owning reference
    CFRunLoopRef runLoop_{nullptr};          // Non-owning reference

    IOFireWireLibNuDCLPoolRef nuDCLPool_{nullptr};
    IOFireWireLibRemoteIsochPortRef remotePort_{nullptr};
    IOFireWireLibLocalIsochPortRef localPort_{nullptr};
    IOFireWireLibIsochChannelRef isochChannel_{nullptr};

    // Configuration
    bool isTalker_{false};
    IOFWSpeed configuredSpeed_{kFWSpeed100MBit};
    uint32_t configuredChannel_{kAnyAvailableIsochChannel};
    uint32_t activeChannel_{kAnyAvailableIsochChannel}; // Negotiated channel

    // State
    bool initialized_{false};
    std::atomic<bool> running_{false}; // Controlled by remote start/stop
    bool finalized_{false};
    bool dispatchersAdded_{false};

    // Optional finalize callback forwarding
    // PortFinalizeCallback portFinalizeCallback_{nullptr};
    // void* portFinalizeRefCon_{nullptr};

    // Synchronization
    std::mutex stateMutex_; // Protects configuration and state during access/modification
};

} // namespace Isoch
} // namespace FWA