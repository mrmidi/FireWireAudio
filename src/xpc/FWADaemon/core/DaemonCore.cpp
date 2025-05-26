// DaemonCore.cpp
#include "DaemonCore.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <FWA/CommandInterface.h>

namespace FWA {

// -----------------------------------------------------------------------------
//  spdlog → XPC forwarding sink
// -----------------------------------------------------------------------------
template<typename Mutex>
class XpcSink : public spdlog::sinks::base_sink<Mutex> {
public:
    explicit XpcSink(DaemonCore* core): _core(core) {}
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        if (!_core->isLogCallbackToXpcSet()) return;
        spdlog::memory_buf_t buf;
        this->formatter_->format(msg, buf);
        _core->invokeLogCallbackToXpc(
            fmt::to_string(buf),
            int(msg.level),
            std::string(msg.logger_name.data(), msg.logger_name.size())
        );
    }
    void flush_() override {}
private:
    DaemonCore* _core;
};

// -----------------------------------------------------------------------------
//  Constructor / Destructor
// -----------------------------------------------------------------------------
DaemonCore::DaemonCore(
    std::shared_ptr<spdlog::logger> logger,
    DeviceNotificationToXPC_Cb      deviceCb,
    LogToXPC_Cb                     logCb
)
  : m_logger(std::move(logger))
  , m_deviceNotificationCb_toXPC(std::move(deviceCb))
  , m_logCb_toXPC(std::move(logCb))
  , m_shmName("/fwa_daemon_shm_v1")
{
    if (!m_logger) {
        auto sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        m_logger = std::make_shared<spdlog::logger>("DaemonCore", sink);
        m_logger->warn("No logger passed → using stderr_color_sink_mt fallback");
    }

    setupSpdlogForwardingToXPC();

    if (auto res = setupDriverSharedMemory(); !res) {
        m_logger->critical("Failed to set up driver shared memory: {}", int(res.error()));
    } else {
        m_logger->info("Driver shared memory '{}' initialized", m_shmName);
    }
}

DaemonCore::~DaemonCore() {
    stopAndCleanupService();
}

// -----------------------------------------------------------------------------
//  Setup SHM region for driver → daemon communication
// -----------------------------------------------------------------------------
std::expected<void,DaemonCoreError> DaemonCore::setupDriverSharedMemory() {
    if (m_shmInitialized) {
        m_logger->warn("Shared memory already initialized");
        return {};
    }

    // Ensure stale segment is gone
    shm_unlink(m_shmName.c_str());

    int fd = shm_open(m_shmName.c_str(), O_CREAT|O_RDWR, 0666);
    if (fd < 0) {
        m_logger->critical("shm_open('{}') failed: {} ({})",
                           m_shmName, errno, std::strerror(errno));
        return std::unexpected(DaemonCoreError::SharedMemoryFailure);
    }

    m_shmFd = fd; // Store the fd

    // Resize to fit our POD
    const size_t required = sizeof(RTShmRing::SharedRingBuffer_POD);
    if (ftruncate(m_shmFd, required) != 0) {
        m_logger->critical("ftruncate(fd={},size={}) failed: {} ({})",
                           m_shmFd, required, errno, std::strerror(errno));
        close(m_shmFd);
        m_shmFd = -1;
        return std::unexpected(DaemonCoreError::SharedMemoryTruncateFailure);
    }

    // Map into RingBufferManager
    if (!m_shmManager.map(m_shmFd, true/*creator*/)) {
        m_logger->critical("RingBufferManager.map() failed");
        close(m_shmFd); // Close on error
        m_shmFd = -1;
        shm_unlink(m_shmName.c_str());
        return std::unexpected(DaemonCoreError::SharedMemoryFailure);
    }
    // DO NOT close(m_shmFd) here anymore
    m_shmInitialized = true;
    return {};
}

