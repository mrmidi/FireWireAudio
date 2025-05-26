// DaemonCore.hpp
#pragma once

#include <string>
#include <vector>
#include <memory>       // For std::unique_ptr, std::shared_ptr
#include <functional>   // For std::function
#include <thread>       // For std::thread
#include <atomic>       // For std::atomic
#include <map>          // For std::map
#include <mutex>        // For std::mutex
#include <expected>     // For std::expected (C++23)
#include <optional>     // For std::optional
#include <future>      // For std::future (if needed for async operations)
#include <sys/mman.h>   // For shm_open, shm_unlink, mmap, munmap
#include <sys/stat.h>   // For fstat, mode_t
#include <fcntl.h>      // For O_CREAT, O_RDWR, O_EXCL
#include <unistd.h>     // For ftruncate, close
#include <cerrno>       // For errno
#include <cstring>      // For strerror

#include <CoreFoundation/CoreFoundation.h> // For CFRunLoopRef, CFRunLoopSourceRef
#include <spdlog/spdlog.h>                // For spdlog::logger

// --- FWA Library Core Headers ---
#include "FWA/DeviceController.h"   // Manages AudioDevice collection and discovery lifecycle
#include "FWA/AudioDevice.h"        // Represents a single device, now owns its IsoStreamHandler
#include "FWA/IOKitFireWireDeviceDiscovery.h" // For device discovery via IOKit
#include "FWA/Error.h"              // For IOKitError (used by FWA components)

// --- Isoch Library Core Headers ---
// DaemonCore needs to know about ITransmitPacketProvider to link ShmIsochBridge
#include "Isoch/interfaces/ITransmitPacketProvider.hpp"
// IsoStreamHandler is now managed by AudioDevice, so DaemonCore doesn't need to include its header directly
// unless it needs to interact with it beyond what AudioDevice exposes.

// --- Daemon's Shared Memory and Bridge Components ---
#include "xpc/FWAXPC/RingBufferManager.hpp" // For SHM between ASPL driver and daemon
#include "xpc/FWAXPC/ShmIsochBridge.hpp"   // Bridges audio from SHM to an ITransmitPacketProvider

namespace FWA {

// Error enum for DaemonCore's public methods.
// These can represent high-level operational errors or wrap/map errors from underlying FWA/Isoch calls.
enum class DaemonCoreError {
    Success = 0,
    NotInitialized,         // Service/component not ready
    AlreadyInitialized,     // Attempt to initialize something already up
    ServiceNotRunning,      // Core service (e.g., discovery thread) isn't running
    DeviceNotFound,         // Specified GUID does not match an active device
    OperationFailed,        // Generic failure for an operation
    XPCInterfaceError,      // Problem with XPC interaction (e.g., SHM name retrieval if daemon was also a client)
    IOKitFailure,           // An error reported by IOKit (often from FWA::AudioDevice or DeviceController)
    StreamSetupFailure,     // Could not set up or start streams (from AudioDevice or Isoch)
    StreamStopFailure,      // Could not stop streams
    SharedMemoryFailure,    // Issue creating, mapping, or using the driver SHM segment
    SharedMemoryTruncateFailure, // Failed to truncate SHM segment
    ThreadCreationFailed,   // Failed to create necessary internal threads
    NoTransmitProvider,     // AudioDevice started streams but didn't yield a transmitter
    InvalidParameter
};

class DaemonCore {
public:
    // --- Callbacks to notify the Objective-C++ XPC layer ---

    // Called when a device is added or removed by DeviceController
    using DeviceNotificationToXPC_Cb = std::function<void(
        uint64_t guid,
        const std::string& name,    // Device Name
        const std::string& vendor,  // Vendor Name
        bool added                  // True if added, false if removed
    )>;

    // Called when stream status for a specific device changes
    using StreamStatusToXPC_Cb = std::function<void(
        uint64_t guid,
        bool isStreaming,           // True if one or more streams are active, false otherwise
        int fwaErrorCode            // 0 for success, or a DaemonCoreError/IOKitError code
    )>;

    // Called to forward internal logs (from FWA/Isoch via spdlog) to XPC
    using LogToXPC_Cb = std::function<void(
        const std::string& message, // Fully formatted log message
        int level,                  // spdlog::level::level_enum as int
        const std::string& source   // Name of the spdlog logger (e.g., "FWA::AudioDevice")
    )>;

    // Called when the daemon receives an XPC notification from the ASPL driver
    // about its presence (e.g., driver loaded/unloaded in coreaudiod).
    using DriverPresenceNotificationToXPC_Cb = std::function<void(bool isPresent)>;


    // --- Constructor & Destructor ---
    // Modified constructor to accept critical callbacks
    explicit DaemonCore(
        std::shared_ptr<spdlog::logger> logger,
        DeviceNotificationToXPC_Cb deviceCb,
        LogToXPC_Cb logCb
    );
    ~DaemonCore();

    // Prevent copy/move until properly implemented if needed
    DaemonCore(const DaemonCore&) = delete;
    DaemonCore& operator=(const DaemonCore&) = delete;
    DaemonCore(DaemonCore&&) = delete;
    DaemonCore& operator=(DaemonCore&&) = delete;


