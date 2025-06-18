#include "Isoch/AudioDeviceStream.hpp"
#include "FWA/CommandInterface.h"
#include "Isoch/core/AmdtpTransmitter.hpp" 
#include "Isoch/interfaces/ITransmitPacketProvider.hpp" 
#include <spdlog/spdlog.h>
#include <pthread.h>
#include <mach/mach_time.h>
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <mach/thread_act.h>
#include <sys/mman.h>      // For mlock
#include "Isoch/utils/RunLoopHelper.hpp"
#include "FWA/DeviceController.h"

// Real-Time Performance Fixes Applied:
// 1. FIXED: RunLoop thread real-time priority - now properly starts dedicated RT thread
// 2. FIXED: Callback dispatcher attachment - now happens inside the running RT thread
// 3. FIXED: Hot-path logging removed - no more trace logging in 8kHz callbacks
// 4. ADDED: Memory pre-faulting - prevents page faults during RT execution
// 5. ADDED: CPU affinity - prevents core migration latency
// These fixes eliminate DBC discontinuities and reduce worst-case latency

namespace FWA {

AudioDeviceStream::AudioDeviceStream(
                                     std::shared_ptr<AudioDevice> audioDevice,
                                     StreamType streamType,
                                     uint8_t devicePlugNumber,
                                     std::shared_ptr<spdlog::logger> logger,
                                     Isoch::PacketCallback dataPushCallback,
                                     void* dataPushRefCon,
                                     Isoch::MessageCallback messageCallback,
                                     void* messageRefCon,
                                     unsigned int cyclesPerSegment,
                                     unsigned int numSegments,
                                     unsigned int bufferSize,
                                     IOFWSpeed speed,
                                     IOFireWireLibDeviceRef interface
                                     )
: m_audioDevice(std::move(audioDevice)),
m_logger(std::move(logger)),
m_streamType(streamType),
m_devicePlugNumber(devicePlugNumber),
m_cyclesPerSegment(cyclesPerSegment),
m_numSegments(numSegments),
m_bufferSize(bufferSize),
m_isochSpeed(speed),
m_interface(interface),
m_packetCallback(dataPushCallback),
m_packetCallbackRefCon(dataPushRefCon),
m_messageCallback(messageCallback),
m_messageCallbackRefCon(messageRefCon)
{
    if (!m_logger) {
        m_logger = spdlog::default_logger();
    }
    
    m_logger->info("AudioDeviceStream: Created for plug {} with type {}",
                   m_devicePlugNumber, static_cast<int>(m_streamType));
}

AudioDeviceStream::~AudioDeviceStream() {
    // Ensure the stream is stopped and disconnected before destruction
    if (m_isActive) {
        stop();
    }
    
    if (m_isPlugConnected) {
        disconnectPlug();
    }
    
    // Stop the RunLoop thread if it's running
    if (m_runLoopActive) {
        m_runLoopActive = false;
        if (m_runLoopThread.joinable()) {
            m_runLoopThread.join();
        }
    }
    
    // Remove dispatchers from RunLoop if needed
    if (m_interface && m_runLoop) {
        (*m_interface)->RemoveIsochCallbackDispatcherFromRunLoop(m_interface);
        (*m_interface)->RemoveCallbackDispatcherFromRunLoop(m_interface);
    }
    
    m_logger->info("AudioDeviceStream: Destroyed for plug {}", m_devicePlugNumber);
}

std::expected<std::shared_ptr<AudioDeviceStream>, IOKitError> AudioDeviceStream::create(
                                                                                        std::shared_ptr<AudioDevice> audioDevice,
                                                                                        StreamType streamType,
                                                                                        uint8_t devicePlugNumber,
                                                                                        std::shared_ptr<spdlog::logger> logger,
                                                                                        Isoch::PacketCallback dataPushCallback,
                                                                                        void* dataPushRefCon,
                                                                                        Isoch::MessageCallback messageCallback,
                                                                                        void* messageRefCon,
                                                                                        unsigned int cyclesPerSegment,
                                                                                        unsigned int numSegments,
                                                                                        unsigned int bufferSize,
                                                                                        IOFWSpeed speed,
                                                                                        IOFireWireLibDeviceRef interface
                                                                                        )
{
    if (!audioDevice) {
        return std::unexpected(IOKitError::BadArgument);
    }
    
    // Create the stream object
    auto stream = std::shared_ptr<AudioDeviceStream>(new AudioDeviceStream(
                                                                           std::move(audioDevice),
                                                                           streamType,
                                                                           devicePlugNumber,
                                                                           std::move(logger),
                                                                           dataPushCallback,
                                                                           dataPushRefCon,
                                                                           messageCallback,
                                                                           messageRefCon,
                                                                           cyclesPerSegment,
                                                                           numSegments,
                                                                           bufferSize,
                                                                           speed,
                                                                           interface
                                                                           ));
    
    // Initialize the RunLoop thread
    auto runLoopResult = stream->initializeRunLoop();
    if (!runLoopResult) {
        stream->m_logger->error("AudioDeviceStream: Failed to initialize RunLoop: {}",
                                iokit_error_category().message(static_cast<int>(runLoopResult.error())));
        return std::unexpected(runLoopResult.error());
    }

    logRunLoopInfo("AudioDeviceStream", "create", stream->m_runLoop);
    
    // Create appropriate stream implementation based on type
    switch (streamType) {
        case StreamType::AmdtpReceiver: {
            // Create AmdtpReceiver
            try {
                // Configure the receiver with the new packet-based parameters
                Isoch::ReceiverConfig config;
                config.logger = stream->m_logger;
                
                // Convert from old cycle-based config to new packet-based config
                // Typical parameters conversion: assume each cycle contained 1 packet
                config.numGroups = stream->m_numSegments;
                config.packetsPerGroup = stream->m_cyclesPerSegment;
                config.packetDataSize = stream->m_bufferSize;
                config.callbackGroupInterval = 1; // Default to callback every group
                
                // Create a component factory for the receiver
                auto receiver = Isoch::ReceiverFactory::createStandardReceiver(config);
                
                // Initialize the receiver with the device interface
                stream->m_logger->info("AudioDeviceStream: Initializing AmdtpReceiver for plug {}", devicePlugNumber);
                auto initResult = receiver->initialize(interface);
                if (!initResult.has_value()) {
                    stream->m_logger->error("AudioDeviceStream: Failed to initialize AmdtpReceiver: {}",
                                            iokit_error_category().message(static_cast<int>(initResult.error())));
                    return std::unexpected(initResult.error());
                }
                
                // Store the implementation first, then set up callbacks
                stream->m_streamImpl = receiver;
                
                // Set up callbacks with proper refcon handling
                stream->setupReceiverCallbacks(receiver);
                
                stream->m_logger->info("AudioDeviceStream: Created AmdtpReceiver for plug {}", devicePlugNumber);
            }
            catch (const std::exception& ex) {
                stream->m_logger->error("AudioDeviceStream: Exception creating AmdtpReceiver: {}", ex.what());
                return std::unexpected(IOKitError::Error);
            }
            break;
        }
            
        case StreamType::AmdtpTransmitter: {
            try {
                // Configure the transmitter with the packet-based parameters
                Isoch::TransmitterConfig txConfig;
                txConfig.logger = stream->m_logger;
                
                // Set transmitter parameters
                txConfig.numGroups = stream->m_numSegments;
                txConfig.packetsPerGroup = stream->m_cyclesPerSegment;
                txConfig.clientBufferSize = stream->m_bufferSize;
                txConfig.sampleRate = 44100.0; // Default sample rate
                txConfig.numChannels = 2;      // Default stereo
                txConfig.initialSpeed = speed;
                // --- Set transmission type for refactor testing ---
                txConfig.transmissionType = Isoch::TransmissionType::Blocking; // Or NonBlocking
                // test non-blocking behavior
                // txConfig.transmissionType = Isoch::TransmissionType::NonBlocking; // Default to current behavior
                // txConfig.transmissionType = Isoch::TransmissionType::NonBlocking;

                // Create the transmitter
                auto transmitter = Isoch::AmdtpTransmitter::create(txConfig);
                
                // Initialize the transmitter with the device interface
                stream->m_logger->info("AudioDeviceStream: Initializing AmdtpTransmitter for plug {}", devicePlugNumber);
                auto initResult = transmitter->initialize(interface);
                if (!initResult.has_value()) {
                    stream->m_logger->error("AudioDeviceStream: Failed to initialize AmdtpTransmitter: {}",
                                           iokit_error_category().message(static_cast<int>(initResult.error())));
                    return std::unexpected(initResult.error());
                }
                
                // Store the implementation
                stream->m_streamImpl = transmitter;
                
                // Set up message callback
                if (messageCallback) {
                    transmitter->setMessageCallback(messageCallback, messageRefCon);
                }
                
                stream->m_logger->info("AudioDeviceStream: Created AmdtpTransmitter for plug {}", devicePlugNumber);
            }
            catch (const std::exception& ex) {
                stream->m_logger->error("AudioDeviceStream: Exception creating AmdtpTransmitter: {}", ex.what());
                return std::unexpected(IOKitError::Error);
            }
            break;
        }
            
        case StreamType::UniversalReceiver:
        case StreamType::UniversalTransmitter:
            stream->m_logger->error("AudioDeviceStream: Legacy stream types not supported");
            return std::unexpected(IOKitError::Unsupported);
            
        default:
            return std::unexpected(IOKitError::BadArgument);
    }
    
    return stream;
}

std::expected<std::shared_ptr<AudioDeviceStream>, IOKitError> AudioDeviceStream::createReceiverForDevicePlug(
                                                                                                             std::shared_ptr<AudioDevice> audioDevice,
                                                                                                             uint8_t devicePlugNumber,
                                                                                                             Isoch::PacketCallback dataPushCallback,
                                                                                                             void* dataPushRefCon,
                                                                                                             Isoch::MessageCallback messageCallback,
                                                                                                             void* messageRefCon,
                                                                                                             std::shared_ptr<spdlog::logger> logger,
                                                                                                             unsigned int cyclesPerSegment,
                                                                                                             unsigned int numSegments,
                                                                                                             unsigned int cycleBufferSize,
                                                                                                             IOFireWireLibDeviceRef interface
                                                                                                             )
{
    // Use the general create method with specific parameters for a receiver
    return create(
                  std::move(audioDevice),
                  StreamType::AmdtpReceiver,
                  devicePlugNumber,
                  std::move(logger),
                  dataPushCallback,
                  dataPushRefCon,
                  messageCallback,
                  messageRefCon,
                  cyclesPerSegment,
                  numSegments,
                  cycleBufferSize,
                  kFWSpeed100MBit,  // Default speed, can be changed later
                  interface
                  );
}

std::expected<std::shared_ptr<AudioDeviceStream>, IOKitError> AudioDeviceStream::createTransmitterForDevicePlug(
                                                                                                                std::shared_ptr<AudioDevice> audioDevice,
                                                                                                                uint8_t devicePlugNumber,
                                                                                                                Isoch::PacketCallback dataPullCallback,
                                                                                                                void* dataPullRefCon,
                                                                                                                Isoch::MessageCallback messageCallback,
                                                                                                                void* messageRefCon,
                                                                                                                std::shared_ptr<spdlog::logger> logger,
                                                                                                                const FWA::Isoch::TransmitterConfig& config,
                                                                                                                IOFireWireLibDeviceRef interface
                                                                                                                )
{
    if (!audioDevice) {
        return std::unexpected(IOKitError::BadArgument);
    }
    
    // Create the stream object with transmitter-specific values from config
    unsigned int transmitBufferSize = 2 * config.numGroups * config.packetsPerGroup * config.packetDataSize;
    auto stream = std::shared_ptr<AudioDeviceStream>(new AudioDeviceStream(
        audioDevice,
        StreamType::AmdtpTransmitter,
        devicePlugNumber,
        logger,
        nullptr,                // No data push callback for transmitter
        nullptr,                // No refCon for data push
        messageCallback,
        messageRefCon,
        config.packetsPerGroup,
        config.numGroups,
        transmitBufferSize,
        config.initialSpeed,
        interface
    ));
    
    if (!logger) {
        stream->m_logger = spdlog::default_logger();
    }
    
    // Create AmdtpTransmitter directly with the provided config
    try {
        // Use the provided config directly instead of building a new one
        FWA::Isoch::TransmitterConfig txConfig = config;
        txConfig.logger = stream->m_logger;
        
        // Create the transmitter
        auto transmitter = Isoch::AmdtpTransmitter::create(txConfig);
        
        // Initialize the transmitter with the device interface
        stream->m_logger->info("AudioDeviceStream: Initializing AmdtpTransmitter for plug {}", devicePlugNumber);
        auto initResult = transmitter->initialize(interface);
        if (!initResult.has_value()) {
            stream->m_logger->error("AudioDeviceStream: Failed to initialize AmdtpTransmitter: {}",
                                   iokit_error_category().message(static_cast<int>(initResult.error())));
            return std::unexpected(initResult.error());
        }
        
        // Store the implementation
        stream->m_streamImpl = transmitter;
        
        // Set up message callback
        if (messageCallback) {
            transmitter->setMessageCallback(messageCallback, messageRefCon);
        }
        
        stream->m_logger->info("AudioDeviceStream: Created AmdtpTransmitter for plug {}", devicePlugNumber);
    }
    catch (const std::exception& ex) {
        stream->m_logger->error("AudioDeviceStream: Exception creating AmdtpTransmitter: {}", ex.what());
        return std::unexpected(IOKitError::Error);
    }
    
    return stream;
}

std::expected<void, IOKitError> AudioDeviceStream::start()
{
    if (m_isActive) {
        return {};  // Already active, nothing to do
    }
    
    // Ensure we have a connection to the device plug
    if (!m_isPlugConnected) {
        auto connectResult = connectPlug();
        if (!connectResult) {
            return std::unexpected(connectResult.error());
        }
    }
    
    // Handle different stream types
    if (std::holds_alternative<std::shared_ptr<Isoch::AmdtpReceiver>>(m_streamImpl)) {
        auto receiver = std::get<std::shared_ptr<Isoch::AmdtpReceiver>>(m_streamImpl);
        
        auto result = receiver->startReceive();
        if (!result.has_value()) {
            m_logger->error("AudioDeviceStream: Failed to start AmdtpReceiver: {}",
                            iokit_error_category().message(static_cast<int>(result.error())));
            return std::unexpected(result.error());
        }
    }
    else if (std::holds_alternative<std::shared_ptr<Isoch::AmdtpTransmitter>>(m_streamImpl)) {
        auto transmitter = std::get<std::shared_ptr<Isoch::AmdtpTransmitter>>(m_streamImpl);
        
        auto result = transmitter->startTransmit();
        if (!result.has_value()) {
            m_logger->error("AudioDeviceStream: Failed to start AmdtpTransmitter: {}",
                           iokit_error_category().message(static_cast<int>(result.error())));
            return std::unexpected(result.error());
        }
    }
    else {
        m_logger->warn("AudioDeviceStream: Unsupported stream type");
        return std::unexpected(IOKitError::Unsupported);
    }
    
    m_isActive = true;
    
    // Pre-fault buffers to prevent page faults during RT execution
    prefaultBuffers();
    
    m_logger->info("AudioDeviceStream: Started stream for plug {}", m_devicePlugNumber);
    return {};
}

std::expected<void, IOKitError> AudioDeviceStream::stop()
{
    if (!m_isActive) {
        return {};  // Already stopped, nothing to do
    }
    
    // Handle different stream types
    if (std::holds_alternative<std::shared_ptr<Isoch::AmdtpReceiver>>(m_streamImpl)) {
        auto receiver = std::get<std::shared_ptr<Isoch::AmdtpReceiver>>(m_streamImpl);
        
        auto result = receiver->stopReceive();
        if (!result.has_value()) {
            m_logger->error("AudioDeviceStream: Failed to stop AmdtpReceiver: {}",
                            iokit_error_category().message(static_cast<int>(result.error())));
            return std::unexpected(result.error());
        }
    }
    else if (std::holds_alternative<std::shared_ptr<Isoch::AmdtpTransmitter>>(m_streamImpl)) {
        auto transmitter = std::get<std::shared_ptr<Isoch::AmdtpTransmitter>>(m_streamImpl);
        
        auto result = transmitter->stopTransmit();
        if (!result.has_value()) {
            m_logger->error("AudioDeviceStream: Failed to stop AmdtpTransmitter: {}",
                           iokit_error_category().message(static_cast<int>(result.error())));
            return std::unexpected(result.error());
        }
    }
    else {
        m_logger->warn("AudioDeviceStream: Unsupported stream type");
        return std::unexpected(IOKitError::Unsupported);
    }
    
    m_isActive = false;
    m_logger->info("AudioDeviceStream: Stopped stream for plug {}", m_devicePlugNumber);
    return {};
}

void AudioDeviceStream::setMessageCallback(Isoch::MessageCallback callback, void* refCon)
{
    m_messageCallback = callback;
    m_messageCallbackRefCon = refCon;
    
    // Update the callback in the stream implementation if it exists
    if (std::holds_alternative<std::shared_ptr<Isoch::AmdtpReceiver>>(m_streamImpl)) {
        auto receiver = std::get<std::shared_ptr<Isoch::AmdtpReceiver>>(m_streamImpl);
        setupReceiverCallbacks(receiver);  // Use the helper to set up callbacks properly
        m_logger->info("AudioDeviceStream: Updated message callback for AmdtpReceiver");
    }
    else {
        m_logger->warn("AudioDeviceStream: Cannot update message callback for this stream type");
    }
}

void AudioDeviceStream::setPacketCallback(Isoch::PacketCallback callback, void* refCon)
{
//    logCallbackThreadInfo("AudioDeviceStream", "setPacketCallback", this);
    m_packetCallback = callback;
    m_packetCallbackRefCon = refCon;
    
    // Update the callback in the stream implementation if it exists
    if (std::holds_alternative<std::shared_ptr<Isoch::AmdtpReceiver>>(m_streamImpl)) {
        auto receiver = std::get<std::shared_ptr<Isoch::AmdtpReceiver>>(m_streamImpl);
        setupReceiverCallbacks(receiver);  // Use the helper to set up callbacks properly
        m_logger->info("AudioDeviceStream: Updated packet callback for AmdtpReceiver");
    }
    else {
        m_logger->warn("AudioDeviceStream: Cannot update packet callback for this stream type");
    }
}

void AudioDeviceStream::setPacketPullCallback(Isoch::PacketCallback callback, void* refCon)
{
    m_packetPullCallback = callback;
    m_packetPullCallbackRefCon = refCon;
    
    // Transmitter not yet implemented
    m_logger->warn("AudioDeviceStream: Cannot update packet pull callback, transmitter not implemented");
}

std::expected<void, IOKitError> AudioDeviceStream::initializeRunLoop() {
    // Start a dedicated real-time RunLoop thread for this stream
    m_runLoopActive = true;
    
    // Create the RunLoop thread
    m_runLoopThread = std::thread([this] {
        pthread_setname_np("FWA_StreamRunLoop");
        
        // Make this thread real-time with CPU affinity
        makeCurrentThreadRealtime();
        
        // Get the RunLoop for this thread
        m_runLoop = CFRunLoopGetCurrent();
        
        m_logger->info("AudioDeviceStream: Dedicated RunLoop thread started: {:p} on thread {}",
                      (void*)m_runLoop,
                      static_cast<uint64_t>(pthread_mach_thread_np(pthread_self())));
        
        // Add FireWire dispatchers to this dedicated RunLoop
        if (m_interface) {
            IOReturn ret = (*m_interface)->AddCallbackDispatcherToRunLoop(m_interface, m_runLoop);
            if (ret != kIOReturnSuccess) {
                m_logger->error("AudioDeviceStream: Failed to add callback dispatcher: 0x{:08X}", ret);
                m_runLoopActive = false;
                return;
            }
            
            ret = (*m_interface)->AddIsochCallbackDispatcherToRunLoop(m_interface, m_runLoop);
            if (ret != kIOReturnSuccess) {
                m_logger->error("AudioDeviceStream: Failed to add isoch callback dispatcher: 0x{:08X}", ret);
                m_runLoopActive = false;
                return;
            }
            
            m_logger->info("AudioDeviceStream: Added FireWire dispatchers to dedicated RunLoop");
        }
        
        // Run the RunLoop until stopped
        while (m_runLoopActive) {
            // Run the RunLoop for a short interval to allow periodic checking
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true);
        }
        
        m_logger->info("AudioDeviceStream: RunLoop thread exiting");
    });
    