// -----------------------------------------------------------------------------
//  Start / stop the overall service (discovery + streaming pump)
// -----------------------------------------------------------------------------
std::expected<void,DaemonCoreError> DaemonCore::initializeAndStartService() {
    m_logger->info("initializeAndStartService()...");

    if (m_serviceIsRunning) {
        m_logger->warn("Service already running");
        return std::unexpected(DaemonCoreError::AlreadyInitialized);
    }

    if (!m_shmInitialized) {
        m_logger->error("Cannot start service: shared memory not initialized");
        return std::unexpected(DaemonCoreError::SharedMemoryFailure);
    }

    m_serviceIsRunning = true;
    m_runLoopReadyPromise = {};
    auto readyFut = m_runLoopReadyPromise.get_future();

    try {
        m_controllerThread = std::thread(&DaemonCore::controllerThreadRunLoopFunc, this);
    } catch (const std::system_error& e) {
        m_logger->critical("Failed to create controller thread: {}", e.what());
        m_serviceIsRunning = false;
        return std::unexpected(DaemonCoreError::ThreadCreationFailed);
    }

    // Wait until CFRunLoop is up & discovery started
    try {
        readyFut.get();
    } catch (...) {
        m_logger->critical("Controller thread run loop setup failed");
        return std::unexpected(DaemonCoreError::OperationFailed);
    }

    m_logger->info("Service initialization complete");
    return {};
}

void DaemonCore::stopAndCleanupService() {
    m_logger->info("stopAndCleanupService()...");

    if (!m_serviceIsRunning.load(std::memory_order_acquire)) {
        m_logger->warn("Service not running");
        return;
    }

    // --- MODIFIED ORDER ---
    // 1. Stop all active audio streams FIRST
    m_logger->info("Stopping all active audio streams...");
    std::vector<uint64_t> guids_to_stop;
    {
        std::lock_guard lk(m_streamsMutex);
        for (const auto& [guid, provider_ptr] : m_activeProviders) {
            guids_to_stop.push_back(guid);
        }
    }
    for (uint64_t guid : guids_to_stop) {
        auto stop_res = stopAudioStreams(guid);
        if (!stop_res) {
            m_logger->error("Error stopping streams for GUID 0x{:x} during shutdown: {}", guid, static_cast<int>(stop_res.error()));
        }
    }
    m_logger->info("All active audio streams requested to stop.");
    // --- END MODIFIED ORDER ---

    // Signal controller thread to end its run loop
    m_serviceIsRunning.store(false, std::memory_order_release);
    if (m_controllerRunLoopRef && m_controllerThreadStopSignal) {
        CFRunLoopSourceSignal(m_controllerThreadStopSignal);
        CFRunLoopWakeUp(m_controllerRunLoopRef);
    }

    if (m_controllerThread.joinable()) {
        m_logger->info("Joining controller thread...");
        m_controllerThread.join();
        m_logger->info("Controller thread joined.");
    } else {
        m_logger->warn("Controller thread was not joinable.");
    }

    // Unmap & unlink shared memory
    if (m_shmInitialized) {
        m_shmManager.unmap();
        if (m_shmFd != -1) {
            close(m_shmFd);
            m_shmFd = -1;
        }
        if (shm_unlink(m_shmName.c_str()) == 0) {
            m_logger->info("Unlinked shared memory '{}'", m_shmName);
        } else {
             m_logger->error("Failed to unlink shared memory '{}': {}", m_shmName, strerror(errno));
        }
        m_shmInitialized = false;
    }

    m_logger->info("Service cleanup complete");
}

bool DaemonCore::isServiceRunning() const {
    return m_serviceIsRunning.load(std::memory_order_acquire);
}

