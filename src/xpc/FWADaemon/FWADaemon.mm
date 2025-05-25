
#import "FWADaemon.h" // Import the interface declaration
#import "shared/xpc/FWAClientNotificationProtocol.h" // Correct client protocol
#import <Foundation/Foundation.h>
#import <os/log.h> // Use os_log

// --- C++ Includes ---
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE // Ensure all spdlog macros are compiled
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_sinks.h> // For stderr_sink_mt
#include "core/DaemonCore.hpp"          // Path to your DaemonCore.hpp
#include "OsLogSink.hpp"                // Your custom sink for C++ logs to os_log
#include "GuiCallbackSink.hpp"          // Your custom sink for C++ logs to GUI clients
#include "shared/xpc/FWAXPCCommonTypes.h"  // For FWAXPCLoglevel

// Error domain constants
NSString * const FWADaemonErrorDomain = @"FWADaemonErrorDomain";
NSString * const FWADaemonCoreErrorDomain = @"FWADaemonCoreErrorDomain";
NSString * const FWADaemonCppErrorDomain = @"FWADaemonCppErrorDomain";

// Helper to convert DaemonCoreError to NSError
static NSError* errorFromDaemonCoreError(FWA::DaemonCoreError daemonError, NSString* descriptionPrefix) {
    NSString *desc = [NSString stringWithFormat:@"%@: DaemonCore error code %d",
                                     descriptionPrefix ?: @"Operation failed",
                                     static_cast<int>(daemonError)];
    return [NSError errorWithDomain:@"FWADaemonCoreErrorDomain"
                               code:static_cast<NSInteger>(daemonError)
                           userInfo:@{NSLocalizedDescriptionKey: desc}];
}

// Helper to convert IOKitError to NSError
static NSError* errorFromIOKitError(FWA::IOKitError fwaError, NSString* descriptionPrefix) {
    NSString *desc = [NSString stringWithFormat:@"%@: C++ error code %d",
                                     descriptionPrefix ?: @"Operation failed",
                                     static_cast<int>(fwaError)];
    return [NSError errorWithDomain:@"FWADaemonCppErrorDomain"
                               code:static_cast<NSInteger>(fwaError)
                           userInfo:@{NSLocalizedDescriptionKey: desc}];
}

// Helper to convert spdlog level to FWAXPCLoglevel
static FWAXPCLoglevel spdlogLevelToFWAXPCLoglevel(spdlog::level::level_enum spdlogLevel) {
    switch (spdlogLevel) {
        case spdlog::level::trace:    return FWAXPCLoglevelTrace;
        case spdlog::level::debug:    return FWAXPCLoglevelDebug;
        case spdlog::level::info:     return FWAXPCLoglevelInfo;
        case spdlog::level::warn:     return FWAXPCLoglevelWarn;
        case spdlog::level::err:      return FWAXPCLoglevelError;
        case spdlog::level::critical: return FWAXPCLoglevelCritical;
        case spdlog::level::off:      return FWAXPCLoglevelOff;
        default:                      return FWAXPCLoglevelInfo; // Default
    }
}

// Helper to convert FWAXPCLoglevel to spdlog level
static spdlog::level::level_enum fwaXPCLoglevelToSpdlogLevel(FWAXPCLoglevel xpcLevel) {
    switch (xpcLevel) {
        case FWAXPCLoglevelTrace:    return spdlog::level::trace;
        case FWAXPCLoglevelDebug:    return spdlog::level::debug;
        case FWAXPCLoglevelInfo:     return spdlog::level::info;
        case FWAXPCLoglevelWarn:     return spdlog::level::warn;
        case FWAXPCLoglevelError:    return spdlog::level::err;
        case FWAXPCLoglevelCritical: return spdlog::level::critical;
        case FWAXPCLoglevelOff:      return spdlog::level::off;
        default:                     return spdlog::level::info; // Default
    }
}

// --- Global/Static for C++ Core and Daemon's own logger ---
static std::shared_ptr<spdlog::logger> s_FWADaemon_ObjC_logger; // Logger for FWADaemon.mm

static const void * const kInternalQueueKey = &kInternalQueueKey;
static void * const kInternalQueueContext = (void *)&kInternalQueueContext;

// Simple class to hold client info (Keep this definition)
@interface ClientInfo : NSObject
@property (nonatomic, copy) NSString *clientID;
@property (nonatomic, strong) NSXPCConnection *connection;
@property (nonatomic, strong) id<FWAClientNotificationProtocol> remoteProxy; // Use correct protocol
@end
@implementation ClientInfo
@end

// Class extension for private properties
@interface FWADaemon ()
@property (nonatomic, strong) NSMutableDictionary<NSString *, ClientInfo *> *connectedClients;
@property (nonatomic, assign) BOOL driverIsConnected;
@property (nonatomic, strong) dispatch_queue_t internalQueue;
// Note: sharedMemoryName property removed - DaemonCore handles the name
@end

@implementation FWADaemon

+ (instancetype)sharedService {
    static FWADaemon *sharedInstance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        // Initialize shared logger for FWADaemon.mm messages first
        if (!s_FWADaemon_ObjC_logger) {
            try {
                // This logger is for messages specifically from FWADaemon.mm itself.
                // DaemonCore will get its own logger instance passed to it.
                auto os_sink_for_daemon_mm = std::make_shared<os_log_sink_mt>(OS_LOG_DEFAULT);
                s_FWADaemon_ObjC_logger = std::make_shared<spdlog::logger>("FWADaemonObjC", os_sink_for_daemon_mm);
                s_FWADaemon_ObjC_logger->set_level(spdlog::level::trace);
                spdlog::register_logger(s_FWADaemon_ObjC_logger); // Optional: register if accessed via spdlog::get
                s_FWADaemon_ObjC_logger->info("FWADaemon.mm specific logger initialized.");
            } catch (const std::exception& e) {
                os_log_error(OS_LOG_DEFAULT, "FWADaemon.mm specific spdlog init failed: %s", e.what());
                // Fallback to os_log directly if spdlog fails for this specific logger
            }
        }
        sharedInstance = [[FWADaemon alloc] initPrivate]; // Use a private initializer
    });
    return sharedInstance;
}

