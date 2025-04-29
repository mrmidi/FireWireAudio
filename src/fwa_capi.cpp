#include "fwa_capi.h"
#include "FWA/DeviceController.h"
#include "FWA/AudioDevice.h"
#include "FWA/CommandInterface.h"
#include "FWA/DeviceInfo.hpp"
#include "FWA/IOKitFireWireDeviceDiscovery.h"
#include "FWA/DeviceParser.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_sinks.h> // For stdout_sink_mt / stderr_sink_mt
#include <spdlog/pattern_formatter.h>  // <--- ADD THIS INCLUDE
#include <spdlog/logger.h>          // Include for spdlog::logger construction/registration
#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <expected>

struct FWAEngine {
    std::shared_ptr<FWA::DeviceController> controller;
    FWALogCallback log_callback = nullptr;
    void* log_user_data = nullptr;
    FWADeviceNotificationCallback notification_callback = nullptr;
    void* notification_user_data = nullptr;
    std::shared_ptr<spdlog::logger> logger;
    std::string logger_name;
    FWA::IOKitFireWireDeviceDiscovery* discovery_ptr = nullptr;
};

namespace {

template<typename Mutex>
class FWACCallbackSink : public spdlog::sinks::base_sink<Mutex> {
public:
    FWACCallbackSink(FWAEngineRef engine_ref) : engine_(engine_ref) {}
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        if (engine_ && engine_->log_callback) {
            spdlog::memory_buf_t formatted;
            spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);
            FWALogLevel level = FWA_LOG_LEVEL_INFO;
            switch (msg.level) {
                case spdlog::level::trace:    level = FWA_LOG_LEVEL_TRACE; break;
                case spdlog::level::debug:    level = FWA_LOG_LEVEL_DEBUG; break;
                case spdlog::level::info:     level = FWA_LOG_LEVEL_INFO; break;
                case spdlog::level::warn:     level = FWA_LOG_LEVEL_WARN; break;
                case spdlog::level::err:      level = FWA_LOG_LEVEL_ERROR; break;
                case spdlog::level::critical: level = FWA_LOG_LEVEL_CRITICAL; break;
                case spdlog::level::off:      level = FWA_LOG_LEVEL_OFF; break;
                default: break;
            }
            // FIX: Pass user_data, then level, then message
            engine_->log_callback(engine_->log_user_data, level, fmt::to_string(formatted).c_str());
        }
    }
    void flush_() override {}
private:
    FWAEngineRef engine_;
};

template <typename Func>
IOReturn safe_execute(FWAEngineRef engine, Func&& func) {
    if (!engine || !engine->controller) {
        return kIOReturnBadArgument;
    }
    try {
        auto result = func(engine->controller);
        if (result) {
            return kIOReturnSuccess;
        } else {
            return static_cast<IOReturn>(result.error());
        }
    } catch (const std::bad_alloc&) {
        return kIOReturnNoMemory;
    } catch (const std::exception& e) {
        if(engine->logger) { engine->logger->critical("C++ Exception in C API: {}", e.what()); }
        else { std::cerr << "C++ Exception in C API: " << e.what() << std::endl; }
        return kIOReturnInternalError;
    } catch (...) {
         if(engine->logger) { engine->logger->critical("Unknown C++ Exception in C API"); }
         else { std::cerr << "Unknown C++ Exception in C API" << std::endl; }
        return kIOReturnInternalError;
    }
}

template <typename Func>
IOReturn safe_execute_device(FWADeviceRef device_ref, Func&& func) {
    if (!device_ref) {
        return kIOReturnBadArgument;
    }
    FWA::AudioDevice* device = reinterpret_cast<FWA::AudioDevice*>(device_ref);
    try {
        auto result = func(device);
        if (result) {
            return kIOReturnSuccess;
        } else {
            return static_cast<IOReturn>(result.error());
        }
    } catch (const std::bad_alloc&) {
        return kIOReturnNoMemory;
    } catch (const std::exception& e) {
         auto logger = spdlog::get("fwa_global");
         if (logger) { logger->critical("C++ Exception in C API (Device {}): {}", device->getGuid(), e.what()); }
         else { std::cerr << "C++ Exception in C API (Device): " << e.what() << std::endl; }
        return kIOReturnInternalError;
    } catch (...) {
         auto logger = spdlog::get("fwa_global");
         if (logger) { logger->critical("Unknown C++ Exception in C API (Device {})", device->getGuid()); }
         else { std::cerr << "Unknown C++ Exception in C API (Device)" << std::endl; }
        return kIOReturnInternalError;
    }
}