    // Wait a moment for the thread to start and set up the RunLoop
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    if (!m_runLoopActive) {
        m_logger->error("AudioDeviceStream: Failed to start RunLoop thread");
        if (m_runLoopThread.joinable()) {
            m_runLoopThread.join();
        }
        return std::unexpected(IOKitError::InternalError);
    }
    
    m_logger->info("AudioDeviceStream: RunLoop initialization completed successfully");
    return {};
}



// Static helper to make current thread real-time with CPU affinity
void AudioDeviceStream::makeCurrentThreadRealtime(uint32_t periodNs,
                                                  uint32_t computeNs,
                                                  uint32_t constraintNs)
{
    mach_port_t self = mach_thread_self();
    
    // 1. Set CPU affinity to prevent core migration
    thread_affinity_policy_data_t affinity = { 3 }; // Arbitrary non-zero tag
    kern_return_t affinity_result = thread_policy_set(
        self,
        THREAD_AFFINITY_POLICY,
        (thread_policy_t)&affinity,
        THREAD_AFFINITY_POLICY_COUNT
    );
    
    if (affinity_result != KERN_SUCCESS) {
        spdlog::warn("AudioDeviceStream: Failed to set CPU affinity, error: {}", affinity_result);
    }
    
    // 2. Set real-time scheduling constraints
    // Get Mach timebase for accurate time conversion
    mach_timebase_info_data_t tbi;
    mach_timebase_info(&tbi);
    auto toAbsoluteTime = [&](uint32_t nanoseconds) -> uint64_t {
        return (uint64_t)nanoseconds * tbi.denom / tbi.numer;
    };
    
    thread_time_constraint_policy_data_t policy;
    policy.period      = toAbsoluteTime(periodNs);
    policy.computation = toAbsoluteTime(computeNs);
    policy.constraint  = toAbsoluteTime(constraintNs);
    policy.preemptible = 1;
    
    kern_return_t result = thread_policy_set(
        self,
        THREAD_TIME_CONSTRAINT_POLICY,
        (thread_policy_t)&policy,
        THREAD_TIME_CONSTRAINT_POLICY_COUNT
    );
    
    if (result == KERN_SUCCESS) {
        spdlog::info("AudioDeviceStream: Current thread set to real-time priority ({}µs period, {}µs compute, {}µs constraint)",
                    periodNs/1000, computeNs/1000, constraintNs/1000);
    } else {
        spdlog::warn("AudioDeviceStream: Failed to set current thread to real-time priority, error: {}", result);
    }
}