// Private initializer to set up C++ core
- (instancetype)initPrivate {
    self = [super init];
    if (self) {
        if (s_FWADaemon_ObjC_logger) {
            s_FWADaemon_ObjC_logger->info("FWADaemon initPrivate starting...");
        }
        _internalQueue = dispatch_queue_create("net.mrmidi.FWADaemon.internalQueue", DISPATCH_QUEUE_SERIAL);
        dispatch_queue_set_specific(_internalQueue, kInternalQueueKey, kInternalQueueContext, NULL);
        _connectedClients = [NSMutableDictionary dictionary];
        _driverIsConnected = NO;

        // --- spdlog setup for C++ libraries (FWA, Isoch) if not done by DaemonCore ---
        // This global spdlog setup should ideally happen once.
        // If DaemonCore is the primary user of spdlog for FWA/Isoch, it might manage its own
        // logger instance that uses these sinks, or we set the default logger here.
        static dispatch_once_t s_spdlog_setup_token;
        dispatch_once(&s_spdlog_setup_token, ^{
            try {
                if (s_FWADaemon_ObjC_logger) {
                    s_FWADaemon_ObjC_logger->info("Performing global spdlog setup (sinks, default logger)...");
                }
                auto os_sink = std::make_shared<os_log_sink_mt>(OS_LOG_DEFAULT); // For C++ libs to log to os_log
                auto gui_sink = std::make_shared<gui_callback_sink_mt>(self); // For C++ libs to log to GUI
                #ifdef DEBUG
                auto stderr_sink = std::make_shared<spdlog::sinks::stderr_sink_mt>();
                std::vector<spdlog::sink_ptr> sinks { os_sink, gui_sink, stderr_sink };
                #else
                std::vector<spdlog::sink_ptr> sinks { os_sink, gui_sink };
                #endif

                os_sink->set_pattern("[%^%l%$] %v");
                gui_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
                #ifdef DEBUG
                stderr_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
                #endif

                auto defaultCppLogger = std::make_shared<spdlog::logger>("FWA_CPP_Libs", sinks.begin(), sinks.end());
                defaultCppLogger->set_level(spdlog::level::trace);
                defaultCppLogger->flush_on(spdlog::level::warn);
                spdlog::set_default_logger(defaultCppLogger); // C++ libs using spdlog::info will use this
                spdlog::register_logger(defaultCppLogger);
                if (s_FWADaemon_ObjC_logger) {
                    s_FWADaemon_ObjC_logger->info("Global spdlog default logger for C++ libs configured.");
                }
            } catch (const std::exception& e) {
                if (s_FWADaemon_ObjC_logger) {
                    s_FWADaemon_ObjC_logger->critical("Global spdlog setup failed: {}", e.what());
                }
            }
        });
        // --- End spdlog setup ---

        // --- Instantiate C++ DaemonCore ---
        // Pass the logger that DaemonCore should use (can be the default_logger, or a specific one)
        if (s_FWADaemon_ObjC_logger) {
            s_FWADaemon_ObjC_logger->info("Creating FWA::DaemonCore instance...");
        }
        // --- Define the callback lambdas FIRST ---
        FWADaemon *strongSelf = self;
        auto deviceNotificationCallbackToXPC = [strongSelf](uint64_t guid, const std::string& name, const std::string& vendor, bool added) {
            if (!strongSelf) {
                SPDLOG_WARN("FWADaemon instance (self) is nil in C++ to XPC callback. Skipping notification.");
                return;
            }
            // Forward to Objective-C method or notification system as needed
            // Example: [strongSelf deviceNotificationReceived:guid name:name vendor:vendor added:added];
            // (Implement this method if needed)
        };

        auto logCallbackToXPC = [strongSelf](const std::string& message, int level, const std::string& source) {
            if (!strongSelf) {
                SPDLOG_WARN("FWADaemon instance (self) is nil in C++ log callback. Skipping log forwarding.");
                return;
            }
            // Forward to Objective-C method that handles XPC broadcast
            NSString *nsMessage = [NSString stringWithUTF8String:message.c_str()];
            NSString *nsSource = [NSString stringWithUTF8String:source.c_str()];
            // Example: [strongSelf forwardLogMessageToClients:nsSource level:level message:nsMessage];
            // (Implement this method if needed)
        };

        // --- Instantiate C++ DaemonCore, PASSING the callbacks ---
        _cppCore = std::make_unique<FWA::DaemonCore>(
            spdlog::default_logger(),
            deviceNotificationCallbackToXPC,
            logCallbackToXPC
        );

        if (s_FWADaemon_ObjC_logger) {
            s_FWADaemon_ObjC_logger->info("FWA::DaemonCore instance created with callbacks passed to constructor.");
        }
        // Set any remaining callbacks that are still managed by setters:
        // _cppCore->setStreamStatusCallback(...);
        // _cppCore->setDriverPresenceCallback(...);
        // DaemonCore's constructor now handles SHM initialization.
        if (s_FWADaemon_ObjC_logger) {
            s_FWADaemon_ObjC_logger->info("FWADaemon initPrivate finished.");
        }
    }
    return self;
}

- (void)dealloc {
    if (s_FWADaemon_ObjC_logger) {
        s_FWADaemon_ObjC_logger->info("FWADaemon deallocating...");
    }
    if (_cppCore) {
        // _cppCore->cleanupSharedMemory(); // Destructor of DaemonCore should handle this
        _cppCore.reset(); // This will call ~DaemonCore()
        if (s_FWADaemon_ObjC_logger) {
            s_FWADaemon_ObjC_logger->info("C++ DaemonCore instance reset.");
        }
    }
    // Other cleanup like invalidating internalQueue if it's not done automatically
}



// Remove obsolete clients getter if mutableClients is removed
// - (NSArray<NSXPCConnection *> *)clients {
//     return [self.mutableClients copy];
// }

// Helper for safe queue access
- (void)performOnInternalQueueSync:(dispatch_block_t)block {
    // Ensure queue exists before using it
    if (!self.internalQueue) {
         os_log_error(OS_LOG_DEFAULT, "[FWADaemon] Internal queue is nil in performOnInternalQueueSync!");
         return; // Or handle error differently
    }

    // +++ ADDED CHECK +++
    if (dispatch_get_specific(kInternalQueueKey) == kInternalQueueContext) {
        // Already running on the internal queue, execute block directly
        block();
    } else {
        // Not running on internal queue, dispatch synchronously
        dispatch_sync(self.internalQueue, block);
    }
    // +++++++++++++++++++++
}
- (void)performOnInternalQueueAsync:(dispatch_block_t)block {
     if (self.internalQueue) {
        dispatch_async(self.internalQueue, block);
     } else {
         os_log_error(OS_LOG_DEFAULT, "[FWADaemon] Internal queue is nil in performOnInternalQueueAsync!");
     }
}


#pragma mark - FWADaemonControlProtocol Implementation

// --- Registration & Lifecycle ---

