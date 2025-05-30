// DaemonCore.cpp
#include "DaemonCore.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <FWA/CommandInterface.h>

#include <CoreFoundation/CoreFoundation.h>
#include <future>
#include <optional>
#include <memory>
#include <stdexcept>
#include <os/log.h>

#include "Isoch/core/IsochPacketProvider.hpp"

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
//  Helper: run a block synchronously on the controller thread's run‑loop
// -----------------------------------------------------------------------------
void DaemonCore::performOnControllerThreadSync(const std::function<void()>& block)
{
    if (!m_controllerRunLoopRef) {
        m_logger->error("performOnControllerThreadSync: Controller run loop is not valid!");
        os_log(OS_LOG_DEFAULT,
               "DaemonCore: performOnControllerThreadSync called without valid controller run loop");
        throw std::runtime_error("Controller run loop not available for sync execution");
    }
    if (CFRunLoopGetCurrent() == m_controllerRunLoopRef) {
        block();
        return;
    }

    auto promisePtr = std::make_shared<std::promise<std::optional<std::exception_ptr>>>();
    auto future     = promisePtr->get_future();

    CFRunLoopPerformBlock(m_controllerRunLoopRef,
                          kCFRunLoopDefaultMode,
                          ^{
                              try {
                                  block();
                                  promisePtr->set_value(std::nullopt);
                              } catch (...) {
                                  promisePtr->set_value(std::current_exception());
                              }
                          });
    CFRunLoopWakeUp(m_controllerRunLoopRef);

    if (auto maybeEx = future.get(); maybeEx) {
        std::rethrow_exception(*maybeEx);
    }
}

// -----------------------------------------------------------------------------
//  Constructor / Destructor
// -----------------------------------------------------------------------------
DaemonCore::DaemonCore(std::shared_ptr<spdlog::logger> logger,
                       DeviceNotificationToXPC_Cb deviceCb,
                       LogToXPC_Cb logCb)
  : m_logger(std::move(logger)),
    m_deviceNotificationCb_toXPC(std::move(deviceCb)),
    m_logCb_toXPC(std::move(logCb)),
    m_shmName("/fwa_daemon_shm_v2")  // Updated for ABI v2
{
    if (!m_logger) {
        auto sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        m_logger  = std::make_shared<spdlog::logger>("DaemonCore", sink);
        m_logger->warn("No logger passed → using stderr_color_sink_mt fallback");
    }

    setupSpdlogForwardingToXPC();

    // SHM setup is now deferred to initializeAndStartService()
}