std::shared_ptr<spdlog::logger> get_global_logger() {
    auto logger = spdlog::get("fwa_global");
    if (!logger) {
        try {
            // Use non-colored sink
            auto sink = std::make_shared<spdlog::sinks::stderr_sink_mt>(); // USE NON-COLORED SINK
            logger = std::make_shared<spdlog::logger>("fwa_global", sink); // CREATE LOGGER WITH SINK
            spdlog::register_logger(logger); // REGISTER IT
            logger->set_level(spdlog::level::debug);
            logger->flush_on(spdlog::level::trace);
        } catch (const spdlog::spdlog_ex& ex) {
            std::cerr << "Global logger init failed: " << ex.what() << std::endl;
            return nullptr;
        }
    }
    return logger;
}

} // anonymous namespace

FWAEngineRef FWAEngine_Create() {
    FWAEngine* engine = nullptr;
    try {
        engine = new FWAEngine();
        engine->logger_name = "fwa_engine_" + std::to_string(reinterpret_cast<uintptr_t>(engine));
        get_global_logger();
        std::shared_ptr<spdlog::logger> temp_logger = get_global_logger();
        try {
            // Use non-colored sink for engine logger
             auto sink = std::make_shared<spdlog::sinks::stderr_sink_mt>(); // USE NON-COLORED SINK
             engine->logger = std::make_shared<spdlog::logger>(engine->logger_name, sink); // CREATE LOGGER WITH SINK
            // NOTE: No need to spdlog::register_logger if it's engine-specific
            engine->logger->set_level(spdlog::level::debug);
            engine->logger->flush_on(spdlog::level::trace);
        } catch (const spdlog::spdlog_ex& ex) {
             if(temp_logger) temp_logger->error("Failed to create engine-specific logger '{}': {}", engine->logger_name, ex.what());
             else std::cerr << "Failed to create engine-specific logger '" << engine->logger_name << "': " << ex.what() << std::endl;
             engine->logger = temp_logger;
        }
        // Create unique_ptr for discovery, then shared_ptr for controller
        auto discovery = std::make_unique<FWA::IOKitFireWireDeviceDiscovery>(nullptr);
        auto controller = std::make_shared<FWA::DeviceController>(std::move(discovery));
        engine->controller = controller;
        engine->discovery_ptr = dynamic_cast<FWA::IOKitFireWireDeviceDiscovery*>(controller->getDiscoveryRaw());
        if(engine->discovery_ptr) {
             engine->discovery_ptr->setDeviceController(controller);
        } else {
             engine->logger->error("Discovery mechanism pointer is null after creation.");
             delete engine;
             return nullptr;
        }
        engine->logger->info("FWA Engine created successfully.");
        return engine;
    } catch (const std::bad_alloc&) {
        auto logger = get_global_logger();
        if(logger) logger->critical("FWA Engine creation failed: No memory");
        else std::cerr << "FWA Engine creation failed: No memory" << std::endl;
        delete engine;
        return nullptr;
    } catch (const std::exception& e) {
        auto logger = get_global_logger();
        if(logger) logger->critical("FWA Engine creation failed: {}", e.what());
        else std::cerr << "FWA Engine creation failed: " << e.what() << std::endl;
        delete engine;
        return nullptr;
    } catch (...) {
        auto logger = get_global_logger();
        if(logger) logger->critical("FWA Engine creation failed: Unknown exception");
        else std::cerr << "FWA Engine creation failed: Unknown exception" << std::endl;
        delete engine;
        return nullptr;
    }
}