std::expected<void, IOKitError> AudioDeviceStream::connectPlug()
{
    if (m_isPlugConnected) {
        return {};  // Already connected
    }
    
    // Implementation depends on stream type and plug direction
    switch (m_streamType) {
        case StreamType::AmdtpReceiver: {
            // For receiver, connect to output plug on device
            auto device = m_audioDevice;
            if (!device) {
                return std::unexpected(IOKitError::NoDevice);
            }
            
            auto cmdInterface = device->getCommandInterface();
            if (!cmdInterface) {
                return std::unexpected(IOKitError::NotReady);
            }
            
            // Access the avcInterface using the getter method
            auto avcInterface = cmdInterface->getAVCInterface();
            if (!avcInterface) {
                return std::unexpected(IOKitError::NotReady);
            }
            
            m_logger->info("AudioDeviceStream: Connecting to device output plug {} on channel {}",
                           m_devicePlugNumber, m_isochChannel);
            
            // Connect to the device's output plug using the built-in makeP2POutputConnection
            IOReturn result = (*avcInterface)->makeP2POutputConnection(
                                                                       avcInterface,
                                                                       m_devicePlugNumber,  // The device plug number
                                                                       m_isochChannel,      // The isochronous channel to use
                                                                       m_isochSpeed         // The speed to use
                                                                       );
            
            if (result != kIOReturnSuccess) {
                m_logger->error("AudioDeviceStream: Failed to connect to device output plug {}: 0x{:x}",
                                m_devicePlugNumber, result);
                // Convert IOReturn to our IOKitError enum
                return std::unexpected(static_cast<IOKitError>(result));
            }
            
            m_logger->info("AudioDeviceStream: Connected to device output plug {} on channel {}",
                           m_devicePlugNumber, m_isochChannel);
            m_isPlugConnected = true;
            break;
        }
            
        case StreamType::AmdtpTransmitter: {
            auto device = m_audioDevice;
            if (!device) return std::unexpected(IOKitError::NoDevice);
            auto cmdInterface = device->getCommandInterface();
            if (!cmdInterface) return std::unexpected(IOKitError::NotReady);
            auto avcInterface = cmdInterface->getAVCInterface();
            if (!avcInterface) return std::unexpected(IOKitError::NotReady);

            m_logger->info("AudioDeviceStream: Connecting to device INPUT plug {} on channel {}",
                       m_devicePlugNumber, m_isochChannel);

            // Connect to the device's *input* plug for transmitting
            IOReturn result = (*avcInterface)->makeP2PInputConnection(
                avcInterface,
                m_devicePlugNumber,  // The device *input* plug number
                m_isochChannel      // The isochronous channel to use
            );

            if (result != kIOReturnSuccess) {
                m_logger->error("AudioDeviceStream: Failed to connect to device input plug {}: 0x{:x}",
                            m_devicePlugNumber, result);
                return std::unexpected(static_cast<IOKitError>(result));
            }

            m_logger->info("AudioDeviceStream: Connected to device input plug {} on channel {}",
                       m_devicePlugNumber, m_isochChannel);
            m_isPlugConnected = true;
            break;
        }
            
        default:
            return std::unexpected(IOKitError::Unsupported);
    }
    
    return {};
}

