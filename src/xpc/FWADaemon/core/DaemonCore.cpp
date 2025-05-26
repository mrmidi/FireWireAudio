// DaemonCore.cpp
#include "core/DaemonCore.hpp" // Assuming DaemonCore.hpp is in core/
#include "FWA/Helpers.h"       // If needed for any utility functions
#include <nlohmann/json.hpp>   // Include for nlohmann::json
#include "FWA/CommandInterface.h"

#include <spdlog/sinks/base_sink.h> // For custom spdlog sink
#include <spdlog/sinks/stdout_color_sinks.h> // For stderr_color_sink_mt
#include <mach/thread_policy.h>     // For thread priorities
#include <pthread.h>                // For pthread_setname_np

namespace FWA {

// Custom spdlog sink to forward logs to the XPC layer
template<typename Mutex>
class XpcLogForwardingSink : public spdlog::sinks::base_sink<Mutex> {
public:
    explicit XpcLogForwardingSink(DaemonCore* core_ptr) : m_daemonCorePtr(core_ptr) {}

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        if (m_daemonCorePtr && m_daemonCorePtr->isLogCallbackToXpcSet()) {
            spdlog::memory_buf_t formatted;
            this->formatter_->format(msg, formatted);
            std::string message_str = fmt::to_string(formatted);
            std::string source_str = std::string(msg.logger_name.data(), msg.logger_name.size());
            m_daemonCorePtr->invokeLogCallbackToXpc(message_str, static_cast<int>(msg.level), source_str);
        }
    }

    void flush_() override {}

private:
    DaemonCore* m_daemonCorePtr; // Non-owning raw pointer
};


// --- Constructor & Destructor ---
DaemonCore::DaemonCore(
    std::shared_ptr<spdlog::logger> logger,
    DeviceNotificationToXPC_Cb deviceCb,
    LogToXPC_Cb logCb)
    : m_logger(std::move(logger)),
      m_deviceNotificationCb_toXPC(std::move(deviceCb)),
      m_logCb_toXPC(std::move(logCb)),
      m_driverSharedMemoryName("/fwa_daemon_shm_v1_dc_test"),
      m_driverShmRingBufferManager(RingBufferManager::instance())
{
    if (!m_logger) {
        auto stderr_fallback_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        m_logger = std::make_shared<spdlog::logger>("DaemonCore_Fallback", stderr_fallback_sink);
        m_logger->set_level(spdlog::level::info);
        m_logger->warn("DaemonCore: Using fallback stderr logger.");
    }
    m_logger->info("DaemonCore constructing...");

    if (!m_logCb_toXPC) {
        m_logger->error("DaemonCore Constructor: Log CB is NULL after construction!");
    } else {
        setupSpdlogForwardingToXPC();
    }

    // Setup driver shared memory in constructor
    auto shmResult = setupDriverSharedMemory();
    if (!shmResult) {
        m_logger->critical("DaemonCore: CRITICAL FAILURE during constructor: Failed to setup driver shared memory: {}", static_cast<int>(shmResult.error()));
    } else {
        m_logger->info("DaemonCore: Driver shared memory setup successfully during construction.");
    }

    m_logger->info("DaemonCore constructed.");
}

DaemonCore::~DaemonCore() {
    m_logger->info("DaemonCore destructing...");
    if (m_serviceIsRunning.load(std::memory_order_relaxed)) {
        stopAndCleanupService();
    }
    m_logger->info("DaemonCore destructed.");
}