// DEPRECATED: Use registerClientAndStartEngine instead
- (void)registerClient:(NSString *)clientID
clientNotificationEndpoint:(NSXPCListenerEndpoint *)clientNotificationEndpoint
           withReply:(void (^)(BOOL success, NSDictionary * _Nullable daemonInfo))reply
{
    SPDLOG_WARN("DEPRECATED: registerClient called from '{}' - use registerClientAndStartEngine instead", [clientID UTF8String] ?: "<nil_client_id>");
    
    if (!clientID || [clientID length] == 0 || !clientNotificationEndpoint) {
        SPDLOG_ERROR("Registration failed - invalid clientID or endpoint for '{}'.", [clientID UTF8String] ?: "<nil_client_id>");
        if (reply) reply(NO, nil);
        return;
    }

    // Use the endpoint to establish the connection back TO the client
    NSXPCConnection *clientConnection = [[NSXPCConnection alloc] initWithListenerEndpoint:clientNotificationEndpoint];
    clientConnection.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(FWAClientNotificationProtocol)]; // Use correct protocol

    ClientInfo *newClientInfo = [[ClientInfo alloc] init];
    newClientInfo.clientID = clientID;
    newClientInfo.connection = clientConnection; // This connection is for calling the CLIENT


    // Notify GUI that driver is connected
    if (self.driverIsConnected) {
        id<FWAClientNotificationProtocol> guiProxy = newClientInfo.remoteProxy;
        @try {
            [guiProxy driverConnectionStatusDidChange:self.driverIsConnected];
            SPDLOG_INFO("Notified GUI '{}' of driverConnected=YES on registration.", [clientID UTF8String]);
        } @catch (NSException *ex) {
            os_log_error(OS_LOG_DEFAULT,
                "[FWADaemon] Failed to notify GUI '%{public}@' on register: %{public}@",
                clientID, ex);
        }
    }

    // Error handler for the connection TO the client
    newClientInfo.remoteProxy = [clientConnection remoteObjectProxyWithErrorHandler:^(NSError * _Nonnull error) {
        const char* clientID_cstr = [clientID UTF8String];
        const char* error_cstr = [[error description] UTF8String];
        SPDLOG_ERROR("XPC error calling remote proxy for client '{}': {}",
                     clientID_cstr ? clientID_cstr : "<nil_id>",
                     error_cstr ? error_cstr : "<nil_error>");
        [self performOnInternalQueueAsync:^{
             ClientInfo *info = self.connectedClients[clientID];
             if (info && info.connection == clientConnection) {
                 SPDLOG_INFO("Removing client '{}' due to remote proxy error.", clientID_cstr ? clientID_cstr : "<nil_id>");
                 [self.connectedClients removeObjectForKey:clientID];
             }
         }];
    }];

    // Invalidation handler for the connection TO the client
    clientConnection.invalidationHandler = ^{
        const char* clientID_cstr = [clientID UTF8String];
        SPDLOG_INFO("Client connection invalidated for '{}'.", clientID_cstr ? clientID_cstr : "<nil_id>");
        [self performOnInternalQueueAsync:^{
            ClientInfo *info = self.connectedClients[clientID];
            if (info && info.connection == clientConnection) {
                SPDLOG_INFO("Removing client '{}' from registry.", clientID_cstr ? clientID_cstr : "<nil_id>");
                [self.connectedClients removeObjectForKey:clientID];
            } else {
                SPDLOG_INFO("Invalidation handler: Client '{}' not found or connection mismatch.", clientID_cstr ? clientID_cstr : "<nil_id>");
            }
        }];
    };

    [clientConnection resume];

    // Store the new client info thread-safely
    [self performOnInternalQueueSync:^{
        ClientInfo* existingClient = self.connectedClients[clientID];
        if (existingClient) {
            SPDLOG_WARN("Client ID '{}' already registered. Invalidating old connection.", [clientID UTF8String]);
            [existingClient.connection invalidate];
        }
        self.connectedClients[clientID] = newClientInfo;
        SPDLOG_INFO("Client '{}' successfully registered.", [clientID UTF8String]);
    }];
    // Immediately notify GUI of current driverConnected state if already connected
    if (self.driverIsConnected) {
        id<FWAClientNotificationProtocol> guiProxy = newClientInfo.remoteProxy;
        @try {
            [guiProxy driverConnectionStatusDidChange:self.driverIsConnected];
            SPDLOG_INFO("Notified GUI '{}' of driverConnected=YES on registration.", [clientID UTF8String]);
        } @catch (NSException *ex) {
            os_log_error(OS_LOG_DEFAULT,
                         "[FWADaemon] Failed to notify GUI '%{public}s' on register: %{public}@",
                         [clientID UTF8String], ex);
        }
    }

    // Perform a quick handshake: ping the GUI and log the reply
    id<FWAClientNotificationProtocol> guiProxy = [clientConnection remoteObjectProxy];
    [guiProxy daemonHandshake:^(BOOL ok) {
            if (ok) {
                SPDLOG_INFO("Handshake with GUI client '{}' succeeded.", [clientID UTF8String]);
            } else {
                SPDLOG_ERROR("Handshake with GUI client '{}' failed.", [clientID UTF8String]);
            }
    }];

    // Prepare reply info
    NSDictionary *daemonInfo = @{ @"daemonVersion": @"0.1.0-stub" };
    if (reply) reply(YES, daemonInfo);
}

- (void)unregisterClient:(NSString *)clientID {
     SPDLOG_INFO("Received unregisterClient request from '{}'", [clientID UTF8String]);
     [self performOnInternalQueueSync:^{ // Use sync to ensure removal is processed promptly
         ClientInfo *info = self.connectedClients[clientID];
         if (info) {
             SPDLOG_INFO("Invalidating connection and removing client '{}'", [clientID UTF8String]);
             [info.connection invalidate]; // Explicitly invalidate
             [self.connectedClients removeObjectForKey:clientID];
         } else {
             SPDLOG_INFO("WARNING: unregisterClient called for unknown clientID '{}'", [clientID UTF8String]);
         }
     }];
}

// --- Status & Config ---
- (void)updateDeviceConnectionStatus:(uint64_t)guid
                         isConnected:(BOOL)isConnected
                       isInitialized:(BOOL)isInitialized
                          deviceName:(NSString *)deviceName
                          vendorName:(NSString *)vendorName
{
    SPDLOG_INFO("Received updateDeviceConnectionStatus for GUID 0x%llx: connected=%d, initialized=%d, name='%s', vendor='%s'",
        guid, isConnected, isInitialized,
        [deviceName UTF8String] ?: "<nil>", [vendorName UTF8String] ?: "<nil>");
    // TODO: Cache state and broadcast to Driver client
}

- (void)updateDeviceConfiguration:(uint64_t)guid configInfo:(NSDictionary *)configInfo {
    SPDLOG_INFO("Received updateDeviceConfiguration for GUID 0x%llx: %s",
        guid, [[configInfo description] UTF8String] ?: "<nil>");
     // TODO: Cache state and broadcast to Driver client
}