std::expected<void, IOKitError> AudioDeviceStream::disconnectPlug()
{
    if (!m_isPlugConnected) {
        return {};  // Already disconnected
    }
    
    // Implementation depends on stream type and plug direction
    switch (m_streamType) {
        case StreamType::AmdtpReceiver: {
            // For receiver, disconnect from output plug on device
            auto device = m_audioDevice;
            if (!device) {
                return std::unexpected(IOKitError::NoDevice);
            }
            
            auto cmdInterface = device->getCommandInterface();
            if (!cmdInterface) {
                return std::unexpected(IOKitError::NotReady);
            }
            
            // Access the avcInterface using the getter method
            auto avcInterface = cmdInterface->getAVCInterface();
            if (!avcInterface) {
                return std::unexpected(IOKitError::NotReady);
            }
            
            // Disconnect from the device's output plug using the built-in breakP2POutputConnection
            IOReturn result = (*avcInterface)->breakP2POutputConnection(
                                                                        avcInterface,
                                                                        m_devicePlugNumber  // The device plug number
                                                                        );
            
            if (result != kIOReturnSuccess) {
                m_logger->error("AudioDeviceStream: Failed to disconnect from device output plug {}: 0x{:x}",
                                m_devicePlugNumber, result);
                // Convert IOReturn to our IOKitError enum
                return std::unexpected(static_cast<IOKitError>(result));
            }
            
            m_logger->info("AudioDeviceStream: Disconnected from device output plug {}", m_devicePlugNumber);
            m_isPlugConnected = false;
            break;
        }
            
        case StreamType::AmdtpTransmitter: {
            auto device = m_audioDevice;
            if (!device) return std::unexpected(IOKitError::NoDevice);
            auto cmdInterface = device->getCommandInterface();
            if (!cmdInterface) return std::unexpected(IOKitError::NotReady);
            auto avcInterface = cmdInterface->getAVCInterface();
            if (!avcInterface) return std::unexpected(IOKitError::NotReady);

            m_logger->info("AudioDeviceStream: Disconnecting from device input plug {}", m_devicePlugNumber);

            // Disconnect from the device's *input* plug
            IOReturn result = (*avcInterface)->breakP2PInputConnection(
                avcInterface,
                m_devicePlugNumber  // The device input plug number
            );

            if (result != kIOReturnSuccess) {
                m_logger->error("AudioDeviceStream: Failed to disconnect from device input plug {}: 0x{:x}",
                            m_devicePlugNumber, result);
                return std::unexpected(static_cast<IOKitError>(result));
            }

            m_logger->info("AudioDeviceStream: Disconnected from device input plug {}", m_devicePlugNumber);
            m_isPlugConnected = false;
            break;
        }
            
        default:
            return std::unexpected(IOKitError::Unsupported);
    }
    
    return {};
}