void FWAEngine_Destroy(FWAEngineRef engine) {
    if (!engine) {
        return;
    }
    auto logger = engine->logger ? engine->logger : get_global_logger();
    if (engine->controller) {
         if(logger) logger->info("Stopping FWA Engine before destruction.");
        try {
            (void)engine->controller->stop();
        } catch(...) {
            if(logger) logger->warn("Exception caught during FWA Engine stop on destroy.");
        }
    }
    if(logger) logger->info("Destroying FWA Engine.");
    spdlog::drop(engine->logger_name);
    delete engine;
}

IOReturn FWAEngine_SetLogCallback(FWAEngineRef engine,
                                  FWALogCallback callback,
                                  void* user_data)
{
    if (!engine) return kIOReturnBadArgument;

    engine->log_callback = callback;
    engine->log_user_data = user_data;

    auto cb_sink = std::make_shared<FWACCallbackSink<std::mutex>>(engine);
    cb_sink->set_level(spdlog::level::trace);

    // (optional) keep local stderr sink so debug prints still show in Xcode
    auto stderr_sink = std::make_shared<spdlog::sinks::stderr_sink_mt>();

    engine->logger->sinks().clear();
    engine->logger->sinks().push_back(cb_sink);
    engine->logger->sinks().push_back(stderr_sink);

    // 👇 **THIS LINE MAKES EVERY spdlog::<level>() GO THROUGH YOUR SINK**
    spdlog::set_default_logger(engine->logger);

    engine->logger->info("Registered C log callback.");
    return kIOReturnSuccess;
}

IOReturn FWAEngine_Start(FWAEngineRef engine, FWADeviceNotificationCallback notification_callback, void* user_data) {
    if (!engine || !engine->controller || !notification_callback) {
        return kIOReturnBadArgument;
    }
    engine->notification_callback = notification_callback;
    engine->notification_user_data = user_data;
    auto cpp_callback = [engine](std::shared_ptr<FWA::AudioDevice> device_sptr, bool connected) {
        if (engine && engine->notification_callback && device_sptr) {
            FWADeviceRef device_ref = reinterpret_cast<FWADeviceRef>(device_sptr.get());
            // FIX: Pass user_data, then device_ref, then connected
            engine->notification_callback(engine->notification_user_data, device_ref, connected);
        } else if (!device_sptr) {
            if(engine->logger) engine->logger->warn("Device notification received with null device pointer.");
        }
    };
    return safe_execute(engine, [&](auto& controller) {
        engine->logger->info("Starting FWA Engine...");
        return controller->start(cpp_callback);
    });
}

IOReturn FWAEngine_Stop(FWAEngineRef engine) {
    return safe_execute(engine, [&](auto& controller) {
        engine->logger->info("Stopping FWA Engine...");
        return controller->stop();
    });
}

IOReturn FWADevice_GetGUID(FWADeviceRef device_ref, uint64_t* out_guid) {
    if (!device_ref || !out_guid) {
        return kIOReturnBadArgument;
    }
    FWA::AudioDevice* device = reinterpret_cast<FWA::AudioDevice*>(device_ref);
    try {
        *out_guid = device->getGuid();
        return kIOReturnSuccess;
    } catch (...) {
        return kIOReturnInternalError;
    }
}

char* FWADevice_GetDeviceName(FWADeviceRef device_ref) {
    if (!device_ref) {
        return nullptr;
    }
    FWA::AudioDevice* device = reinterpret_cast<FWA::AudioDevice*>(device_ref);
    try {
        const std::string& name = device->getDeviceName();
        char* c_name = strdup(name.c_str());
        if (!c_name) {
             auto logger = spdlog::get("fwa_global");
             if(logger) logger->error("Failed to allocate memory for device name string.");
        }
        return c_name;
    } catch (...) {
        return nullptr;
    }
}