// -----------------------------------------------------------------------------
//  CFRunLoop + DeviceController discovery
// -----------------------------------------------------------------------------
void DaemonCore::controllerThreadRunLoopFunc() {
    pthread_setname_np("FWA.DaemonCore.Ctrl");

    m_logger->info("Controller thread starting CFRunLoop...");
    m_controllerRunLoopRef = CFRunLoopGetCurrent();

    // Create a dummy source so we can stop the loop
    CFRunLoopSourceContext ctx = {0, this, nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr};
    m_controllerThreadStopSignal = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &ctx);
    CFRunLoopAddSource(m_controllerRunLoopRef,
                       m_controllerThreadStopSignal,
                       kCFRunLoopDefaultMode);

    // Instantiate DeviceController + discovery
    try {
        m_deviceController = std::make_shared<DeviceController>(nullptr);
        auto discovery = std::make_unique<IOKitFireWireDeviceDiscovery>(m_deviceController);
        m_deviceController->setDiscoveryService(std::move(discovery));
    } catch (const std::exception& e) {
        m_logger->critical("Failed to create DeviceController/discovery: {}", e.what());
        m_runLoopReadyPromise.set_exception(std::make_exception_ptr(e));
        return;
    }

    // Start discovery
    auto startRes = m_deviceController->start(
        [this](auto dev,bool added){
            auto guid = dev->getGuid();
            if (!added) ensureStreamsStoppedForDevice(guid, dev);
            if (m_deviceNotificationCb_toXPC) {
                m_deviceNotificationCb_toXPC(
                    guid,
                    dev->getDeviceName(),
                    dev->getVendorName(),
                    added
                );
            }
        }
    );
    if (!startRes) {
        m_logger->critical("DeviceController.start() failed: {}", int(startRes.error()));
        m_runLoopReadyPromise.set_exception(
            std::make_exception_ptr(std::runtime_error("Discovery start failed"))
        );
        return;
    }

    // Signal main thread that we're up & running
    m_runLoopReadyPromise.set_value();
    m_logger->info("Device discovery started; entering CFRunLoop");

    // Run loop until stop signaled
    while (m_serviceIsRunning.load(std::memory_order_acquire)) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, /*returnAfterSourceHandled=*/true);
    }

    m_logger->info("Controller CFRunLoop exiting");
    if (m_controllerRunLoopRef && m_controllerThreadStopSignal) {
        CFRunLoopRemoveSource(
            m_controllerRunLoopRef,
            m_controllerThreadStopSignal,
            kCFRunLoopDefaultMode
        );
        CFRelease(m_controllerThreadStopSignal);
        m_controllerThreadStopSignal = nullptr;
    }
    m_controllerRunLoopRef = nullptr;
}

// -----------------------------------------------------------------------------
//  Helper to get device by GUID
// -----------------------------------------------------------------------------
std::expected<std::shared_ptr<AudioDevice>, DaemonCoreError> 
DaemonCore::getDeviceByGuid(uint64_t guid) {
    if (!m_serviceIsRunning || !m_deviceController) {
        return std::unexpected(DaemonCoreError::ServiceNotRunning);
    }
    auto r = m_deviceController->getDeviceByGuid(guid);
    if (!r) {
        return std::unexpected(DaemonCoreError::DeviceNotFound);
    }
    return r.value();
}

// -----------------------------------------------------------------------------
//  Queries & AVC Commands
// -----------------------------------------------------------------------------
std::expected<std::string,DaemonCoreError>
DaemonCore::getDetailedDeviceInfoJSON(uint64_t guid) {
    auto deviceRes = getDeviceByGuid(guid);
    if (!deviceRes) return std::unexpected(deviceRes.error());
    
    auto device = deviceRes.value();
    auto info   = device->getDeviceInfo().toJson(*device);
    return info.dump(4);
}

std::expected<std::vector<uint8_t>,DaemonCoreError>
DaemonCore::sendAVCCommand(uint64_t guid,
                           const std::vector<uint8_t>& cmd)
{
    auto deviceRes = getDeviceByGuid(guid);
    if (!deviceRes) return std::unexpected(deviceRes.error());

    auto ci = deviceRes.value()->getCommandInterface();
    if (!ci) {
        return std::unexpected(DaemonCoreError::NotInitialized);
    }
    auto resp = ci->sendCommand(cmd);
    if (!resp) {
        return std::unexpected(DaemonCoreError::IOKitFailure);
    }
    return resp.value();
}