- (void)getDeviceConnectionStatus:(uint64_t)guid withReply:(void (^)(NSDictionary * _Nullable statusInfo))reply {
    SPDLOG_INFO("Received getDeviceConnectionStatus request for GUID 0x%llx", guid);
    // TODO: Read from cache
    SPDLOG_INFO("STUB: Replying nil to getDeviceConnectionStatus for GUID 0x%llx.", guid);
    if (reply) reply(nil);
}

- (void)getDeviceConfiguration:(uint64_t)guid withReply:(void (^)(NSDictionary * _Nullable configInfo))reply {
    SPDLOG_INFO("Received getDeviceConfiguration request for GUID 0x%llx", guid);
    // TODO: Read from cache
    SPDLOG_INFO("STUB: Replying nil to getDeviceConfiguration for GUID 0x%llx.", guid);
    if (reply) reply(nil);
}

- (void)getConnectedDeviceGUIDsWithReply:(void (^)(NSArray<NSNumber *> * _Nullable guids))reply {
    SPDLOG_INFO("Received getConnectedDeviceGUIDs request");
     // TODO: Read GUIDs from cached status
    SPDLOG_INFO("STUB: Replying empty array to getConnectedDeviceGUIDs.");
     if (reply) reply(@[]);
}

// --- Driver Presence ---
- (void)setDriverPresenceStatus:(BOOL)isPresent {
    SPDLOG_INFO("Received setDriverPresenceStatus: {}", isPresent);
    [self performOnInternalQueueAsync:^{
        if (self.driverIsConnected != isPresent) {
            SPDLOG_INFO("Driver connection status changed to {}. Broadcasting...", isPresent);
            self.driverIsConnected = isPresent;
            [self broadcastDriverConnectionStatus:isPresent];
        } else {
            os_log_debug(OS_LOG_DEFAULT, "[FWADaemon] Driver connection status already %{public}d, no change.", isPresent);
        }
    }];
}

- (void)getIsDriverConnectedWithReply:(void (^)(BOOL isConnected))reply {
    os_log_debug(OS_LOG_DEFAULT, "[FWADaemon] Received getIsDriverConnected request.");
    if (!reply) return;
    [self performOnInternalQueueSync:^{
        BOOL currentStatus = self.driverIsConnected;
        os_log_debug(OS_LOG_DEFAULT, "[FWADaemon] Replying to getIsDriverConnected with status: %{public}d", currentStatus);
        reply(currentStatus);
    }];
}

- (void)broadcastDriverConnectionStatus:(BOOL)isConnected {
    SPDLOG_INFO("Broadcasting driverConnectionStatusDidChange: {}", isConnected);
    for (ClientInfo *info in self.connectedClients.allValues) {
        if ([info.clientID hasPrefix:@"GUI"]) {
            id<FWAClientNotificationProtocol> guiProxy = info.remoteProxy;
            if (guiProxy) {
                @try {
                    [guiProxy driverConnectionStatusDidChange:isConnected];
                } @catch (NSException *exception) {
                    os_log_error(OS_LOG_DEFAULT, "[FWADaemon] Exception broadcasting driver status to GUI '%{public}@': %{public}@", info.clientID, exception);
                }
            }
        }
    }
}

// --- Helper to find GUI proxy ---
- (id<FWAClientNotificationProtocol>)getGUIProxyForGUID:(uint64_t)guid {
    // Note: In GUI-centric model, there might only be one GUI, or maybe one per device?
    // This finds the *first* client whose ID starts with "GUI".
    __block id<FWAClientNotificationProtocol> guiProxy = nil;
    [self performOnInternalQueueSync:^{ // Sync needed to return value reliably
        for (ClientInfo *info in self.connectedClients.allValues) {
             // TODO: Need a better way to associate GUIDs with clients if multiple devices/GUI
             if ([info.clientID hasPrefix:@"GUI"]) {
                guiProxy = info.remoteProxy;
                break;
            }
        }
    }];
    return guiProxy;
}


// --- Control Commands (Driver -> Daemon -> GUI) ---
- (void)requestSetNominalSampleRate:(uint64_t)guid rate:(double)rate withReply:(void (^)(BOOL success))reply {
    SPDLOG_INFO("--> Forwarding SetNominalSampleRate(GUID: 0x{:x}, Rate: {:.1f} Hz) to GUI", guid, rate);
    id<FWAClientNotificationProtocol> guiProxy = [self getGUIProxyForGUID:guid];
    if (guiProxy) {
        [guiProxy performSetNominalSampleRate:guid rate:rate withReply:^(BOOL guiSuccess) {
            SPDLOG_INFO("<-- Relaying reply ({}) for SetNominalSampleRate(GUID: 0x{:x})", guiSuccess ? "Success" : "Fail", guid);
            if (reply) reply(guiSuccess);
        }];
    } else {
        SPDLOG_ERROR("No GUI client found to forward SetNominalSampleRate(GUID: 0x{:x}). Replying NO.", guid);
        if (reply) reply(NO);
    }
}

- (void)requestSetClockSource:(uint64_t)guid clockSourceID:(uint32_t)clockSourceID withReply:(void (^)(BOOL success))reply {
    SPDLOG_INFO("Forwarding requestSetClockSource for GUID 0x%llx to ID %u", guid, clockSourceID);
    id<FWAClientNotificationProtocol> guiProxy = [self getGUIProxyForGUID:guid];
    if (guiProxy) {
        [guiProxy performSetClockSource:guid clockSourceID:clockSourceID withReply:^(BOOL guiSuccess) {
            SPDLOG_INFO("Relaying reply (%d) for requestSetClockSource GUID 0x%llx", guiSuccess, guid);
            if (reply) reply(guiSuccess);
        }];
    } else {
        os_log_error(OS_LOG_DEFAULT, "[FWADaemon] ERROR: No GUI client for requestSetClockSource GUID 0x%llx.", guid);
        if (reply) reply(NO);
    }
}

- (void)requestSetMasterVolumeScalar:(uint64_t)guid scope:(uint32_t)scope element:(uint32_t)element scalarValue:(float)scalarValue withReply:(void (^)(BOOL success))reply {
    SPDLOG_INFO("Forwarding requestSetMasterVolumeScalar for GUID 0x%llx (Scope %u, Elem %u) to %.3f", guid, scope, element, scalarValue);
     id<FWAClientNotificationProtocol> guiProxy = [self getGUIProxyForGUID:guid];
    if (guiProxy) {
        [guiProxy performSetMasterVolumeScalar:guid scope:scope element:element scalarValue:scalarValue withReply:^(BOOL guiSuccess) {
            SPDLOG_INFO("Relaying reply (%d) for requestSetMasterVolumeScalar GUID 0x%llx", guiSuccess, guid);
            if (reply) reply(guiSuccess);
        }];
    } else {
        os_log_error(OS_LOG_DEFAULT, "[FWADaemon] ERROR: No GUI client for requestSetMasterVolumeScalar GUID 0x%llx.", guid);
        if (reply) reply(NO);
    }
}