char* FWADevice_GetVendorName(FWADeviceRef device_ref) {
     if (!device_ref) {
        return nullptr;
    }
    FWA::AudioDevice* device = reinterpret_cast<FWA::AudioDevice*>(device_ref);
    try {
        const std::string& name = device->getVendorName();
        char* c_name = strdup(name.c_str());
        if (!c_name) {
             auto logger = spdlog::get("fwa_global");
             if(logger) logger->error("Failed to allocate memory for vendor name string.");
        }
        return c_name;
    } catch (...) {
        return nullptr;
    }
}

char* FWADevice_GetInfoJSON(FWADeviceRef device_ref) {
    if (!device_ref) {
        return nullptr;
    }
    FWA::AudioDevice* device = reinterpret_cast<FWA::AudioDevice*>(device_ref);
    auto logger = spdlog::get("fwa_global");
    try {
        logger->debug("Refreshing device info for GUID 0x{:x} before generating JSON...", device->getGuid());
        auto parser = std::make_shared<FWA::DeviceParser>(device);
        auto parseResult = parser->parse();
        if (!parseResult) {
             if(logger) logger->warn("Failed to parse/refresh device info for GUID 0x{:x} before JSON generation: IOReturn 0x{:x}. JSON might be stale or incomplete.",
                  device->getGuid(), static_cast<IOReturn>(parseResult.error()));
        }
        const FWA::DeviceInfo& info = device->getDeviceInfo();
        nlohmann::json j = info.toJson(*device);
        std::string json_str = j.dump(2);
        char* c_json = strdup(json_str.c_str());
         if (!c_json) {
             if(logger) logger->error("Failed to allocate memory for JSON info string for device 0x{:x}.", device->getGuid());
         }
        return c_json;
    } catch (const std::exception& e) {
         if(logger) logger->error("Error generating JSON for device 0x{:x}: {}", device->getGuid(), e.what());
        return nullptr;
    } catch (...) {
         if(logger) logger->error("Unknown error generating JSON for device 0x{:x}", device->getGuid());
        return nullptr;
    }
}

void FWADevice_FreeString(char* str) {
    if (str) {
        free(str);
    }
}

IOReturn FWADevice_SendCommand(FWADeviceRef device_ref,
                               const uint8_t* cmd_data,
                               size_t cmd_len,
                               uint8_t** out_resp_data,
                               size_t* out_resp_len)
{
    if (!device_ref || !cmd_data || cmd_len == 0 || !out_resp_data || !out_resp_len) {
        return kIOReturnBadArgument;
    }
    *out_resp_data = nullptr;
    *out_resp_len = 0;
    FWA::AudioDevice* device = reinterpret_cast<FWA::AudioDevice*>(device_ref);
    auto logger = spdlog::get("fwa_global");
    try {
        auto commandInterface = device->getCommandInterface();
        if (!commandInterface) {
             if(logger) logger->warn("SendCommand failed for device 0x{:x}: CommandInterface is null (device might be terminating).", device->getGuid());
            return kIOReturnNotAttached;
        }
        auto activation_result_expected = commandInterface->activate();
        // Check if the expected object contains a value (success) or an error
        IOReturn activationResult = kIOReturnInternalError; // Default to error
        if (activation_result_expected.has_value()) {
            activationResult = kIOReturnSuccess;
        } else {
            activationResult = static_cast<IOReturn>(activation_result_expected.error());
        }
        // The original check for StillOpen was also correct, let's keep allowing it
        if (activationResult != kIOReturnSuccess && activationResult != kIOReturnStillOpen) {
             if(logger) logger->error("SendCommand failed for device 0x{:x}: Failed to activate CommandInterface: 0x{:x}", device->getGuid(), activationResult);
             return activationResult;
        }
        std::vector<uint8_t> commandVec(cmd_data, cmd_data + cmd_len);
        std::expected<std::vector<uint8_t>, FWA::IOKitError> result =
            commandInterface->sendCommand(commandVec);
        if (!result) {
            IOReturn io_error = static_cast<IOReturn>(result.error());
             if(logger) logger->warn("SendCommand failed for device 0x{:x}: Underlying send error: 0x{:x}", device->getGuid(), io_error);
            return io_error;
        }
        const std::vector<uint8_t>& responseVec = result.value();
        if (!responseVec.empty()) {
            *out_resp_len = responseVec.size();
            *out_resp_data = static_cast<uint8_t*>(malloc(*out_resp_len));
            if (!*out_resp_data) {
                 if(logger) logger->error("SendCommand failed for device 0x{:x}: Failed to allocate memory for response buffer ({} bytes).", device->getGuid(), *out_resp_len);
                return kIOReturnNoMemory;
            }
            memcpy(*out_resp_data, responseVec.data(), *out_resp_len);
        } else {
            *out_resp_len = 0;
            *out_resp_data = nullptr;
        }
        return kIOReturnSuccess;
    } catch (const std::bad_alloc&) {
        if(logger) logger->error("SendCommand failed for device 0x{:x}: std::bad_alloc", device->getGuid());
        return kIOReturnNoMemory;
    } catch (const std::exception& e) {
        if(logger) logger->critical("SendCommand failed for device 0x{:x}: C++ Exception: {}", device->getGuid(), e.what());
        return kIOReturnInternalError;
    } catch (...) {
        if(logger) logger->critical("SendCommand failed for device 0x{:x}: Unknown C++ Exception", device->getGuid());
        return kIOReturnInternalError;
    }
}