// Static callback handler implementations with correct signatures
void AudioDeviceStream::handlePacketReceived(const uint8_t* data, size_t length, void* refCon)
{
    // Log callback using RunLoopHelper
//    logCallbackThreadInfo("AudioDeviceStream", "handlePacketReceived", refCon);
    
    // Cast refCon to AudioDeviceStream
    auto* self = static_cast<AudioDeviceStream*>(refCon);
    
    // Call the user-provided callback with the RefCon
    if (self && self->m_packetCallback) {
        self->m_packetCallback(data, length, self->m_packetCallbackRefCon);
    }
}

// New static handler for processed data
void AudioDeviceStream::handleProcessedDataStatic(
    const std::vector<Isoch::ProcessedSample>& samples,
    const Isoch::PacketTimingInfo& timing,
    void* refCon)
{
    // Log callback using RunLoopHelper
//    logCallbackThreadInfo("AudioDeviceStream", "handleProcessedDataStatic", refCon);
    
    // Cast refCon to AudioDeviceStream
    auto* self = static_cast<AudioDeviceStream*>(refCon);
    
    // Forward to instance method
    if (self) {
        self->handleProcessedDataImpl(samples, timing);
    }
}

// New instance method to handle processed data
void AudioDeviceStream::handleProcessedDataImpl(
    const std::vector<Isoch::ProcessedSample>& samples,
    const Isoch::PacketTimingInfo& timing)
{
    // For backward compatibility with clients expecting raw packets:
    // Call the legacy packet callback with nullptr to signal data arrival
    // The client should eventually transition to reading from ring buffer directly
    if (m_packetCallback) {
        // NO LOGGING in the audio hot path - removed trace logging for performance
        m_packetCallback(nullptr, 0, m_packetCallbackRefCon);
    }
}