// --- Lifecycle Management ---
std::expected<void, DaemonCoreError> DaemonCore::initializeAndStartService() {
    m_logger->info("DaemonCore: Initializing and starting service (discovery, etc.)...");

    if (m_serviceIsRunning.load(std::memory_order_acquire)) {
        m_logger->warn("DaemonCore: Service already running.");
        return std::unexpected(DaemonCoreError::AlreadyInitialized);
    }

    // Ensure SHM is ready before proceeding
    if (!m_shmInitialized.load()) {
        m_logger->error("DaemonCore::initializeAndStartService: Driver SHM is not initialized. Cannot start full service.");
        return std::unexpected(DaemonCoreError::SharedMemoryFailure);
    }

    // Start Controller Thread for IOKit Discovery
    m_logger->info("DaemonCore: Starting controller/discovery thread...");
    m_serviceIsRunning.store(true, std::memory_order_release);

    m_runLoopReadyPromise = std::promise<void>();
    auto runLoopReadyFuture = m_runLoopReadyPromise.get_future();

    try {
        m_controllerThread = std::thread(&DaemonCore::controllerThreadRunLoopFunc, this);
    } catch (const std::system_error& e) {
        m_logger->critical("DaemonCore: Failed to create controller thread: {}", e.what());
        m_serviceIsRunning.store(false, std::memory_order_relaxed);
        return std::unexpected(DaemonCoreError::ThreadCreationFailed);
    }

    m_logger->debug("DaemonCore: Waiting for controller thread run loop to become ready...");
    try {
        runLoopReadyFuture.get();
        m_logger->info("DaemonCore: Controller thread run loop is ready.");
    } catch (const std::future_error& e) {
        m_logger->critical("DaemonCore: Error waiting for run loop ready signal: {}", e.what());
        m_serviceIsRunning.store(false, std::memory_order_relaxed);
        if (m_controllerThreadStopSignalSource && m_controllerRunLoopRef) {
            CFRunLoopSourceSignal(m_controllerThreadStopSignalSource);
            CFRunLoopWakeUp(m_controllerRunLoopRef);
        }
        if (m_controllerThread.joinable()) m_controllerThread.join();
        return std::unexpected(DaemonCoreError::OperationFailed);
    }

    m_logger->info("DaemonCore: Service initialization and startup sequence complete.");
    return {};
}
// New private method for SHM setup
std::expected<void, DaemonCoreError> DaemonCore::setupDriverSharedMemory() {
    if (m_shmInitialized.load()) {
        m_logger->warn("DaemonCore::setupDriverSharedMemory: SHM already initialized.");
        return {};
    }

    m_logger->info("DaemonCore: Setting up shared memory '{}' for driver communication...", m_driverSharedMemoryName);
    shm_unlink(m_driverSharedMemoryName.c_str());

    int shmFd = shm_open(m_driverSharedMemoryName.c_str(), O_CREAT | O_RDWR, 0666);
    bool isCreator = true;

    if (shmFd == -1) {
        m_logger->critical("DaemonCore: shm_open failed for '{}': {} - {}", m_driverSharedMemoryName, errno, strerror(errno));
        return std::unexpected(DaemonCoreError::SharedMemoryFailure);
    }
    m_logger->info("DaemonCore: SHM segment '{}' descriptor acquired (fd {}).", m_driverSharedMemoryName, shmFd);

    off_t requiredSize = sizeof(RTShmRing::SharedRingBuffer_POD);
    if (ftruncate(shmFd, requiredSize) == -1) {
        m_logger->critical("DaemonCore: ftruncate failed for SHM (fd {}): {} - {}", shmFd, errno, strerror(errno));
        close(shmFd);
        shm_unlink(m_driverSharedMemoryName.c_str());
        return std::unexpected(DaemonCoreError::SharedMemoryTruncateFailure);
    }
    m_logger->info("DaemonCore: SHM segment truncated to {} bytes.", requiredSize);

    bool mapSuccess = m_driverShmRingBufferManager.map(shmFd, isCreator);
    close(shmFd);

    if (!mapSuccess) {
        m_logger->critical("DaemonCore: RingBufferManager failed to map shared memory '{}'.", m_driverSharedMemoryName);
        shm_unlink(m_driverSharedMemoryName.c_str());
        return std::unexpected(DaemonCoreError::SharedMemoryFailure);
    }
    m_shmInitialized.store(true);
    m_logger->info("DaemonCore: Driver Shared Memory '{}' successfully setup and mapped.", m_driverSharedMemoryName);
    return {};
}