// -----------------------------------------------------------------------------
//  Audio stream start/stop with two-phase initialization
// -----------------------------------------------------------------------------
std::expected<void,DaemonCoreError>
DaemonCore::startAudioStreams(uint64_t guid)
{
    m_logger->info("Starting audio streams for device GUID: 0x{:x}", guid);
    
    auto deviceRes = getDeviceByGuid(guid);
    if (!deviceRes) return std::unexpected(deviceRes.error());
    auto device = deviceRes.value();

    // Phase 1: Start the audio streams
    m_logger->debug("Phase 1: Starting device streams...");
    auto startRes = device->startStreams();
    if (!startRes) {
        m_logger->error("Failed to start streams for GUID 0x{:x}: {}", 
                       guid, static_cast<int>(startRes.error()));
        if (m_streamStatusCb_toXPC) {
            m_streamStatusCb_toXPC(guid, false, int(startRes.error()));
        }
        return std::unexpected(DaemonCoreError::StreamSetupFailure);
    }
    m_logger->info("Device streams started successfully for GUID 0x{:x}", guid);

    // Phase 2: Configure data flow
    m_logger->debug("Phase 2: Configuring data flow...");
    auto configRes = configureDataFlow(guid, device);
    if (!configRes) {
        m_logger->error("Failed to configure data flow for GUID 0x{:x}: {}", 
                       guid, static_cast<int>(configRes.error()));
        // Cleanup: stop streams if data flow configuration failed
        device->stopStreams();
        if (m_streamStatusCb_toXPC) {
            m_streamStatusCb_toXPC(guid, false, int(configRes.error()));
        }
        return configRes;
    }

    m_logger->info("Audio streams and data flow configured successfully for GUID 0x{:x}", guid);
    if (m_streamStatusCb_toXPC) {
        m_streamStatusCb_toXPC(guid, true, 0);
    }
    
    return {};
}

std::expected<void,DaemonCoreError>
DaemonCore::configureDataFlow(uint64_t guid, std::shared_ptr<AudioDevice> device) {
    if (!device) {
        return std::unexpected(DaemonCoreError::DeviceNotFound);
    }

    // Get the transmit packet provider from the device
    m_logger->debug("Getting transmit packet provider for GUID 0x{:x}", guid);
    Isoch::ITransmitPacketProvider* provider = device->getTransmitPacketProvider();
    
    if (!provider) {
        m_logger->error("No transmit packet provider available for GUID 0x{:x}", guid);
        return std::unexpected(DaemonCoreError::NoTransmitProvider);
    }

    // Configure the RingBufferManager with the provider
    try {
        m_shmManager.setPacketProvider(provider);
        m_logger->info("RingBufferManager configured with packet provider for GUID 0x{:x}", guid);
    } catch (const std::exception& e) {
        m_logger->error("Failed to configure RingBufferManager: {}", e.what());
        return std::unexpected(DaemonCoreError::DataFlowConfigurationFailure);
    }

    // Store the active provider
    {
        std::lock_guard lk(m_streamsMutex);
        m_activeProviders[guid] = provider;
    }

    m_logger->debug("Data flow configured successfully for GUID 0x{:x}", guid);
    return {};
}

std::expected<void,DaemonCoreError>
DaemonCore::stopAudioStreams(uint64_t guid)
{
    m_logger->info("Stopping audio streams for device GUID: 0x{:x}", guid);
    
    auto deviceRes = getDeviceByGuid(guid);
    if (!deviceRes) return std::unexpected(deviceRes.error());

    // Phase 1: Stop data flow first
    m_logger->debug("Phase 1: Stopping data flow...");
    {
        std::lock_guard lk(m_streamsMutex);
        auto it = m_activeProviders.find(guid);
        if (it != m_activeProviders.end()) {
            // Clear the provider from RingBufferManager
            m_shmManager.setPacketProvider(nullptr);
            m_activeProviders.erase(it);
            m_logger->debug("Data flow stopped for GUID 0x{:x}", guid);
        }
    }

    // Phase 2: Stop the device streams
    m_logger->debug("Phase 2: Stopping device streams...");
    auto stopRes = deviceRes.value()->stopStreams();
    if (!stopRes) {
        m_logger->error("Failed to stop streams for GUID 0x{:x}: {}", 
                       guid, static_cast<int>(stopRes.error()));
        if (m_streamStatusCb_toXPC) {
            m_streamStatusCb_toXPC(guid, true, int(stopRes.error()));
        }
        return std::unexpected(DaemonCoreError::StreamStopFailure);
    }

    m_logger->info("Audio streams stopped successfully for GUID 0x{:x}", guid);
    if (m_streamStatusCb_toXPC) {
        m_streamStatusCb_toXPC(guid, false, 0);
    }
    
    return {};
}