void AudioDeviceStream::handleMessageReceived(uint32_t message, uint32_t param1, uint32_t param2, void* refCon)
{
    // Log callback using RunLoopHelper
    logCallbackThreadInfo("AudioDeviceStream", "handleMessageReceived", refCon);
    
    // Cast refCon to AudioDeviceStream
    auto* self = static_cast<AudioDeviceStream*>(refCon);
    
    // Call the user-provided callback with the RefCon
    if (self && self->m_messageCallback) {
        self->m_messageCallback(message, param1, param2, self->m_messageCallbackRefCon);
    }
}

// Modified implementation to use the new callback mechanism
void AudioDeviceStream::setupReceiverCallbacks(std::shared_ptr<Isoch::AmdtpReceiver> receiver)
{
    if (!receiver) {
        m_logger->error("Cannot setup callbacks for null receiver");
        return;
    }
    
    // Set up the processed data callback with this instance as refCon
    receiver->setProcessedDataCallback(handleProcessedDataStatic, this);
    m_logger->info("[AudioDeviceStream] Receiver processed data callback set up with refCon: {}", (void*)this);
    
    // Set up the message callback with this instance as refCon
    receiver->setMessageCallback(handleMessageReceived, this);
    
    m_logger->info("Receiver callbacks set up successfully");
}