void DaemonCore::controllerThreadRunLoopFunc() {
    pthread_setname_np("FWA.DaemonCore.Ctrl");
    m_logger->info("DaemonCore: Controller thread ({}) started.", fmt::ptr(pthread_self()));

    m_controllerRunLoopRef = CFRunLoopGetCurrent();
    if (!m_controllerRunLoopRef) {
        m_logger->critical("DaemonCore: Failed to get CFRunLoop for controller thread! Aborting.");
        m_runLoopReadyPromise.set_exception(std::make_exception_ptr(std::runtime_error("Failed to get CFRunLoop")));
        m_serviceIsRunning.store(false, std::memory_order_release);
        return;
    }

    // Create and add a simple signal source to allow stopping the run loop cleanly
    CFRunLoopSourceContext context = {0, this, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    m_controllerThreadStopSignalSource = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &context);
    if (!m_controllerThreadStopSignalSource) {
        m_logger->critical("DaemonCore: Failed to create stop signal source for controller thread run loop!");
        m_runLoopReadyPromise.set_exception(std::make_exception_ptr(std::runtime_error("Failed to create CFRunLoopSource")));
        m_serviceIsRunning.store(false, std::memory_order_release);
        return;
    }
    CFRunLoopAddSource(m_controllerRunLoopRef, m_controllerThreadStopSignalSource, kCFRunLoopDefaultMode);

    // Instantiate Discovery and Controller
    try {
        // Create DeviceController first
        m_deviceController = std::make_shared<FWA::DeviceController>(nullptr); // Pass nullptr for discovery
        // Now create discovery and set controller
        auto discoveryService = std::make_unique<FWA::IOKitFireWireDeviceDiscovery>(m_deviceController);
        m_deviceController->setDiscoveryService(std::move(discoveryService));
    } catch (const std::exception& e) {
        m_logger->critical("DaemonCore: Failed to create DeviceController/Discovery: {}", e.what());
        m_runLoopReadyPromise.set_exception(std::make_exception_ptr(e));
        CFRunLoopRemoveSource(m_controllerRunLoopRef, m_controllerThreadStopSignalSource, kCFRunLoopDefaultMode);
        CFRelease(m_controllerThreadStopSignalSource); m_controllerThreadStopSignalSource = nullptr;
        m_controllerRunLoopRef = nullptr;
        m_serviceIsRunning.store(false, std::memory_order_release);
        return;
    }

    // Start Discovery
    m_logger->info("DaemonCore: Controller thread calling m_deviceController->startDiscovery()...");
    auto startResult = m_deviceController->start(
        [this](std::shared_ptr<FWA::AudioDevice> dev, bool isAdded) {
            this->handleDeviceControllerNotification(std::move(dev), isAdded);
        }
    );
    if (!startResult) {
        m_logger->critical("DaemonCore: m_deviceController->startDiscovery() failed: {}", static_cast<int>(startResult.error()));
        m_runLoopReadyPromise.set_exception(std::make_exception_ptr(std::runtime_error("DeviceController start failed")));
         // Cleanup
        m_deviceController.reset();
        CFRunLoopRemoveSource(m_controllerRunLoopRef, m_controllerThreadStopSignalSource, kCFRunLoopDefaultMode);
        CFRelease(m_controllerThreadStopSignalSource); m_controllerThreadStopSignalSource = nullptr;
        m_controllerRunLoopRef = nullptr;
        m_serviceIsRunning.store(false, std::memory_order_release);
        return;
    }
    m_logger->info("DaemonCore: m_deviceController->startDiscovery() successful.");

    // Signal main thread that run loop is ready and discovery is initiated
    m_runLoopReadyPromise.set_value();

    m_logger->info("DaemonCore: Controller RunLoop starting on thread ({}).", fmt::ptr(pthread_self()));
    while (m_serviceIsRunning.load(std::memory_order_acquire)) {
        int32_t result = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, true); // Run for 1 sec
        if (result == kCFRunLoopRunStopped || result == kCFRunLoopRunFinished) {
            m_logger->info("DaemonCore ControllerRunLoop: CFRunLoopRunInMode indicated stop/finish.");
            break; // Exit loop if explicitly stopped or no more sources
        } else if (result == kCFRunLoopRunTimedOut) {
            // Loop continues, this is normal
        } else if (result == kCFRunLoopRunHandledSource) {
            // A source was handled, loop continues
        }
    }

    m_logger->info("DaemonCore: Controller RunLoop on thread ({}) stopping...", fmt::ptr(pthread_self()));
    if (m_deviceController) {
        m_logger->debug("DaemonCore: Stopping device controller discovery...");
        m_deviceController->stop(); // This should stop IOKitFireWireDeviceDiscovery
    }
    if (m_controllerRunLoopRef && m_controllerThreadStopSignalSource) { // Check if still valid
        CFRunLoopRemoveSource(m_controllerRunLoopRef, m_controllerThreadStopSignalSource, kCFRunLoopDefaultMode);
    }
    if (m_controllerThreadStopSignalSource) {
        CFRelease(m_controllerThreadStopSignalSource);
        m_controllerThreadStopSignalSource = nullptr;
    }
    m_controllerRunLoopRef = nullptr;
    m_logger->info("DaemonCore: Controller thread ({}) finished.", fmt::ptr(pthread_self()));
}