- (void)requestSetMasterMute:(uint64_t)guid scope:(uint32_t)scope element:(uint32_t)element muteState:(BOOL)muteState withReply:(void (^)(BOOL success))reply {
    SPDLOG_INFO("Forwarding requestSetMasterMute for GUID 0x%llx (Scope %u, Elem %u) to %d", guid, scope, element, muteState);
     id<FWAClientNotificationProtocol> guiProxy = [self getGUIProxyForGUID:guid];
    if (guiProxy) {
        [guiProxy performSetMasterMute:guid scope:scope element:element muteState:muteState withReply:^(BOOL guiSuccess) {
            SPDLOG_INFO("Relaying reply (%d) for requestSetMasterMute GUID 0x%llx", guiSuccess, guid);
            if (reply) reply(guiSuccess);
        }];
    } else {
        os_log_error(OS_LOG_DEFAULT, "[FWADaemon] ERROR: No GUI client for requestSetMasterMute GUID 0x%llx.", guid);
        if (reply) reply(NO);
    }
}

// --- IO State (Driver -> Daemon -> GUI) ---
- (void)requestStartIO:(uint64_t)guid withReply:(void (^)(BOOL success))reply {
    os_log_info(OS_LOG_DEFAULT, "[FWADaemon] Forwarding requestStartIO for GUID 0x%llx", guid);
    id<FWAClientNotificationProtocol> guiProxy = [self getGUIProxyForGUID:guid];
     if (guiProxy) {
        [guiProxy performStartIO:guid withReply:^(BOOL guiSuccess) {
            os_log_info(OS_LOG_DEFAULT, "[FWADaemon] Relaying reply (%d) for requestStartIO GUID 0x%llx", guiSuccess, guid);
            if (reply) reply(guiSuccess);
        }];
    } else {
        os_log_error(OS_LOG_DEFAULT, "[FWADaemon] ERROR: No GUI client for requestStartIO GUID 0x%llx.", guid);
        if (reply) reply(NO); // Cannot start IO without GUI
    }
}

- (void)requestStopIO:(uint64_t)guid {
    os_log_info(OS_LOG_DEFAULT, "[FWADaemon] Forwarding requestStopIO for GUID 0x%llx", guid);
     id<FWAClientNotificationProtocol> guiProxy = [self getGUIProxyForGUID:guid];
     if (guiProxy) {
        [guiProxy performStopIO:guid]; // Fire and forget
    } else {
         os_log_error(OS_LOG_DEFAULT, "[FWADaemon] ERROR: No GUI client found to forward requestStopIO for GUID 0x%llx.", guid);
    }
}

// --- Logging (Driver -> Daemon -> GUI) ---
- (void)forwardLogMessageFromDriver:(int32_t)level message:(NSString *)message {
     // os_log_debug(OS_LOG_DEFAULT, "[FWADaemon] Received log from Driver (Level %d): %{public}@", level, message); // Use debug level?
     __block NSMutableArray<ClientInfo *> *guiClients = [NSMutableArray array];
     // Find GUI clients safely
     [self performOnInternalQueueSync:^{
         for (ClientInfo *info in self.connectedClients.allValues) {
             if ([info.clientID hasPrefix:@"GUI"]) {
                 [(NSMutableArray*)guiClients addObject:info];
             }
         }
     }];

     // Forward to all found GUI clients
     if ([guiClients count] > 0) {
        // os_log_debug(OS_LOG_DEFAULT, "[FWADaemon] Forwarding driver log to %lu GUI client(s)", (unsigned long)[guiClients count]);
        dispatch_async(self.internalQueue, ^{
            for (ClientInfo *info in guiClients) {
                id<FWAClientNotificationProtocol> guiProxy = info.remoteProxy;
                if (guiProxy) {
                    @try {
                        [guiProxy didReceiveLogMessageFrom:@"FWADriver" level:(FWAXPCLoglevel)level message:message];
                    } @catch (NSException *exception) {
                        os_log_error(OS_LOG_DEFAULT, "[FWADaemon] Exception forwarding log to GUI client '%{public}@': %{public}@", info.clientID, exception);
                        // Consider removing this client if forwarding fails repeatedly
                    }
                } else {
                    os_log_debug(OS_LOG_DEFAULT, "Skip log to GUI '%{public}@': proxy/connection invalid.", info.clientID);
                }
            }
        });
     } else {
          // os_log_debug(OS_LOG_DEFAULT, "[FWADaemon] No GUI client connected to forward driver log.");
     }
}

// --- New XPC method for shared memory name ---
- (void)getSharedMemoryNameWithReply:(void (^)(NSString * _Nullable shmName))reply {
    if (s_FWADaemon_ObjC_logger) {
        s_FWADaemon_ObjC_logger->info("XPC: getSharedMemoryNameWithReply called.");
    }

    if (!_cppCore) { // Check if C++ core exists
        if (s_FWADaemon_ObjC_logger) {
            s_FWADaemon_ObjC_logger->error("XPC: getSharedMemoryName called but C++ core is nil!");
        }
        if (reply) reply(nil);
        return;
    }
    if (!_cppCore->isSharedMemoryInitialized()) {
        if (s_FWADaemon_ObjC_logger) {
            s_FWADaemon_ObjC_logger->error("XPC: getSharedMemoryName called but C++ core SHM not initialized!");
        }
        if (reply) reply(nil); // Or reply with an error
        return;
    }

    std::string cppShmName = _cppCore->getSharedMemoryName();
    NSString* nsShmName = nil;

    if (!cppShmName.empty()) {
        nsShmName = [NSString stringWithUTF8String:cppShmName.c_str()];
    }

    if (s_FWADaemon_ObjC_logger) {
        s_FWADaemon_ObjC_logger->info("XPC: Replying to getSharedMemoryName with: '{}'", nsShmName ? [nsShmName UTF8String] : "<nil>");
    }
    if (reply) {
        reply(nsShmName);
    }
}

// --- New FWADaemonControlProtocol Methods ---

