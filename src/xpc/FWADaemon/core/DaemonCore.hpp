// DaemonCore.hpp - Direct SHM Management (Complete Updated Header)
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <map>
#include <mutex>
#include <expected>
#include <optional>
#include <future>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

#include <CoreFoundation/CoreFoundation.h>
#include <spdlog/spdlog.h>

#include "FWA/DeviceController.h"
#include "FWA/AudioDevice.h"
#include "FWA/IOKitFireWireDeviceDiscovery.h"
#include "FWA/Error.h"

// Direct SHM structures
#include "shared/SharedMemoryStructures.hpp"

// Isochronous transmitter interface
#include "Isoch/interfaces/ITransmitPacketProvider.hpp"
#include "Isoch/core/AmdtpTransmitter.hpp" // For Diagnostics

namespace FWA {

enum class DaemonCoreError {
    Success = 0,
    NotInitialized,
    AlreadyInitialized,
    ServiceNotRunning,
    DeviceNotFound,
    OperationFailed,
    XPCInterfaceError,
    IOKitFailure,
    StreamSetupFailure,
    StreamStopFailure,
    SharedMemoryFailure,
    SharedMemoryTruncateFailure,
    SharedMemoryMappingFailure,
    SharedMemoryValidationFailure,
    ThreadCreationFailed,
    NoTransmitProvider,
    DataFlowConfigurationFailure,
    ProviderBindingFailure,
    InvalidParameter
};

class DaemonCore {
public:
    using DeviceNotificationToXPC_Cb = std::function<void(
        uint64_t guid,
        const std::string& name,
        const std::string& vendor,
        bool added
    )>;

    using StreamStatusToXPC_Cb = std::function<void(
        uint64_t guid,
        bool isStreaming,
        int errorCode
    )>;

    using LogToXPC_Cb = std::function<void(
        const std::string& msg,
        int level,
        const std::string& source
    )>;

    using DriverPresenceNotificationToXPC_Cb = std::function<void(bool)>;

    explicit DaemonCore(
        std::shared_ptr<spdlog::logger> logger,
        DeviceNotificationToXPC_Cb deviceCb,
        LogToXPC_Cb        logCb
    );
    ~DaemonCore();

    std::expected<void, DaemonCoreError> initializeAndStartService();
    void stopAndCleanupService();
    bool isServiceRunning() const;

    std::expected<std::string, DaemonCoreError> getDetailedDeviceInfoJSON(uint64_t guid);
    std::expected<std::vector<uint8_t>, DaemonCoreError> sendAVCCommand(uint64_t guid,
                                                                       const std::vector<uint8_t>& cmd);

    std::expected<void, DaemonCoreError> startAudioStreams(uint64_t guid);
    std::expected<void, DaemonCoreError> stopAudioStreams(uint64_t guid);

    void setDaemonLogLevel(int lvl);
    int  getDaemonLogLevel() const;

    void notifyDriverPresenceChanged(bool isPresent);
    std::string getSharedMemoryName() const;
    bool isSharedMemoryInitialized() const;

    void setStreamStatusCallback(StreamStatusToXPC_Cb cb);
    void setDriverPresenceCallback(DriverPresenceNotificationToXPC_Cb cb);

    bool isLogCallbackToXpcSet() const { return bool(m_logCb_toXPC); }
    void invokeLogCallbackToXpc(const std::string& msg,
                                int               level,
                                const std::string& source);

    // DIAGNOSTICS
    std::expected<std::map<uint32_t, uint64_t>, DaemonCoreError> getSHMFillLevelHistogram(uint64_t deviceGuid);
    std::expected<void, DaemonCoreError> resetSHMFillLevelHistogram(uint64_t deviceGuid);

private:
    // Controller thread / run loop
    void controllerThreadRunLoopFunc();
    std::thread    m_controllerThread;
    CFRunLoopRef   m_controllerRunLoopRef             = nullptr;
    CFRunLoopSourceRef m_controllerThreadStopSignal  = nullptr;
    std::atomic<bool>  m_serviceIsRunning{false};
    std::promise<void> m_runLoopReadyPromise;

    // Core
    std::shared_ptr<spdlog::logger> m_logger;
    std::shared_ptr<DeviceController> m_deviceController;

    // REMOVED: RingBufferManager m_shmManager
    // NEW: Direct SHM Management
    int m_shmFd = -1;                                    // SHM file descriptor
    void* m_shmRawPtr = nullptr;                         // Raw mmap pointer for cleanup
    size_t m_shmSize = 0;                                // Mapped size for munmap
    RTShmRing::ControlBlock_POD* m_shmControlBlock = nullptr;  // Direct control block pointer
    RTShmRing::AudioChunk_POD* m_shmRingArray = nullptr;       // Direct ring array pointer

    // Callbacks to XPC
    DeviceNotificationToXPC_Cb    m_deviceNotificationCb_toXPC;
    StreamStatusToXPC_Cb         m_streamStatusCb_toXPC;
    LogToXPC_Cb                   m_logCb_toXPC;
    DriverPresenceNotificationToXPC_Cb m_driverPresenceCb_toXPC;

    // Streams
    std::mutex m_streamsMutex;
    std::map<uint64_t, Isoch::ITransmitPacketProvider*> m_activeProviders;

    // Shared‐memory state
    const std::string m_shmName;
    std::atomic<bool> m_shmInitialized{false};

    // Private helper methods
    std::expected<void,DaemonCoreError> setupDriverSharedMemory();
    void cleanupSharedMemory();
    std::expected<void,DaemonCoreError> validateSharedMemoryFormat();
    std::expected<void,DaemonCoreError> configureDataFlow(uint64_t guid, std::shared_ptr<AudioDevice> device);
    void ensureStreamsStoppedForDevice(uint64_t guid,
                                      std::shared_ptr<AudioDevice> dev = nullptr);

    // spdlog→XPC sink
    std::shared_ptr<spdlog::sinks::sink> m_xpcSink;
    void setupSpdlogForwardingToXPC();

    // sync helper
    void performOnControllerThreadSync(const std::function<void()>& block);

    // Helper to get device by GUID
    std::expected<std::shared_ptr<AudioDevice>, DaemonCoreError> getDeviceByGuid(uint64_t guid);

    // FOR DIAGNOSTICS
    std::shared_ptr<FWA::Isoch::AmdtpTransmitter> mainTransmitter_;
};

} // namespace FWA