DaemonCore::~DaemonCore() { 
    stopAndCleanupService(); 
    cleanupSharedMemory();
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

    // Create or open SHM segment
    m_shmFd = shm_open(m_shmName.c_str(), O_CREAT|O_RDWR, 0666);
    if (m_shmFd < 0) {
        m_logger->critical("shm_open('{}') failed: {} ({})",
                           m_shmName, errno, std::strerror(errno));
        return std::unexpected(DaemonCoreError::SharedMemoryFailure);
    }

    // Set size for the entire SharedRingBuffer_POD structure
    const size_t required = sizeof(RTShmRing::SharedRingBuffer_POD);
    m_shmSize = required;
    
    if (ftruncate(m_shmFd, required) != 0) {
        m_logger->critical("ftruncate(fd={},size={}) failed: {} ({})",
                           m_shmFd, required, errno, std::strerror(errno));
        close(m_shmFd);
        m_shmFd = -1;
        return std::unexpected(DaemonCoreError::SharedMemoryTruncateFailure);
    }


    // Direct mmap of the entire structure
    m_shmRawPtr = mmap(nullptr, required, PROT_READ | PROT_WRITE, MAP_SHARED, m_shmFd, 0);
    if (m_shmRawPtr == MAP_FAILED) {
        m_logger->critical("mmap(size={}) failed: {} ({})",
                           required, errno, std::strerror(errno));
        close(m_shmFd);
        m_shmFd = -1;
        m_shmRawPtr = nullptr;
        return std::unexpected(DaemonCoreError::SharedMemoryMappingFailure);
    }

    // Zero the entire SHM region to prevent stale data
    std::memset(m_shmRawPtr, 0, required);

    // Lock pages in memory for real-time performance
    if (mlock(m_shmRawPtr, required) != 0) {
        m_logger->warn("mlock failed: {} ({}) - real-time performance may suffer", 
                       errno, strerror(errno));
        // Continue anyway - mlock failure isn't fatal
    }

    // Extract direct pointers to control block and ring array
    RTShmRing::SharedRingBuffer_POD* shmBuffer = 
        static_cast<RTShmRing::SharedRingBuffer_POD*>(m_shmRawPtr);
    m_shmControlBlock = &shmBuffer->control;
    m_shmRingArray = shmBuffer->ring;

    // Initialize ABI v2 control block
    m_shmControlBlock->abiVersion = kShmVersion;
    m_shmControlBlock->capacity = kRingCapacityPow2;
    m_shmControlBlock->sampleRateHz = 44100;    // Default, driver may override
    m_shmControlBlock->channelCount = 2;        // Default stereo
    m_shmControlBlock->bytesPerFrame = 8;       // 2 channels * 4 bytes (24-bit in 32-bit)
    
    // Initialize atomic indices
    RTShmRing::WriteIndexProxy(*m_shmControlBlock).store(0, std::memory_order_relaxed);
    RTShmRing::ReadIndexProxy(*m_shmControlBlock).store(0, std::memory_order_relaxed);
    RTShmRing::OverrunCountProxy(*m_shmControlBlock).store(0, std::memory_order_relaxed);
    RTShmRing::UnderrunCountProxy(*m_shmControlBlock).store(0, std::memory_order_relaxed);

    // Validate the initialized format
    if (auto validateRes = validateSharedMemoryFormat(); !validateRes) {
        cleanupSharedMemory();
        return validateRes;
    }

    m_shmInitialized = true;
    m_logger->info("Direct SHM mapping established: ptr={}, size={}, ABI v{}", 
                   m_shmRawPtr, m_shmSize, m_shmControlBlock->abiVersion);
    return {};
}

// -----------------------------------------------------------------------------
//  Start / stop the overall service (discovery + streaming pump)
// -----------------------------------------------------------------------------
std::expected<void,DaemonCoreError> DaemonCore::initializeAndStartService() {
    m_logger->info("initializeAndStartService()...");


    // Setup SHM first if not already done
    if (!m_shmInitialized) {
        if (auto res = setupDriverSharedMemory(); !res) {
            m_logger->error("Cannot start service: shared memory setup failed: {}", int(res.error()));
            return res;
        }
    }

    if (m_serviceIsRunning) {
        m_logger->warn("Service already running");
        return std::unexpected(DaemonCoreError::AlreadyInitialized);
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

    // Stop all active audio streams FIRST
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
            m_logger->error("Error stopping streams for GUID 0x{:x} during shutdown: {}", 
                           guid, static_cast<int>(stop_res.error()));
        }
    }
    m_logger->info("All active audio streams requested to stop.");

    // Signal controller thread to stop
    m_serviceIsRunning.store(false, std::memory_order_release);
    if (m_controllerRunLoopRef) {
        CFRunLoopPerformBlock(m_controllerRunLoopRef,
                              kCFRunLoopDefaultMode,
                              ^{ CFRunLoopStop(CFRunLoopGetCurrent()); });
        CFRunLoopWakeUp(m_controllerRunLoopRef);
    }

    // Join controller thread
    if (m_controllerThread.joinable()) {
        m_logger->info("Joining controller thread...");
        m_controllerThread.join();
        m_logger->info("Controller thread joined.");
    }

    // NOTE: Direct SHM cleanup happens in destructor via cleanupSharedMemory()
    m_logger->info("Service cleanup complete");
}

bool DaemonCore::isServiceRunning() const {
    return m_serviceIsRunning.load(std::memory_order_acquire);
}