std::expected<void, IOKitError> AudioDeviceStream::setIsochChannel(uint32_t channel)
{
    if (m_isActive) {
        m_logger->error("AudioDeviceStream: Cannot change channel while stream is active");
        return std::unexpected(IOKitError::Busy);
    }
    
    m_isochChannel = channel;
    m_logger->info("AudioDeviceStream: Set isochronous channel to {}", channel);
    
    // Configure the channel in the stream implementation
    if (std::holds_alternative<std::shared_ptr<Isoch::AmdtpReceiver>>(m_streamImpl)) {
        auto receiver = std::get<std::shared_ptr<Isoch::AmdtpReceiver>>(m_streamImpl);
        return receiver->configure(m_isochSpeed, channel);
    }
    else if (std::holds_alternative<std::shared_ptr<Isoch::AmdtpTransmitter>>(m_streamImpl)) {
        auto transmitter = std::get<std::shared_ptr<Isoch::AmdtpTransmitter>>(m_streamImpl);
        return transmitter->configure(m_isochSpeed, channel);
    }
    else {
        m_logger->warn("AudioDeviceStream: Cannot set channel for this stream type");
        return std::unexpected(IOKitError::Unsupported);
    }
}

std::expected<void, IOKitError> AudioDeviceStream::setIsochSpeed(IOFWSpeed speed)
{
    if (m_isActive) {
        m_logger->error("AudioDeviceStream: Cannot change speed while stream is active");
        return std::unexpected(IOKitError::Busy);
    }
    
    m_isochSpeed = speed;
    m_logger->info("AudioDeviceStream: Set isochronous speed to {}", static_cast<int>(speed));
    
    // Configure the speed in the stream implementation
    if (std::holds_alternative<std::shared_ptr<Isoch::AmdtpReceiver>>(m_streamImpl)) {
        auto receiver = std::get<std::shared_ptr<Isoch::AmdtpReceiver>>(m_streamImpl);
        return receiver->configure(speed, m_isochChannel);
    }
    else if (std::holds_alternative<std::shared_ptr<Isoch::AmdtpTransmitter>>(m_streamImpl)) {
        auto transmitter = std::get<std::shared_ptr<Isoch::AmdtpTransmitter>>(m_streamImpl);
        return transmitter->configure(speed, m_isochChannel);
    }
    else {
        m_logger->warn("AudioDeviceStream: Cannot set speed for this stream type");
        return std::unexpected(IOKitError::Unsupported);
    }
}