- (void)registerClientAndStartEngine:(NSString *)clientID
          clientNotificationEndpoint:(NSXPCListenerEndpoint *)clientNotificationEndpoint
                           withReply:(void (^)(BOOL success, NSError * _Nullable error))reply {
    SPDLOG_INFO("XPC: registerClientAndStartEngine from '{}'", [clientID UTF8String] ?: "<nil_client_id>");

    if (!_cppCore) {
        SPDLOG_CRITICAL("C++ DaemonCore is not initialized!");
        if (reply) reply(NO, [NSError errorWithDomain:@"FWADaemonErrorDomain" code:1000 userInfo:@{NSLocalizedDescriptionKey:@"Daemon core not ready"}]);
        return;
    }

    // 1. Validate input parameters
    if (!clientID || [clientID length] == 0) {
        SPDLOG_ERROR("Registration failed - invalid clientID");
        if (reply) reply(NO, [NSError errorWithDomain:@"FWADaemonErrorDomain" code:1001 userInfo:@{NSLocalizedDescriptionKey:@"Invalid client ID"}]);
        return;
    }
    
    if (!clientNotificationEndpoint) {
        SPDLOG_ERROR("Registration failed - missing client notification endpoint for '{}'", [clientID UTF8String]);
        if (reply) reply(NO, [NSError errorWithDomain:@"FWADaemonErrorDomain" code:1002 userInfo:@{NSLocalizedDescriptionKey:@"Missing client notification endpoint"}]);
        return;
    }

    // 2. Setup client connection with comprehensive error handling
    NSXPCConnection *clientConnection = [[NSXPCConnection alloc] initWithListenerEndpoint:clientNotificationEndpoint];
    if (!clientConnection) {
        SPDLOG_ERROR("Failed to create XPC connection for client '{}'", [clientID UTF8String]);
        if (reply) reply(NO, [NSError errorWithDomain:@"FWADaemonErrorDomain" code:1003 userInfo:@{NSLocalizedDescriptionKey:@"Failed to create XPC connection"}]);
        return;
    }

    clientConnection.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(FWAClientNotificationProtocol)];

    // Create client info with enhanced error handling
    ClientInfo *newClientInfo = [[ClientInfo alloc] init];
    newClientInfo.clientID = clientID;
    newClientInfo.connection = clientConnection;

    // Enhanced error handler with cleanup and daemon state management
    newClientInfo.remoteProxy = [clientConnection remoteObjectProxyWithErrorHandler:^(NSError * _Nonnull error) {
        const char* clientID_cstr = [clientID UTF8String];
        const char* error_cstr = [[error description] UTF8String];
        SPDLOG_ERROR("XPC error calling remote proxy for client '{}': {}",
                     clientID_cstr ? clientID_cstr : "<nil_id>",
                     error_cstr ? error_cstr : "<nil_error>");
        
        [self performOnInternalQueueAsync:^{
            ClientInfo *info = self.connectedClients[clientID];
            if (info && info.connection == clientConnection) {
                [self.connectedClients removeObjectForKey:clientID];
                SPDLOG_INFO("Removed client '{}' due to XPC proxy error", clientID_cstr ? clientID_cstr : "<nil_id>");
                
                // If this was the last client, consider stopping the engine
                if (self.connectedClients.count == 0) {
                    SPDLOG_INFO("Last client disconnected due to error, stopping engine");
                    self->_cppCore->stopAndCleanupService();
                }
            }
        }];
    }];

    // Enhanced invalidation handler
    clientConnection.invalidationHandler = ^{
        const char* clientID_cstr = [clientID UTF8String];
        SPDLOG_INFO("Client connection invalidated for '{}'. Cleaning up...", clientID_cstr ? clientID_cstr : "<nil_id>");
        
        [self performOnInternalQueueAsync:^{
            ClientInfo *info = self.connectedClients[clientID];
            if (info && info.connection == clientConnection) {
                [self.connectedClients removeObjectForKey:clientID];
                SPDLOG_INFO("Removed client '{}' from registry due to invalidation", clientID_cstr ? clientID_cstr : "<nil_id>");
                
                // If this was the last client, stop the engine
                if (self.connectedClients.count == 0) {
                    SPDLOG_INFO("Last client disconnected, stopping engine");
                    self->_cppCore->stopAndCleanupService();
                }
            }
        }];
    };

    [clientConnection resume];

    // 3. Register client with thread-safe replacement of existing clients
    __block BOOL hadExistingClient = NO;
    [self performOnInternalQueueSync:^{
        ClientInfo* existingClient = self.connectedClients[clientID];
        if (existingClient) {
            SPDLOG_WARN("Client ID '{}' already registered. Invalidating old connection.", [clientID UTF8String]);
            [existingClient.connection invalidate];
            hadExistingClient = YES;
        }
        self.connectedClients[clientID] = newClientInfo;
        SPDLOG_INFO("Client '{}' successfully registered. Total clients: {}", [clientID UTF8String], self.connectedClients.count);
    }];

    // 4. Start the C++ Engine (idempotent - safe to call multiple times)
    SPDLOG_INFO("Starting C++ engine service for client '{}'...", [clientID UTF8String]);
    std::expected<void, FWA::DaemonCoreError> engineStartResult = _cppCore->initializeAndStartService();
    if (!engineStartResult) {
        SPDLOG_ERROR("Failed to start C++ service for client '{}': {}", [clientID UTF8String], static_cast<int>(engineStartResult.error()));
        
        // Clean up the newly registered client on engine start failure
        [self performOnInternalQueueAsync:^{
            [self.connectedClients removeObjectForKey:clientID];
            [clientConnection invalidate];
        }];
        
        NSError *error = errorFromDaemonCoreError(engineStartResult.error(), @"Failed to start daemon engine");
        if (reply) reply(NO, error);
        return;
    }

    // 5. Perform comprehensive handshake with client
    SPDLOG_INFO("Performing handshake with client '{}'...", [clientID UTF8String]);
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        id<FWAClientNotificationProtocol> remoteProxy = newClientInfo.remoteProxy;
        
        [remoteProxy daemonHandshake:^(BOOL handshakeSuccess) {
            if (handshakeSuccess) {
                SPDLOG_INFO("✅ Handshake with client '{}' succeeded", [clientID UTF8String]);
                
                // Send current daemon state to the new client
                dispatch_async(self.internalQueue, ^{
                    @try {
                        // Send current driver connection status
                        if (self.driverIsConnected) {
                            [remoteProxy driverConnectionStatusDidChange:self.driverIsConnected];
                            SPDLOG_INFO("Sent current driver status (connected) to client '{}'", [clientID UTF8String]);
                        }
                        
                        // TODO: Send current device summaries when available
                        // [remoteProxy deviceListDidUpdate:currentDeviceSummaries];
                        
                    } @catch (NSException *ex) {
                        SPDLOG_ERROR("Exception sending initial state to client '{}': {}", 
                                   [clientID UTF8String], [[ex description] UTF8String]);
                    }
                });
                
            } else {
                SPDLOG_ERROR("❌ Handshake with client '{}' failed", [clientID UTF8String]);
                
                // Consider removing the client if handshake fails
                [self performOnInternalQueueAsync:^{
                    [self.connectedClients removeObjectForKey:clientID];
                    [clientConnection invalidate];
                }];
            }
        }];
    });

    // 6. Reply with success - engine is started and client is registered
    SPDLOG_INFO("✅ Client '{}' registered and C++ engine started successfully", [clientID UTF8String]);
    if (reply) reply(YES, nil);
}