void DaemonCore::stopAndCleanupService() {
    m_logger->info("DaemonCore: Stopping and cleaning up service...");
    if (!m_serviceIsRunning.load(std::memory_order_acquire)) {
        m_logger->warn("DaemonCore: Service already stopped or not started.");
        return;
    }

    // 1. Signal the controller thread to stop its run loop
    m_serviceIsRunning.store(false, std::memory_order_release);
    if (m_controllerRunLoopRef && m_controllerThreadStopSignalSource) {
        m_logger->debug("DaemonCore: Signaling controller thread run loop to stop.");
        CFRunLoopSourceSignal(m_controllerThreadStopSignalSource);
        CFRunLoopWakeUp(m_controllerRunLoopRef); // Wake up the run loop to process the signal
    }

    // 2. Join the controller thread
    if (m_controllerThread.joinable()) {
        m_logger->debug("DaemonCore: Joining controller thread...");
        try {
            m_controllerThread.join();
            m_logger->info("DaemonCore: Controller thread joined.");
        } catch (const std::system_error& e) {
            m_logger->error("DaemonCore: Error joining controller thread: {}", e.what());
        }
    }
    // m_deviceController->stop() is called by the controller thread before it exits.
    m_deviceController.reset(); // Destroy DeviceController (and its IOKitFireWireDeviceDiscovery)

    // 3. Stop all active Isoch streams
    m_logger->debug("DaemonCore: Stopping all active audio streams...");
    std::vector<uint64_t> activeStreamGuids;
    {
        std::lock_guard<std::mutex> lock(m_streamsMutex);
        for(const auto& pair : m_streamHandlers) {
            activeStreamGuids.push_back(pair.first);
        }
    }
    for (uint64_t guid : activeStreamGuids) {
        // Call the public stopAudioStreams which handles m_shmToIsochBridge
        stopAudioStreams(guid);
    }
    {
        std::lock_guard<std::mutex> lock(m_streamsMutex);
        m_streamHandlers.clear();
    }

    // Explicitly stop bridge if it was somehow left running
    // if (m_shmToIsochBridge.isRunning()) {
    //     m_logger->warn("DaemonCore: ShmIsochBridge was still running during final cleanup, stopping it.");
    //     m_shmToIsochBridge.stop();
    // }
    // m_currentShmBridgeUserGuid.reset();



    // 4. Unmap and Unlink Driver Shared Memory
    if (m_driverShmIsInitializedByDaemon) {
        m_logger->debug("DaemonCore: Unmapping and unlinking driver shared memory...");
        if (m_driverShmRingBufferManager.isMapped()) {
            m_driverShmRingBufferManager.unmap();
        }
        if (shm_unlink(m_driverSharedMemoryName.c_str()) == 0) {
            m_logger->info("DaemonCore: Driver SHM segment '{}' unlinked.", m_driverSharedMemoryName);
        } else {
            if (errno != ENOENT) {
                m_logger->warn("DaemonCore: shm_unlink for driver SHM '{}' failed: {} - {}", m_driverSharedMemoryName, errno, strerror(errno));
            }
        }
        m_driverShmIsInitializedByDaemon = false;
    }

    // 5. Clear callbacks (optional, as object is being destroyed)
    m_deviceNotificationCb_toXPC = nullptr;
    m_streamStatusCb_toXPC = nullptr;
    m_logCb_toXPC = nullptr;
    m_driverPresenceCb_toXPC = nullptr;

    m_logger->info("DaemonCore: Service cleanup complete.");
}

bool DaemonCore::isServiceRunning() const {
    return m_serviceIsRunning.load(std::memory_order_acquire);
}