std::expected<void, IOKitError> AudioDeviceStream::checkIOReturn(IOReturn result)
{
    if (result != kIOReturnSuccess) {
        return std::unexpected(static_cast<IOKitError>(result));
    }
    return {};
}

raul::RingBuffer* AudioDeviceStream::getReceiverRingBuffer() const {
    // Check if the variant holds an AmdtpReceiver
    if (std::holds_alternative<std::shared_ptr<Isoch::AmdtpReceiver>>(m_streamImpl)) {
        // Get the receiver shared_ptr
        auto receiver = std::get<std::shared_ptr<Isoch::AmdtpReceiver>>(m_streamImpl);
        if (receiver) {
            // Call the receiver's accessor method
            return receiver->getAppRingBuffer();
        } else {
            // Log if receiver pointer is somehow null (shouldn't happen if variant holds it)
            if(m_logger) m_logger->error("AudioDeviceStream::getReceiverRingBuffer: Held receiver pointer is null!");
            return nullptr;
        }
    } else {
        // This stream is not an AmdtpReceiver type
        // if(m_logger) m_logger->trace("AudioDeviceStream::getReceiverRingBuffer: Stream is not an AmdtpReceiver.");
        return nullptr;
    }
}

bool AudioDeviceStream::pushTransmitData(const void* buffer, size_t bufferSizeInBytes) {
    if (std::holds_alternative<std::shared_ptr<Isoch::AmdtpTransmitter>>(m_streamImpl)) {
        auto transmitter = std::get<std::shared_ptr<Isoch::AmdtpTransmitter>>(m_streamImpl);
        if (transmitter) {
            return transmitter->pushAudioData(buffer, bufferSizeInBytes);
        }
    }
    if (m_logger) m_logger->warn("pushTransmitData called on non-transmitter stream");
    return false;
}

Isoch::ITransmitPacketProvider* AudioDeviceStream::getTransmitPacketProvider() const {
    if (std::holds_alternative<std::shared_ptr<Isoch::AmdtpTransmitter>>(m_streamImpl)) {
        // Get the underlying transmitter object
        const auto& transmitter_ptr = std::get<std::shared_ptr<Isoch::AmdtpTransmitter>>(m_streamImpl);
        if (transmitter_ptr) {
            // Call the transmitter's getter
            return transmitter_ptr->getPacketProvider();
        } else {
             if (m_logger) m_logger->error("getTransmitPacketProvider: Transmitter pointer in variant is null!");
        }
    } else {
         if (m_logger) m_logger->trace("getTransmitPacketProvider: Stream is not a transmitter.");
    }
    return nullptr; // Return null if not a transmitter or not initialized
}

// Pre-fault buffers to prevent page faults during RT execution
void AudioDeviceStream::prefaultBuffers()
{
    // Pre-fault receiver ring buffer if present
    if (auto ringBuffer = getReceiverRingBuffer()) {
        // Note: RingBuffer doesn't expose data() method, so we can't mlock it directly
        // For now, we'll at least pre-fault by reading/writing the capacity info
        uint32_t capacity = ringBuffer->capacity();
        uint32_t readSpace = ringBuffer->read_space();
        uint32_t writeSpace = ringBuffer->write_space();
        
        m_logger->debug("AudioDeviceStream: Ring buffer info - capacity: {}, read_space: {}, write_space: {}", 
                       capacity, readSpace, writeSpace);
        
        // TODO: Ideally we'd want to add a method to RingBuffer to expose the internal buffer
        // for memory locking, or add mlockall() at application startup
        m_logger->debug("AudioDeviceStream: Ring buffer detected but cannot pre-fault (no data() accessor)");
    }
    
    // Pre-fault transmitter buffers if present
    if (std::holds_alternative<std::shared_ptr<Isoch::AmdtpTransmitter>>(m_streamImpl)) {
        auto transmitter = std::get<std::shared_ptr<Isoch::AmdtpTransmitter>>(m_streamImpl);
        if (transmitter) {
            // Note: We'd need a method on AmdtpTransmitter to get its internal buffer
            // For now, just log that we're a transmitter
            m_logger->debug("AudioDeviceStream: Transmitter detected, but no buffer pre-faulting implemented yet");
        }
    }
    
    // Alternative: Pre-fault our own stack to prevent page faults during callbacks
    volatile char stack_pretouch[8192]; // 8KB should cover callback stack usage
    memset((void*)stack_pretouch, 0, sizeof(stack_pretouch));
    (void)stack_pretouch; // Prevent compiler optimization
    
    m_logger->debug("AudioDeviceStream: Pre-faulted callback stack memory");
}


} // namespace FWA