void FWADevice_FreeResponseBuffer(uint8_t* resp_data) {
    if (resp_data) {
        free(resp_data);
    }
}

// --- ENGINE-BASED DEVICE INTERACTION FUNCTIONS ---
char* FWAEngine_GetInfoJSON(FWAEngineRef engine, uint64_t guid) {
    if (!engine || !engine->controller) {
        return nullptr;
    }
    auto logger = engine->logger ? engine->logger : get_global_logger();
    try {
        auto device_expected = engine->controller->getDeviceByGuid(guid);
        if (!device_expected) {
            if(logger) logger->warn("FWAEngine_GetInfoJSON: Device with GUID 0x{:x} not found.", guid);
            return nullptr;
        }
        std::shared_ptr<FWA::AudioDevice> device_sptr = device_expected.value();
        FWA::AudioDevice* device = device_sptr.get();
        logger->trace("FWAEngine_GetInfoJSON: Refreshing info for GUID 0x{:x}", guid);
        auto parser = std::make_shared<FWA::DeviceParser>(device);
        auto parseResult = parser->parse();
        if (!parseResult) {
            if(logger) logger->warn("FWAEngine_GetInfoJSON: Failed to parse/refresh device info for GUID 0x{:x}: IOReturn 0x{:x}. JSON might be stale.",
                guid, static_cast<IOReturn>(parseResult.error()));
        }
        const FWA::DeviceInfo& info = device->getDeviceInfo();
        nlohmann::json j = info.toJson(*device);
        std::string json_str = j.dump(2);
        char* c_json = strdup(json_str.c_str());
        if (!c_json && logger) {
            logger->error("FWAEngine_GetInfoJSON: Failed to allocate memory for JSON string for GUID 0x{:x}.", guid);
        }
        return c_json;
    } catch (const std::exception& e) {
        if(logger) logger->error("FWAEngine_GetInfoJSON: Error generating JSON for GUID 0x{:x}: {}", guid, e.what());
        return nullptr;
    } catch (...) {
        if(logger) logger->error("FWAEngine_GetInfoJSON: Unknown error generating JSON for GUID 0x{:x}", guid);
        return nullptr;
    }
}