// --- Internal Callback Handlers & Helpers ---
void DaemonCore::handleDeviceControllerNotification(std::shared_ptr<FWA::AudioDevice> device, bool added) {
    if (!device) {
        m_logger->error("DaemonCore: Received null device pointer in DeviceController notification.");
        return;
    }
    uint64_t guid = device->getGuid();
    std::string nameStr = device->getDeviceName();
    std::string vendorStr = device->getVendorName();

    m_logger->info("DaemonCore: DeviceController Notification: GUID {:#016x} ('{}'), Added: {}", guid, nameStr, added);

    if (!added) {
        ensureStreamsStoppedForDevice(guid, device);
    }

    if (m_deviceNotificationCb_toXPC) {
        m_deviceNotificationCb_toXPC(guid, nameStr, vendorStr, added);
    } else {
        m_logger->critical("DaemonCore: m_deviceNotificationCb_toXPC is NOT SET even after constructor initialization! Cannot forward device notification.");
    }
}

// This is called by AudioDevice (or IsoStreamHandler via AudioDevice)
void DaemonCore::handleAudioDeviceStreamStatusUpdate(uint64_t guid, bool isStreaming, int fwaErrorCode) {
    m_logger->info("DaemonCore: Stream status update for GUID {:#016x}: Streaming: {}, ErrorCode: {}", guid, isStreaming, fwaErrorCode);
    if (m_streamStatusCb_toXPC) {
        m_streamStatusCb_toXPC(guid, isStreaming, fwaErrorCode);
    } else {
        m_logger->warn("DaemonCore: m_streamStatusCb_toXPC is not set, cannot forward stream status.");
    }
}

void DaemonCore::ensureStreamsStoppedForDevice(uint64_t guid, std::shared_ptr<FWA::AudioDevice> devicePtr) {
    m_logger->debug("DaemonCore: Ensuring streams are stopped for device GUID {:#016x}", guid);
    std::shared_ptr<FWA::AudioDevice> deviceToStop = devicePtr;

    if (!deviceToStop) {
        // Attempt to get it from DeviceController if not provided
        if (m_deviceController) {
            auto result = m_deviceController->getDeviceByGuid(guid);
            if (result) deviceToStop = result.value();
        }
    }

    if (deviceToStop) {
        // This calls the AudioDevice's internal stop for its IsoStreamHandler
        auto stopResult = deviceToStop->stopStreams();
        if (!stopResult) {
            m_logger->error("DaemonCore: Failed to issue stopStreams command to AudioDevice {:#016x}: {}", guid, static_cast<int>(stopResult.error()));
        } else {
            m_logger->info("DaemonCore: Issued stopStreams to AudioDevice {:#016x}.", guid);
        }
    } else {
        m_logger->warn("DaemonCore: Could not find device {:#016x} to ensure its streams are stopped.", guid);
    }

    // If this device was using the ShmIsochBridge, stop the bridge
    if (m_currentShmBridgeUserGuid && m_currentShmBridgeUserGuid.value() == guid) {
        m_logger->info("DaemonCore: Device {:#016x} was using ShmIsochBridge. Stopping bridge.", guid);
        // m_shmToIsochBridge.stop();
        // m_currentShmBridgeUserGuid.reset();
    }
}


// --- spdlog Integration ---
void DaemonCore::setupSpdlogForwardingToXPC() {
    // Implementation using XpcLogForwardingSink from previous example
    try {
        m_xpcLogForwardingSink = std::make_shared<XpcLogForwardingSink<std::mutex>>(this);
        m_xpcLogForwardingSink->set_level(spdlog::level::trace);

        if (m_logger) { // Add to DaemonCore's own logger
            m_logger->sinks().push_back(m_xpcLogForwardingSink);
        }
        // Also add to spdlog's default logger so FWA/Isoch libs using spdlog::info() are caught
        auto defaultLogger = spdlog::default_logger();
        if (defaultLogger && defaultLogger != m_logger) { // Avoid double-adding if m_logger IS the default
            defaultLogger->sinks().push_back(m_xpcLogForwardingSink);
        }
        m_logger->info("DaemonCore: spdlog forwarding sink to XPC configured.");
    } catch (const std::exception& e) {
        m_logger->error("DaemonCore: Failed to setup spdlog forwarding sink: {}", e.what());
    }
}

void DaemonCore::invokeLogCallbackToXpc(const std::string& formattedMessage, int spdlogLevel, const std::string& loggerName) {
    if (m_logCb_toXPC) {
        m_logCb_toXPC(formattedMessage, spdlogLevel, loggerName);
    }
}