- (void)unregisterClientAndStopEngine:(NSString *)clientID
                            withReply:(void (^)(BOOL success, NSError * _Nullable error))reply {
    SPDLOG_INFO("XPC: unregisterClientAndStopEngine from '{}'", [clientID UTF8String] ?: "<nil>");

    if (!_cppCore) {
        SPDLOG_CRITICAL("C++ DaemonCore is not initialized!");
        if (reply) reply(NO, [NSError errorWithDomain:@"FWADaemonErrorDomain" code:1000 userInfo:@{NSLocalizedDescriptionKey:@"Daemon core not ready"}]);
        return;
    }

    if (!clientID || [clientID length] == 0) {
        SPDLOG_ERROR("Unregistration failed - invalid clientID");
        if (reply) reply(NO, [NSError errorWithDomain:@"FWADaemonErrorDomain" code:1001 userInfo:@{NSLocalizedDescriptionKey:@"Invalid client ID"}]);
        return;
    }

    // 1. Unregister the client with proper validation and cleanup
    __block BOOL clientWasRegistered = NO;
    __block NSUInteger remainingClientCount = 0;
    
    [self performOnInternalQueueSync:^{
        ClientInfo *info = self.connectedClients[clientID];
        if (info) {
            clientWasRegistered = YES;
            
            // Properly invalidate the connection
            @try {
                [info.connection invalidate];
            } @catch (NSException *ex) {
                SPDLOG_WARN("Exception invalidating connection for client '{}': {}", 
                           [clientID UTF8String], [[ex description] UTF8String]);
            }
            
            [self.connectedClients removeObjectForKey:clientID];
            remainingClientCount = self.connectedClients.count;
            SPDLOG_INFO("Client '{}' unregistered successfully. Remaining clients: {}", 
                       [clientID UTF8String], remainingClientCount);
        } else {
            SPDLOG_WARN("Attempted to unregister unknown client '{}'", [clientID UTF8String]);
        }
    }];

    if (!clientWasRegistered) {
        // Still return success if client wasn't registered (idempotent operation)
        SPDLOG_INFO("Client '{}' was not registered, treating unregister as success", [clientID UTF8String]);
        if (reply) reply(YES, nil);
        return;
    }

    // 2. Conditionally stop the C++ Engine based on client management policy
    BOOL shouldStopEngine = (remainingClientCount == 0);
    
    if (shouldStopEngine) {
        SPDLOG_INFO("No clients remaining after unregistering '{}', stopping C++ engine", [clientID UTF8String]);
        
        @try {
            _cppCore->stopAndCleanupService();
            SPDLOG_INFO("✅ C++ engine stopped successfully after last client '{}' unregistered", [clientID UTF8String]);
        } @catch (...) {
            SPDLOG_ERROR("❌ Exception occurred while stopping C++ engine after client '{}' unregistered", [clientID UTF8String]);
            // Continue anyway since client is unregistered
        }
    } else {
        SPDLOG_INFO("Engine kept running: {} other clients still connected after unregistering '{}'", 
                   remainingClientCount, [clientID UTF8String]);
    }

    // 3. Reply with success
    if (reply) reply(YES, nil);
}

- (void)getConnectedDeviceSummariesWithReply:(void (^)(NSArray<NSDictionary *> * _Nullable deviceSummaries, NSError * _Nullable error))reply {
    SPDLOG_INFO("XPC: getConnectedDeviceSummaries");
    if (!_cppCore) { 
        if (reply) reply(nil, [NSError errorWithDomain:@"FWADaemonError" code:1000 userInfo:@{NSLocalizedDescriptionKey:@"Daemon core not ready"}]); 
        return; 
    }

    // TODO: DaemonCore doesn't have getConnectedDevices method yet
    // For now, return empty array until the method is implemented in DaemonCore
    SPDLOG_WARN("getConnectedDeviceSummaries: DaemonCore method not implemented yet, returning empty array");
    NSArray<NSDictionary *> *summaries = @[];
    if (reply) reply(summaries, nil);
}

- (void)getDetailedDeviceInfoJSONForGUID:(uint64_t)guid
                               withReply:(void (^)(NSString * _Nullable deviceInfoJSON, NSError * _Nullable error))reply {
    SPDLOG_INFO("XPC: getDetailedDeviceInfoJSONForGUID: 0x{:016X}", guid);
    if (!_cppCore) { 
        if (reply) reply(nil, [NSError errorWithDomain:@"FWADaemonError" code:1000 userInfo:@{NSLocalizedDescriptionKey:@"Daemon core not ready"}]); 
        return; 
    }

    std::expected<std::string, FWA::DaemonCoreError> jsonResult = _cppCore->getDetailedDeviceInfoJSON(guid);
    if (!jsonResult) {
        SPDLOG_ERROR("Failed to get detailed device info for GUID 0x{:016X}: {}", guid, static_cast<int>(jsonResult.error()));
        if (reply) reply(nil, errorFromDaemonCoreError(jsonResult.error(), @"Failed to get detailed device info"));
        return;
    }

    NSString *jsonString = [NSString stringWithUTF8String:jsonResult.value().c_str()];
    if (reply) reply(jsonString, nil);
}

- (void)sendAVCCommandToDevice:(uint64_t)guid
                       command:(NSData *)commandData
                     withReply:(void (^)(NSData * _Nullable responseData, NSError * _Nullable error))reply {
    SPDLOG_INFO("XPC: sendAVCCommandToDevice: GUID=0x{:016X}, CommandBytes={}", guid, commandData ? commandData.length : 0);
    if (!_cppCore) { 
        if (reply) reply(nil, [NSError errorWithDomain:@"FWADaemonError" code:1000 userInfo:@{NSLocalizedDescriptionKey:@"Daemon core not ready"}]); 
        return; 
    }
    if (!commandData || commandData.length == 0) {
        if (reply) reply(nil, [NSError errorWithDomain:@"FWADaemonError" code:1002 userInfo:@{NSLocalizedDescriptionKey:@"Empty command data"}]);
        return;
    }

    std::vector<uint8_t> cppCommand(static_cast<const uint8_t*>(commandData.bytes),
                                    static_cast<const uint8_t*>(commandData.bytes) + commandData.length);

    std::expected<std::vector<uint8_t>, FWA::DaemonCoreError> cppResponseResult = _cppCore->sendAVCCommand(guid, cppCommand);

    if (!cppResponseResult) {
        SPDLOG_ERROR("C++ sendAVCCommand failed for GUID 0x{:016X}: {}", guid, static_cast<int>(cppResponseResult.error()));
        if (reply) reply(nil, errorFromDaemonCoreError(cppResponseResult.error(), @"Failed to send AV/C command"));
        return;
    }

    NSData *nsResponseData = [NSData dataWithBytes:cppResponseResult.value().data() length:cppResponseResult.value().size()];
    if (reply) reply(nsResponseData, nil);
}