IOReturn FWAEngine_SendCommand(FWAEngineRef engine,
                               uint64_t guid,
                               const uint8_t* cmd_data,
                               size_t cmd_len,
                               uint8_t** out_resp_data,
                               size_t* out_resp_len)
{
    if (!engine || !engine->controller || !cmd_data || cmd_len == 0 || !out_resp_data || !out_resp_len) {
        return kIOReturnBadArgument;
    }
    *out_resp_data = nullptr;
    *out_resp_len = 0;
    auto logger = engine->logger ? engine->logger : get_global_logger();
    try {
        auto device_expected = engine->controller->getDeviceByGuid(guid);
        if (!device_expected) {
            if(logger) logger->warn("FWAEngine_SendCommand: Device with GUID 0x{:x} not found.", guid);
            return kIOReturnNotFound;
        }
        std::shared_ptr<FWA::AudioDevice> device_sptr = device_expected.value();
        auto commandInterface = device_sptr->getCommandInterface();
        if (!commandInterface) {
            if(logger) logger->warn("FWAEngine_SendCommand: CommandInterface is null for GUID 0x{:x} (device terminating?).", guid);
            return kIOReturnNotAttached;
        }
        auto activation_result_expected = commandInterface->activate();
        IOReturn activationResult = activation_result_expected.has_value() ? kIOReturnSuccess : static_cast<IOReturn>(activation_result_expected.error());
        if (activationResult != kIOReturnSuccess && activationResult != kIOReturnStillOpen) {
            if(logger) logger->error("FWAEngine_SendCommand: Failed to activate CommandInterface for GUID 0x{:x}: 0x{:x}", guid, activationResult);
            return activationResult;
        }
        std::vector<uint8_t> commandVec(cmd_data, cmd_data + cmd_len);
        auto result = commandInterface->sendCommand(commandVec);
        if (!result) {
            IOReturn io_error = static_cast<IOReturn>(result.error());
            if(logger) logger->warn("FWAEngine_SendCommand: Underlying send error for GUID 0x{:x}: 0x{:x}", guid, io_error);
            return io_error;
        }
        const std::vector<uint8_t>& responseVec = result.value();
        if (!responseVec.empty()) {
            *out_resp_len = responseVec.size();
            *out_resp_data = static_cast<uint8_t*>(malloc(*out_resp_len));
            if (!*out_resp_data) {
                if(logger) logger->error("FWAEngine_SendCommand: Failed to allocate memory for response buffer ({} bytes) for GUID 0x{:x}.", *out_resp_len, guid);
                return kIOReturnNoMemory;
            }
            memcpy(*out_resp_data, responseVec.data(), *out_resp_len);
        } else {
            *out_resp_len = 0;
            *out_resp_data = nullptr;
        }
        return kIOReturnSuccess;
    } catch (const std::bad_alloc&) {
        if(logger) logger->error("FWAEngine_SendCommand: std::bad_alloc for GUID 0x{:x}", guid);
        return kIOReturnNoMemory;
    } catch (const std::exception& e) {
        if(logger) logger->critical("FWAEngine_SendCommand: C++ Exception for GUID 0x{:x}: {}", guid, e.what());
        return kIOReturnInternalError;
    } catch (...) {
        if(logger) logger->critical("FWAEngine_SendCommand: Unknown C++ Exception for GUID 0x{:x}", guid);
        return kIOReturnInternalError;
    }
}

static spdlog::level::level_enum toSpd(FWALogLevel lvl) {
    switch (lvl) {
        case FWA_LOG_LEVEL_TRACE:    return spdlog::level::trace;
        case FWA_LOG_LEVEL_DEBUG:    return spdlog::level::debug;
        case FWA_LOG_LEVEL_INFO:     return spdlog::level::info;
        case FWA_LOG_LEVEL_WARN:     return spdlog::level::warn;
        case FWA_LOG_LEVEL_ERROR:    return spdlog::level::err;
        case FWA_LOG_LEVEL_CRITICAL: return spdlog::level::critical;
        case FWA_LOG_LEVEL_OFF:      return spdlog::level::off;
    }
    return spdlog::level::info; // fallback
}

IOReturn FWAEngine_SetLogLevel(FWAEngineRef engine, FWALogLevel lvl)
{
    if (!engine || !engine->logger) return kIOReturnBadArgument;
    auto spdLvl = toSpd(lvl);
    engine->logger->set_level(spdLvl);
    spdlog::set_level(spdLvl);          // affects global logger & helpers
    return kIOReturnSuccess;
}