// Removed setDeviceNotificationCallback and setLogCallback (now in constructor)
void DaemonCore::setStreamStatusCallback(StreamStatusToXPC_Cb cb) { m_streamStatusCb_toXPC = std::move(cb); }
void DaemonCore::setDriverPresenceCallback(DriverPresenceNotificationToXPC_Cb cb) { m_driverPresenceCb_toXPC = std::move(cb); }


// --- Public Method Implementations (delegating to DeviceController & AudioDevice) ---

std::expected<std::string, DaemonCoreError> DaemonCore::getDetailedDeviceInfoJSON(uint64_t guid) {
    m_logger->debug("DaemonCore: getDetailedDeviceInfoJSON for GUID {:#016x}", guid);
    if (!m_serviceIsRunning || !m_deviceController) return std::unexpected(DaemonCoreError::ServiceNotRunning);

    auto deviceResult = m_deviceController->getDeviceByGuid(guid);
    if (!deviceResult) {
        m_logger->warn("DaemonCore: Device not found for GUID {:#016x}", guid);
        return std::unexpected(DaemonCoreError::DeviceNotFound);
    }
    auto device = deviceResult.value();
    nlohmann::json deviceInfoJson = device->getDeviceInfo().toJson(*device); // Call toJson on DeviceInfo
    return deviceInfoJson.dump(4); // ident for debugging
}


std::expected<std::vector<uint8_t>, DaemonCoreError> DaemonCore::sendAVCCommand(uint64_t guid, const std::vector<uint8_t>& command) {
    m_logger->debug("DaemonCore: sendAVCCommand for GUID {:#016x}", guid);
    if (!m_serviceIsRunning || !m_deviceController) return std::unexpected(DaemonCoreError::ServiceNotRunning);

    auto deviceResult = m_deviceController->getDeviceByGuid(guid);
    if (!deviceResult) return std::unexpected(DaemonCoreError::DeviceNotFound);

    auto cmdInterface = deviceResult.value()->getCommandInterface();
    if (!cmdInterface) {
        m_logger->error("DaemonCore: No CommandInterface for device {:#016x}", guid);
        return std::unexpected(DaemonCoreError::NotInitialized);
    }

    auto avcResult = cmdInterface->sendCommand(command);
    if (!avcResult) {
        // Map IOKitError to DaemonCoreError
        m_logger->error("DaemonCore: sendCommand on interface failed for {:#016x}: {}", guid, static_cast<int>(avcResult.error()));
        return std::unexpected(DaemonCoreError::IOKitFailure); // Or map specific IOKitError
    }
    return avcResult.value();
}

std::expected<void, DaemonCoreError> DaemonCore::startAudioStreams(uint64_t guid) {
    m_logger->info("DaemonCore: Request to start audio streams for GUID {:#016x}", guid);
    if (!m_serviceIsRunning || !m_deviceController) return std::unexpected(DaemonCoreError::ServiceNotRunning);

    auto deviceResult = m_deviceController->getDeviceByGuid(guid);
    if (!deviceResult) return std::unexpected(DaemonCoreError::DeviceNotFound);
    
    std::shared_ptr<AudioDevice> device = deviceResult.value();

    // AudioDevice::startStreams() will create/start its IsoStreamHandler
    // It needs to return the ITransmitPacketProvider* if transmit is started
    auto streamStartOp = device->startStreams();

    if (!streamStartOp) { // This checks the std::expected from device->startStreams()
        m_logger->error("DaemonCore: AudioDevice::startStreams() failed for GUID {:#016x}: error {}", guid, static_cast<int>(streamStartOp.error()));
        // Notify XPC layer of failure immediately if startStreams itself fails synchronously
        handleAudioDeviceStreamStatusUpdate(guid, false, static_cast<int>(streamStartOp.error()));
        return std::unexpected(DaemonCoreError::StreamSetupFailure); // Map error
    }

    // If device->startStreams() succeeded, we assume streaming has started.
    // If you need the provider, update startStreams() to return it.
    m_logger->info("DaemonCore: AudioDevice {:#016x} started streams.", guid);
    // If you need to start m_shmToIsochBridge, get the provider from the device or IsoStreamHandler here.
    
    // Actual "isStreaming" status will come via handleAudioDeviceStreamStatusUpdate callback
    // from IsoStreamHandler -> AudioDevice -> DaemonCore
    m_logger->info("DaemonCore: startAudioStreams command issued for GUID {:#016x}. Waiting for status callback.", guid);
    return {}; // Command issued successfully
}