// -----------------------------------------------------------------------------
//  Helper to stop if device disappears
// -----------------------------------------------------------------------------
void DaemonCore::ensureStreamsStoppedForDevice(
    uint64_t guid,
    std::shared_ptr<AudioDevice> dev
) {
    m_logger->info("Ensuring streams stopped for device GUID: 0x{:x}", guid);
    
    if (!dev) {
        auto deviceRes = getDeviceByGuid(guid);
        if (deviceRes) {
            dev = deviceRes.value();
        }
    }
    
    // Stop data flow first
    {
        std::lock_guard lk(m_streamsMutex);
        auto it = m_activeProviders.find(guid);
        if (it != m_activeProviders.end()) {
            m_shmManager.setPacketProvider(nullptr);
            m_activeProviders.erase(it);
            m_logger->debug("Cleared data flow for disconnected device GUID: 0x{:x}", guid);
        }
    }
    
    // Stop device streams if device is still available
    if (dev) {
        dev->stopStreams();
        m_logger->debug("Stopped streams for disconnected device GUID: 0x{:x}", guid);
    }
}

// -----------------------------------------------------------------------------
//  Logging control
// -----------------------------------------------------------------------------
void DaemonCore::setDaemonLogLevel(int lvl) {
    if (lvl < int(spdlog::level::trace) || lvl > int(spdlog::level::off)) {
        m_logger->warn("Invalid log level {}", lvl);
        return;
    }
    auto level = spdlog::level::level_enum(lvl);
    spdlog::set_level(level);
    m_logger->set_level(level);
}

int DaemonCore::getDaemonLogLevel() const {
    return int(m_logger->level());
}

void DaemonCore::setupSpdlogForwardingToXPC() {
    m_xpcSink = std::make_shared<XpcSink<std::mutex>>(this);
    m_xpcSink->set_level(spdlog::level::trace);
    m_logger->sinks().push_back(m_xpcSink);
    if (auto def = spdlog::default_logger(); def.get() != m_logger.get()) {
        def->sinks().push_back(m_xpcSink);
    }
}

void DaemonCore::invokeLogCallbackToXpc(
    const std::string& msg,
    int                 level,
    const std::string&  source
) {
    if (m_logCb_toXPC) {
        m_logCb_toXPC(msg, level, source);
    }
}

// -----------------------------------------------------------------------------
//  Driver‐presence / SHM name
// -----------------------------------------------------------------------------
void DaemonCore::notifyDriverPresenceChanged(bool isPresent) {
    m_logger->info("Driver presence changed: {}", isPresent);
    if (m_driverPresenceCb_toXPC) {
        m_driverPresenceCb_toXPC(isPresent);
    }
    // Optionally remap if needed...
}

std::string DaemonCore::getSharedMemoryName() const {
    return m_shmName;
}

bool DaemonCore::isSharedMemoryInitialized() const {
    return m_shmInitialized;
}

void DaemonCore::setStreamStatusCallback(StreamStatusToXPC_Cb cb) {
    m_streamStatusCb_toXPC = std::move(cb);
}

void DaemonCore::setDriverPresenceCallback(DriverPresenceNotificationToXPC_Cb cb) {
    m_driverPresenceCb_toXPC = std::move(cb);
}

} // namespace FWA