- (void)startAudioStreamsForDevice:(uint64_t)guid
                         withReply:(void (^)(BOOL success, NSError * _Nullable error))reply {
    SPDLOG_INFO("XPC: startAudioStreamsForDevice: GUID=0x{:016X}", guid);
    if (!_cppCore) { 
        if (reply) reply(NO, [NSError errorWithDomain:@"FWADaemonError" code:1000 userInfo:@{NSLocalizedDescriptionKey:@"Daemon core not ready"}]); 
        return; 
    }

    std::expected<void, FWA::DaemonCoreError> startResult = _cppCore->startAudioStreams(guid);
    if (!startResult) {
        SPDLOG_ERROR("C++ startAudioStreams failed for GUID 0x{:016X}: {}", guid, static_cast<int>(startResult.error()));
        if (reply) reply(NO, errorFromDaemonCoreError(startResult.error(), @"Failed to start audio streams"));
        return;
    }
    if (reply) reply(YES, nil);
}

- (void)stopAudioStreamsForDevice:(uint64_t)guid
                        withReply:(void (^)(BOOL success, NSError * _Nullable error))reply {
    SPDLOG_INFO("XPC: stopAudioStreamsForDevice: GUID=0x{:016X}", guid);
    if (!_cppCore) { 
        if (reply) reply(NO, [NSError errorWithDomain:@"FWADaemonError" code:1000 userInfo:@{NSLocalizedDescriptionKey:@"Daemon core not ready"}]); 
        return; 
    }

    std::expected<void, FWA::DaemonCoreError> stopResult = _cppCore->stopAudioStreams(guid);
    if (!stopResult) {
        SPDLOG_ERROR("C++ stopAudioStreams failed for GUID 0x{:016X}: {}", guid, static_cast<int>(stopResult.error()));
        if (reply) reply(NO, errorFromDaemonCoreError(stopResult.error(), @"Failed to stop audio streams"));
        return;
    }
    if (reply) reply(YES, nil);
}

- (void)setDaemonLogLevel:(FWAXPCLoglevel)level
                withReply:(void (^)(BOOL success))reply {
    SPDLOG_INFO("XPC: setDaemonLogLevel: Level={}", static_cast<int>(level));
    if (!_cppCore) { 
        if (reply) reply(NO); 
        return; 
    }

    _cppCore->setDaemonLogLevel(static_cast<int>(fwaXPCLoglevelToSpdlogLevel(level)));
    // Also set for the ObjC FWADaemon logger if desired
    if (s_FWADaemon_ObjC_logger) {
        s_FWADaemon_ObjC_logger->set_level(fwaXPCLoglevelToSpdlogLevel(level));
    }
    if (reply) reply(YES);
}

- (void)getDaemonLogLevelWithReply:(void (^)(FWAXPCLoglevel currentLevel))reply {
    SPDLOG_INFO("XPC: getDaemonLogLevel");
    if (!_cppCore) { 
        if (reply) reply(FWAXPCLoglevelInfo); 
        return; 
    } // Default

    int currentLevel = _cppCore->getDaemonLogLevel();
    if (reply) reply(spdlogLevelToFWAXPCLoglevel(static_cast<spdlog::level::level_enum>(currentLevel)));
}

// ADDED/MOVED Method Implementations +++
- (BOOL)hasActiveGuiClients {
    __block BOOL hasGui = NO;
    // Access the dictionary safely on the internal queue
    [self performOnInternalQueueSync:^{ // Sync to ensure we get the result before returning
        if (self.connectedClients.count == 0) {
            hasGui = NO;
            return;
        }
        for (NSString *clientID in self.connectedClients) {
            if ([clientID hasPrefix:@"GUI"]) {
                // Optionally check if the connection is actually valid
                ClientInfo *info = self.connectedClients[clientID];
                if (info && info.connection) {
                    hasGui = YES;
                    break;
                }
            }
        }
    }];
    return hasGui;
}

- (void)forwardLogMessageToClients:(NSString *)senderID level:(int32_t)level message:(NSString *)message {
    // This method now assumes the check `hasActiveGuiClients` was already done
    // by the caller (GuiCallbackSink) if the intention is *only* to send to GUIs.
    // However, we keep the check here for safety if called from elsewhere.
    // Let's keep the check for robustness.

    __block NSMutableArray<ClientInfo *> *guiClients = [NSMutableArray array];
    // Find GUI clients safely
    [self performOnInternalQueueSync:^{ // Sync to safely copy client info
        for (ClientInfo *info in self.connectedClients.allValues) {
            if ([info.clientID hasPrefix:@"GUI"]) {
                // Optionally check if connection is valid before adding
                if (info.connection) {
                    [guiClients addObject:info];
                }
            }
        }
    }];

    if ([guiClients count] > 0) {
        // Use dispatch_async to avoid blocking the logging thread/sink
        // IMPORTANT: Ensure self.internalQueue is valid before dispatching
        dispatch_queue_t queue = self.internalQueue;
        if (!queue) {
            os_log_error(OS_LOG_DEFAULT, "Cannot forward log message, internal queue is nil!");
            return;
        }
        dispatch_async(queue, ^{ // Perform XPC calls asynchronously
            for (ClientInfo *info in guiClients) {
                id<FWAClientNotificationProtocol> guiProxy = info.remoteProxy;
                if (guiProxy && info.connection) {
                    @try {
                        [guiProxy didReceiveLogMessageFrom:senderID level:(FWAXPCLoglevel)level message:message];
                    } @catch (NSException *exception) {
                        const char* senderID_cstr = [senderID UTF8String];
                        const char* clientID_cstr = [info.clientID UTF8String];
                        const char* exception_cstr = [[exception description] UTF8String];
                        SPDLOG_ERROR("Exception forwarding log ({}) to GUI client '{}': {}",
                                     senderID_cstr ? senderID_cstr : "<nil_sender>",
                                     clientID_cstr ? clientID_cstr : "<nil_client_id>",
                                     exception_cstr ? exception_cstr : "<nil_exception>");
                    } @finally {
                        // ARC handles proxy release
                    }
                } else {
                    os_log_debug(OS_LOG_DEFAULT, "Skip log to GUI '%{public}@': proxy/connection invalid.", info.clientID);
                }
            }
        });
    }
    // No need for an 'else' log here, the sink already decided not to call if no clients.
}


// END ADDED/MOVED Method Implementations +++

@end