// -----------------------------------------------------------------------------
//  Controller thread main – now uses a single CFRunLoopRun()
// -----------------------------------------------------------------------------
void DaemonCore::controllerThreadRunLoopFunc() {
    pthread_setname_np("FWA.DaemonCore.Ctrl");

    m_logger->info("Controller thread starting CFRunLoop...");
    os_log(OS_LOG_DEFAULT, "DaemonCore: Controller thread starting CFRunLoop...");
    m_controllerRunLoopRef = CFRunLoopGetCurrent();

    // allow default-mode sources to run whenever kCFRunLoopCommonModes is active
    CFRunLoopAddCommonMode(m_controllerRunLoopRef, kCFRunLoopDefaultMode);

    // Dummy source to keep a reference we can wake/stop with
    CFRunLoopSourceContext ctx = {0, this};
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
    os_log(OS_LOG_DEFAULT, "DaemonCore: Device discovery started; entering CFRunLoop");

    // *** Run loop lives until CFRunLoopStop is invoked ***
    CFRunLoopRun();

    m_logger->info("Controller CFRunLoop exiting");
    if (m_controllerThreadStopSignal) {
        CFRunLoopRemoveSource(m_controllerRunLoopRef,
                              m_controllerThreadStopSignal,
                              kCFRunLoopDefaultMode);
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

    m_logger->debug("Configuring direct SHM data flow for GUID 0x{:x}", guid);
    
    // Validate SHM is ready
    if (!m_shmInitialized || !m_shmControlBlock || !m_shmRingArray) {
        m_logger->error("SHM not initialized for GUID 0x{:x}", guid);
        return std::unexpected(DaemonCoreError::SharedMemoryFailure);
    }

    // Get transmit packet provider from device
    Isoch::ITransmitPacketProvider* provider = device->getTransmitPacketProvider();
    if (!provider) {
        m_logger->error("No transmit packet provider available for GUID 0x{:x}", guid);
        return std::unexpected(DaemonCoreError::NoTransmitProvider);
    }

    // Type validation - ensure it's IsochPacketProvider for direct binding
    auto* isochProvider = dynamic_cast<Isoch::IsochPacketProvider*>(provider);
    if (!isochProvider) {
        m_logger->error("Provider is not IsochPacketProvider type for GUID 0x{:x}", guid);
        return std::unexpected(DaemonCoreError::DataFlowConfigurationFailure);
    }

    // Direct binding to SHM
    if (!isochProvider->bindSharedMemory(m_shmControlBlock, m_shmRingArray)) {
        m_logger->error("Failed to bind provider to SHM for GUID 0x{:x}", guid);
        return std::unexpected(DaemonCoreError::ProviderBindingFailure);
    }
    
    m_logger->info("Direct SHM binding configured for GUID 0x{:x} - {} Hz, {} channels, {} bytes/frame",
                   guid, m_shmControlBlock->sampleRateHz, m_shmControlBlock->channelCount, 
                   m_shmControlBlock->bytesPerFrame);

    // Store the active provider
    {
        std::lock_guard lk(m_streamsMutex);
        m_activeProviders[guid] = provider;
    }

    return {};
}

std::expected<void,DaemonCoreError>
DaemonCore::stopAudioStreams(uint64_t guid)
{
    m_logger->info("Stopping audio streams for device GUID: 0x{:x}", guid);
    
    auto deviceRes = getDeviceByGuid(guid);
    if (!deviceRes) return std::unexpected(deviceRes.error());
    auto device = deviceRes.value();

    // Phase 0: Stop the hardware first so no more writes happen
    m_logger->debug("Phase 0: Stopping device streams...");
    auto stopRes = device->stopStreams();
    if (!stopRes) {
        m_logger->error("Failed to stop streams for GUID 0x{:x}: {}", 
                       guid, static_cast<int>(stopRes.error()));
        if (m_streamStatusCb_toXPC) {
            m_streamStatusCb_toXPC(guid, true, int(stopRes.error()));
        }
        return std::unexpected(DaemonCoreError::StreamStopFailure);
    }

    // Phase 1: Unbind provider from SHM, then reset the ring counters
    m_logger->debug("Phase 1: Unbinding provider from SHM and resetting ring...");
    {
        std::lock_guard lk(m_streamsMutex);
        auto it = m_activeProviders.find(guid);
        if (it != m_activeProviders.end()) {
            auto* isochProvider = dynamic_cast<Isoch::IsochPacketProvider*>(it->second);
            if (isochProvider) {
                isochProvider->unbindSharedMemory();
                
                // Reset ring buffer indices so the ring is "empty" next time
                if (m_shmControlBlock) {
                    RTShmRing::WriteIndexProxy(*m_shmControlBlock)
                        .store(0, std::memory_order_relaxed);
                    RTShmRing::ReadIndexProxy(*m_shmControlBlock)
                        .store(0, std::memory_order_relaxed);
                    // Reset counters as well for clean state
                    RTShmRing::OverrunCountProxy(*m_shmControlBlock)
                        .store(0, std::memory_order_relaxed);
                    RTShmRing::UnderrunCountProxy(*m_shmControlBlock)
                        .store(0, std::memory_order_relaxed);
                    m_logger->debug("SHM ring buffer indices reset for GUID 0x{:x}", guid);
                }
                
                m_logger->debug("Provider unbound and SHM reset for GUID 0x{:x}", guid);
            }
            m_activeProviders.erase(it);
        }
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
    
    // Stop device streams first if device is still available
    if (dev) {
        dev->stopStreams();
        m_logger->debug("Stopped streams for disconnected device GUID: 0x{:x}", guid);
    }
    
    // Unbind from SHM and reset ring buffer
    {
        std::lock_guard lk(m_streamsMutex);
        auto it = m_activeProviders.find(guid);
        if (it != m_activeProviders.end()) {
            auto* isochProvider = dynamic_cast<Isoch::IsochPacketProvider*>(it->second);
            if (isochProvider) {
                isochProvider->unbindSharedMemory();
                
                // Reset ring buffer indices for clean state
                if (m_shmControlBlock) {
                    RTShmRing::WriteIndexProxy(*m_shmControlBlock)
                        .store(0, std::memory_order_relaxed);
                    RTShmRing::ReadIndexProxy(*m_shmControlBlock)
                        .store(0, std::memory_order_relaxed);
                    RTShmRing::OverrunCountProxy(*m_shmControlBlock)
                        .store(0, std::memory_order_relaxed);
                    RTShmRing::UnderrunCountProxy(*m_shmControlBlock)
                        .store(0, std::memory_order_relaxed);
                }
            }
            m_activeProviders.erase(it);
            m_logger->debug("Cleared SHM binding and reset ring for disconnected device GUID: 0x{:x}", guid);
        }
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

// bool DaemonCore::isLogCallbackToXpcSet() const {
//     return m_logCb_toXPC != nullptr;
// }

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

// Add this method to DaemonCore.cpp:
void DaemonCore::cleanupSharedMemory() {
    if (m_shmRawPtr) {
        if (munmap(m_shmRawPtr, m_shmSize) != 0) {
            m_logger->error("munmap failed: {} ({})", errno, std::strerror(errno));
        }
        m_shmRawPtr = nullptr;
    }
    
    if (m_shmFd != -1) {
        close(m_shmFd);
        m_shmFd = -1;
    }
    
    if (m_shmInitialized) {
        if (shm_unlink(m_shmName.c_str()) == 0) {
            m_logger->info("Unlinked shared memory '{}'", m_shmName);
        } else {
            m_logger->error("Failed to unlink shared memory '{}': {}", m_shmName, strerror(errno));
        }
    }
    
    m_shmControlBlock = nullptr;
    m_shmRingArray = nullptr;
    m_shmSize = 0;
    m_shmInitialized = false;
}

std::expected<void,DaemonCoreError> DaemonCore::validateSharedMemoryFormat() {
    if (!m_shmControlBlock) {
        return std::unexpected(DaemonCoreError::SharedMemoryValidationFailure);
    }
    
    if (!RTShmRing::ValidateFormat(*m_shmControlBlock)) {
        m_logger->error("SHM format validation failed: ABI={}, sampleRate={}, channels={}, bytesPerFrame={}",
                       m_shmControlBlock->abiVersion, m_shmControlBlock->sampleRateHz,
                       m_shmControlBlock->channelCount, m_shmControlBlock->bytesPerFrame);
        return std::unexpected(DaemonCoreError::SharedMemoryValidationFailure);
    }
    
    return {};
}

} // namespace FWA