    // --- Lifecycle Management ---
    // Initializes DeviceController, starts its discovery thread & run loop. SHM is now set up in constructor.
    std::expected<void, DaemonCoreError> initializeAndStartService();
    void stopAndCleanupService();
    bool isServiceRunning() const;


    // --- Device Information (XPC -> DaemonCore -> DeviceController -> AudioDevice) ---
    std::expected<std::string, DaemonCoreError> getDetailedDeviceInfoJSON(uint64_t guid);


    // --- AV/C Commands (XPC -> DaemonCore -> DeviceController -> AudioDevice -> CommandInterface) ---
    std::expected<std::vector<uint8_t>, DaemonCoreError> sendAVCCommand(uint64_t guid, const std::vector<uint8_t>& command);


    // --- Isochronous Stream Control (XPC -> DaemonCore -> DeviceController -> AudioDevice) ---
    // AudioDevice internally creates and manages its IsoStreamHandler.
    // startAudioStreams will also attempt to link the ShmIsochBridge to the device's transmitter.
    std::expected<void, DaemonCoreError> startAudioStreams(uint64_t guid);
    // stopAudioStreams will also unlink ShmIsochBridge if this device was using it.
    std::expected<void, DaemonCoreError> stopAudioStreams(uint64_t guid);


    // --- Logging Control (XPC -> DaemonCore -> spdlog global settings) ---
    void setDaemonLogLevel(int spdlogLevel); // int matches spdlog::level::level_enum
    int getDaemonLogLevel() const;


    // --- Driver & Shared Memory Interaction ---
    void notifyDriverPresenceChanged(bool isPresent);
    std::string getSharedMemoryName() const;
    bool isSharedMemoryInitialized() const;

    // --- Setters for Callbacks to Objective-C++ XPC Layer ---
    void setStreamStatusCallback(StreamStatusToXPC_Cb cb);
    void setDriverPresenceCallback(DriverPresenceNotificationToXPC_Cb cb);

    // Public method for the XPC Log Forwarding Sink to call.
    bool isLogCallbackToXpcSet() const { return static_cast<bool>(m_logCb_toXPC); }
    void invokeLogCallbackToXpc(const std::string& formattedMessage, int spdlogLevel, const std::string& loggerName);

private:
    // --- Threading & RunLoop for DeviceController ---
    void controllerThreadRunLoopFunc(); // Manages CFRunLoop for DeviceController/IOKit
    std::thread m_controllerThread;
    CFRunLoopRef m_controllerRunLoopRef = nullptr;
    CFRunLoopSourceRef m_controllerThreadStopSignalSource = nullptr; // Used to signal the run loop to stop
    std::atomic<bool> m_serviceIsRunning{false};
    // Promise/Future to signal run loop readiness from controllerThreadRunLoopFunc
    std::promise<void> m_runLoopReadyPromise;


    // --- Core Components ---
    std::shared_ptr<spdlog::logger> m_logger; // Logger for DaemonCore itself
    std::shared_ptr<FWA::DeviceController> m_deviceController;
    // Note: DaemonCore does NOT directly store AudioDevice objects.
    // It interacts with them via the DeviceController.

    // Shared Memory (Driver -> Daemon) & Bridge to Isoch Transmitter
    RingBufferManager& m_driverShmRingBufferManager;
    // ShmIsochBridge& m_shmToIsochBridge;
    // Stream management
    std::mutex m_streamsMutex;
    std::map<uint64_t, std::shared_ptr<FWA::IsoStreamHandler>> m_streamHandlers;

    std::atomic<bool> m_shmInitialized{false};
    const std::string m_driverSharedMemoryName; // Initialized in constructor
    std::optional<uint64_t> m_currentShmBridgeUserGuid; // GUID of device whose transmitter uses the bridge

    // New private method for SHM setup
    std::expected<void, DaemonCoreError> setupDriverSharedMemory();


    // --- Callbacks to Objective-C++ XPC Layer ---
    DeviceNotificationToXPC_Cb m_deviceNotificationCb_toXPC;
    StreamStatusToXPC_Cb m_streamStatusCb_toXPC;
    LogToXPC_Cb m_logCb_toXPC;
    DriverPresenceNotificationToXPC_Cb m_driverPresenceCb_toXPC;


    // --- Internal Callback Handlers & Helpers ---
    // Called by FWA::DeviceController when a device is added/removed
    void handleDeviceControllerNotification(std::shared_ptr<FWA::AudioDevice> device, bool added);
    // Called by FWA::AudioDevice (via DeviceController or directly) for stream status updates
    void handleAudioDeviceStreamStatusUpdate(uint64_t guid, bool isStreaming, int fwaErrorCode);
    // Helper to cleanly stop a specific AudioDevice's streams and related components
    void ensureStreamsStoppedForDevice(uint64_t guid, std::shared_ptr<FWA::AudioDevice> devicePtr = nullptr);


    // --- spdlog Integration ---
    // Custom sink to forward logs from C++ (spdlog) to the m_logCb_toXPC
    std::shared_ptr<spdlog::sinks::sink> m_xpcLogForwardingSink;
    void setupSpdlogForwardingToXPC();


    // --- Internal State ---
    bool m_driverIsPresent = false; // Set by notifyDriverPresenceChanged
    bool m_driverShmIsInitializedByDaemon = false; // Tracks if daemon successfully created/mapped its end of SHM
};

} // namespace FWA