std::expected<void, DaemonCoreError> DaemonCore::stopAudioStreams(uint64_t guid) {
    m_logger->info("DaemonCore: Request to stop audio streams for GUID {:#016x}", guid);
    if (!m_serviceIsRunning || !m_deviceController) return std::unexpected(DaemonCoreError::ServiceNotRunning);

    auto deviceResult = m_deviceController->getDeviceByGuid(guid);
    if (!deviceResult) return std::unexpected(DaemonCoreError::DeviceNotFound);

    auto stopOpResult = deviceResult.value()->stopStreams(); // AudioDevice stops its IsoStreamHandler

    if (m_currentShmBridgeUserGuid && m_currentShmBridgeUserGuid.value() == guid) {
        m_logger->info("DaemonCore: Stopping ShmIsochBridge as device {:#016x} was using it.", guid);
        // m_shmToIsochBridge.stop();
        m_currentShmBridgeUserGuid.reset();
    }

    if (!stopOpResult) {
        m_logger->error("DaemonCore: AudioDevice::stopStreams() failed for GUID {:#016x}: error {}", guid, static_cast<int>(stopOpResult.error()));
        handleAudioDeviceStreamStatusUpdate(guid, true, static_cast<int>(stopOpResult.error())); // Report error but say still streaming if stop cmd failed
        return std::unexpected(DaemonCoreError::StreamStopFailure);
    }
    
    m_logger->info("DaemonCore: stopAudioStreams command issued for GUID {:#016x}. Waiting for status callback.", guid);
    return {}; // Command issued successfully
}

void DaemonCore::setDaemonLogLevel(int spdlogLevel) {
    if (spdlogLevel >= spdlog::level::trace && spdlogLevel <= spdlog::level::off) {
        auto lvl = static_cast<spdlog::level::level_enum>(spdlogLevel);
        m_logger->info("DaemonCore: Setting global spdlog level to: {}", spdlog::level::to_string_view(lvl));
        spdlog::set_level(lvl);
        if (m_logger) m_logger->set_level(lvl); // Set for DaemonCore's own logger too
    } else {
        m_logger->warn("DaemonCore: Invalid spdlog level {} specified.", spdlogLevel);
    }
}

int DaemonCore::getDaemonLogLevel() const {
    return static_cast<int>(m_logger ? m_logger->level() : spdlog::default_logger()->level());
}

void DaemonCore::notifyDriverPresenceChanged(bool isPresent) {
    m_logger->info("DaemonCore: Driver XPC presence changed to: {}", isPresent);
    m_driverIsPresent = isPresent;
    if (m_driverPresenceCb_toXPC) {
        m_driverPresenceCb_toXPC(isPresent);
    }
    if (isPresent && m_driverShmIsInitializedByDaemon && !m_driverShmRingBufferManager.isMapped()) {
        m_logger->warn("DaemonCore: Driver became present, but SHM was not mapped. This scenario needs robust handling (e.g. re-map attempt or error state).");
        // Potentially:
        // int shmFd = shm_open(m_driverSharedMemoryName.c_str(), O_RDWR, 0);
        // if (shmFd != -1) {
        //     m_driverShmRingBufferManager.map(shmFd, false /*isCreator=false, daemon already created*/);
        //     close(shmFd);
        // }
    } else if (!isPresent && m_driverShmRingBufferManager.isMapped()) {
         // If driver disconnects, RingBufferManager can continue trying to pop (will get no data).
         // ShmIsochBridge will also get no data. This is probably fine.
         // No need to unmap SHM here, daemon still owns it.
         m_logger->info("DaemonCore: Driver disconnected, SHM remains mapped by daemon.");
    }
}

std::string DaemonCore::getSharedMemoryName() const {
    if (!m_driverShmIsInitializedByDaemon) {
        m_logger->warn("DaemonCore: getSharedMemoryName called but SHM not initialized by daemon yet.");
    }
    return m_driverSharedMemoryName;
}



bool DaemonCore::isSharedMemoryInitialized() const {
    return m_shmInitialized;
}


} // namespace FWA