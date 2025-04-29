#import "FWADaemon.h" // Import the interface declaration
#import "shared/xpc/FWAClientNotificationProtocol.h" // Correct client protocol
// #import "shared/xpc/FWADaemonControlProtocol.h" // Protocol imported via FWADaemon.h
#import <Foundation/Foundation.h>
#import <os/log.h> // Use os_log
#import <sys/mman.h>
#import <sys/stat.h>
#import <fcntl.h>
#import <unistd.h>
#import "shared/SharedMemoryStructures.hpp"
#import "shared/xpc/RingBufferManager.hpp"
// +++ spdlog includes +++
#define SPDLOG_ENABLE_HIGH_RESOLUTION_TIMER
#include <spdlog/spdlog.h>
#include <spdlog/pattern_formatter.h>
// +++ custom sinks +++
#include "OsLogSink.hpp"
#include "GuiCallbackSink.hpp"

// +++ ADDED: Define the shared memory name +++
static const char* kSharedMemoryName = "/fwa_daemon_shm_v1";

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
@property (nonatomic, assign) BOOL driverIsConnected; // New state variable
@property (nonatomic, strong) dispatch_queue_t internalQueue;
@property (nonatomic, copy) NSString *sharedMemoryName;
// Note: mutableClients and xpcQueue seem redundant if connectedClients and internalQueue are used properly.
// Let's remove them for now unless needed for a specific reason.
// @property (nonatomic, strong) NSMutableArray<NSXPCConnection *> *mutableClients;
// @property (nonatomic, strong) dispatch_queue_t xpcQueue;
@end

@implementation FWADaemon

+ (instancetype)sharedService {
    static FWADaemon *sharedInstance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sharedInstance = [[FWADaemon alloc] init]; // Use alloc/init
    });
    return sharedInstance;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        self.internalQueue = dispatch_queue_create("net.mrmidi.FWADaemon.internalQueue", DISPATCH_QUEUE_SERIAL);
        self.connectedClients = [NSMutableDictionary dictionary];
        _driverIsConnected = NO;
        self.sharedMemoryName = @(kSharedMemoryName);
        // +++ Phase 3 spdlog Initialization (OsLog + GUI Sinks) +++
        try {
            // Create the sinks (thread-safe)
            auto os_sink = std::make_shared<os_log_sink_mt>(OS_LOG_DEFAULT);
            auto gui_sink = std::make_shared<gui_callback_sink_mt>(self);
            #ifdef DEBUG
            auto stderr_sink = std::make_shared<spdlog::sinks::stderr_sink_mt>();
            std::vector<spdlog::sink_ptr> sinks { os_sink, gui_sink, stderr_sink };
            #else
            std::vector<spdlog::sink_ptr> sinks { os_sink, gui_sink };
            #endif

            // Set per-sink patterns
            os_sink->set_pattern("[%^%l%$] %v"); // Simpler for os_log
            gui_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v"); // Detailed for GUI
            // Optionally set pattern for stderr_sink if present
            #ifdef DEBUG
            stderr_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
            #endif

            // Create the logger using the sinks
            auto logger = std::make_shared<spdlog::logger>("FWADaemon", sinks.begin(), sinks.end());
            logger->set_level(spdlog::level::trace);        // Log everything during setup/testing
            logger->flush_on(spdlog::level::warn);          // Flush automatically on warnings and above

            // Register the logger
            spdlog::register_logger(logger);
            // Set as default
            spdlog::set_default_logger(logger);

            // Log initialization success via spdlog itself
            SPDLOG_INFO("--------------------------------------------------");
            SPDLOG_INFO("FWADaemon spdlog initialized. Sinks: os_log, GUI_XPC.");
            SPDLOG_INFO("FWADaemon singleton and internal queue ready.");
        } catch (const spdlog::spdlog_ex& ex) {
            os_log_error(OS_LOG_DEFAULT, "[FWADaemon] !!! spdlog initialization failed: %{public}s", ex.what());
        } catch (const std::exception& std_ex) {
            os_log_error(OS_LOG_DEFAULT, "[FWADaemon] !!! std exception during spdlog initialization: %{public}s", std_ex.what());
        } catch (...) {
            os_log_error(OS_LOG_DEFAULT, "[FWADaemon] !!! Unknown exception during spdlog initialization.");
        }
        // +++ END Phase 3 spdlog Initialization +++

        [self setupSharedMemory];
    }
    return self;
}

// +++ ADDED: Shared Memory Creation Method +++
- (BOOL)setupSharedMemory {
    SPDLOG_INFO("Attempting to create/open shared memory segment: {}", kSharedMemoryName);
    shm_unlink(kSharedMemoryName); // Optional: Remove any stale segment
    int fd = shm_open(kSharedMemoryName, O_CREAT | O_EXCL | O_RDWR, 0666);
    bool isCreator = false;
    if (fd == -1 && errno == EEXIST) {
        SPDLOG_INFO("SHM segment {} already exists, opening...", kSharedMemoryName);
        fd = shm_open(kSharedMemoryName, O_RDWR, 0666);
        isCreator = false;
    } else if (fd != -1) {
        SPDLOG_INFO("SHM segment {} created successfully.", kSharedMemoryName);
        isCreator = true;
    }
    if (fd == -1) {
        SPDLOG_ERROR("shm_open failed for {}: {} - {}", kSharedMemoryName, errno, strerror(errno));
        return NO;
    }
    off_t requiredSize = sizeof(RTShmRing::SharedRingBuffer_POD);
    if (isCreator) {
        if (ftruncate(fd, requiredSize) == -1) {
            SPDLOG_ERROR("ftruncate failed for {} (fd {}): {} - {}", kSharedMemoryName, fd, errno, strerror(errno));
            close(fd);
            shm_unlink(kSharedMemoryName);
            return NO;
        }
    }
    SPDLOG_INFO("Shared memory (fd {}, creator={}). Mapping with RingBufferManager...", fd, isCreator);
    bool mapSuccess = RingBufferManager::instance().map(fd, isCreator);
    close(fd);
    if (!mapSuccess) {
        SPDLOG_ERROR("RingBufferManager failed to map shared memory {}.", kSharedMemoryName);
        shm_unlink(kSharedMemoryName);
        return NO;
    }
    SPDLOG_INFO("Shared memory '{}' successfully created/mapped.", kSharedMemoryName);
    return YES;
}

// Remove obsolete clients getter if mutableClients is removed
// - (NSArray<NSXPCConnection *> *)clients {
//     return [self.mutableClients copy];
// }

// REMOVE OBSOLETE METHODS like sendAudioBuffer, getStreamFormat, handshake, registerClientWithEndpoint

// Helper for safe queue access
- (void)performOnInternalQueueSync:(dispatch_block_t)block {
    // Ensure queue exists before using it
    if (self.internalQueue) {
        dispatch_sync(self.internalQueue, block);
    } else {
        os_log_error(OS_LOG_DEFAULT, "[FWADaemon] Internal queue is nil in performOnInternalQueueSync!");
    }
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
- (void)registerClient:(NSString *)clientID
clientNotificationEndpoint:(NSXPCListenerEndpoint *)clientNotificationEndpoint
           withReply:(void (^)(BOOL success, NSDictionary * _Nullable daemonInfo))reply
{
    SPDLOG_INFO("Received registerClient request from '{}'", [clientID UTF8String] ?: "<nil_client_id>");
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

    // Prepare reply info
    NSDictionary *daemonInfo = @{ @"daemonVersion": @"0.1.0-stub" };
    if (reply) reply(YES, daemonInfo);
}

- (void)unregisterClient:(NSString *)clientID {
     os_log_info(OS_LOG_DEFAULT, "[FWADaemon] Received unregisterClient request from '%{public}@'", clientID);
     [self performOnInternalQueueSync:^{ // Use sync to ensure removal is processed promptly
         ClientInfo *info = self.connectedClients[clientID];
         if (info) {
             os_log_info(OS_LOG_DEFAULT, "[FWADaemon] Invalidating connection and removing client '%{public}@'.", clientID);
             [info.connection invalidate]; // Explicitly invalidate
             [self.connectedClients removeObjectForKey:clientID];
         } else {
             os_log_info(OS_LOG_DEFAULT, "[FWADaemon] WARNING: unregisterClient called for unknown clientID '%{public}@'.", clientID);
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
     os_log_info(OS_LOG_DEFAULT, "[FWADaemon] Received updateDeviceConnectionStatus for GUID 0x%llx: connected=%d, initialized=%d, name='%{public}@', vendor='%{public}@'",
          guid, isConnected, isInitialized, deviceName, vendorName);
    // TODO: Cache state and broadcast to Driver client
}

- (void)updateDeviceConfiguration:(uint64_t)guid configInfo:(NSDictionary *)configInfo {
    os_log_info(OS_LOG_DEFAULT, "[FWADaemon] Received updateDeviceConfiguration for GUID 0x%llx: %{public}@", guid, configInfo);
     // TODO: Cache state and broadcast to Driver client
}

- (void)getDeviceConnectionStatus:(uint64_t)guid withReply:(void (^)(NSDictionary * _Nullable statusInfo))reply {
    os_log_info(OS_LOG_DEFAULT, "[FWADaemon] Received getDeviceConnectionStatus request for GUID 0x%llx", guid);
    // TODO: Read from cache
    os_log_info(OS_LOG_DEFAULT, "[FWADaemon] STUB: Replying nil to getDeviceConnectionStatus for GUID 0x%llx.", guid);
    if (reply) reply(nil);
}

- (void)getDeviceConfiguration:(uint64_t)guid withReply:(void (^)(NSDictionary * _Nullable configInfo))reply {
    os_log_info(OS_LOG_DEFAULT, "[FWADaemon] Received getDeviceConfiguration request for GUID 0x%llx", guid);
    // TODO: Read from cache
    os_log_info(OS_LOG_DEFAULT, "[FWADaemon] STUB: Replying nil to getDeviceConfiguration for GUID 0x%llx.", guid);
    if (reply) reply(nil);
}

- (void)getConnectedDeviceGUIDsWithReply:(void (^)(NSArray<NSNumber *> * _Nullable guids))reply {
     os_log_info(OS_LOG_DEFAULT, "[FWADaemon] Received getConnectedDeviceGUIDs request");
     // TODO: Read GUIDs from cached status
     os_log_info(OS_LOG_DEFAULT, "[FWADaemon] STUB: Replying empty array to getConnectedDeviceGUIDs.");
     if (reply) reply(@[]);
}

// --- Driver Presence ---
- (void)setDriverPresenceStatus:(BOOL)isPresent {
    os_log_info(OS_LOG_DEFAULT, "[FWADaemon] Received setDriverPresenceStatus: %{public}d", isPresent);
    [self performOnInternalQueueAsync:^{
        if (self.driverIsConnected != isPresent) {
            os_log_info(OS_LOG_DEFAULT, "[FWADaemon] Driver connection status changed to %{public}d. Broadcasting...", isPresent);
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
    os_log_info(OS_LOG_DEFAULT, "[FWADaemon] Broadcasting driverConnectionStatusDidChange: %{public}d", isConnected);
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
     os_log_info(OS_LOG_DEFAULT, "[FWADaemon] Forwarding requestSetClockSource for GUID 0x%llx to ID %u", guid, clockSourceID);
    id<FWAClientNotificationProtocol> guiProxy = [self getGUIProxyForGUID:guid];
    if (guiProxy) {
        [guiProxy performSetClockSource:guid clockSourceID:clockSourceID withReply:^(BOOL guiSuccess) {
             os_log_info(OS_LOG_DEFAULT, "[FWADaemon] Relaying reply (%d) for requestSetClockSource GUID 0x%llx", guiSuccess, guid);
            if (reply) reply(guiSuccess);
        }];
    } else {
        os_log_error(OS_LOG_DEFAULT, "[FWADaemon] ERROR: No GUI client for requestSetClockSource GUID 0x%llx.", guid);
        if (reply) reply(NO);
    }
}

- (void)requestSetMasterVolumeScalar:(uint64_t)guid scope:(uint32_t)scope element:(uint32_t)element scalarValue:(float)scalarValue withReply:(void (^)(BOOL success))reply {
     os_log_info(OS_LOG_DEFAULT, "[FWADaemon] Forwarding requestSetMasterVolumeScalar for GUID 0x%llx (Scope %u, Elem %u) to %.3f", guid, scope, element, scalarValue);
     id<FWAClientNotificationProtocol> guiProxy = [self getGUIProxyForGUID:guid];
    if (guiProxy) {
        [guiProxy performSetMasterVolumeScalar:guid scope:scope element:element scalarValue:scalarValue withReply:^(BOOL guiSuccess) {
             os_log_info(OS_LOG_DEFAULT, "[FWADaemon] Relaying reply (%d) for requestSetMasterVolumeScalar GUID 0x%llx", guiSuccess, guid);
            if (reply) reply(guiSuccess);
        }];
    } else {
        os_log_error(OS_LOG_DEFAULT, "[FWADaemon] ERROR: No GUI client for requestSetMasterVolumeScalar GUID 0x%llx.", guid);
        if (reply) reply(NO);
    }
}

- (void)requestSetMasterMute:(uint64_t)guid scope:(uint32_t)scope element:(uint32_t)element muteState:(BOOL)muteState withReply:(void (^)(BOOL success))reply {
     os_log_info(OS_LOG_DEFAULT, "[FWADaemon] Forwarding requestSetMasterMute for GUID 0x%llx (Scope %u, Elem %u) to %d", guid, scope, element, muteState);
     id<FWAClientNotificationProtocol> guiProxy = [self getGUIProxyForGUID:guid];
    if (guiProxy) {
        [guiProxy performSetMasterMute:guid scope:scope element:element muteState:muteState withReply:^(BOOL guiSuccess) {
             os_log_info(OS_LOG_DEFAULT, "[FWADaemon] Relaying reply (%d) for requestSetMasterMute GUID 0x%llx", guiSuccess, guid);
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
     __block NSArray<ClientInfo *> *guiClients = [NSMutableArray array];
     // Find GUI clients safely
     [self performOnInternalQueueSync:^{
         for (ClientInfo *info in self.connectedClients.allValues) {
             if ([info.clientID hasPrefix:@"GUI"]) {
                 [(NSMutableArray*)guiClients addObject:info]; // Cast needed inside block sometimes
             }
         }
     }];

     // Forward to all found GUI clients
     if ([guiClients count] > 0) {
        // os_log_debug(OS_LOG_DEFAULT, "[FWADaemon] Forwarding driver log to %lu GUI client(s)", (unsigned long)[guiClients count]);
         for (ClientInfo *info in guiClients) {
             id<FWAClientNotificationProtocol> guiProxy = info.remoteProxy;
             if (guiProxy) {
                 @try {
                    [guiProxy didReceiveLogMessageFrom:@"FWADriver" level:level message:message];
                 } @catch (NSException *exception) {
                    os_log_error(OS_LOG_DEFAULT, "[FWADaemon] Exception forwarding log to GUI client '%{public}@': %{public}@", info.clientID, exception);
                    // Consider removing this client if forwarding fails repeatedly
                 }
             }
         }
     } else {
          // os_log_debug(OS_LOG_DEFAULT, "[FWADaemon] No GUI client connected to forward driver log.");
     }
}

// --- New XPC method for shared memory name ---
- (void)getSharedMemoryNameWithReply:(void (^)(NSString * _Nullable shmName))reply {
    // Ensure SHM setup has likely occurred (best effort check)
    if (!RingBufferManager::instance().isMapped()) { // Assumes RingBufferManager has an isMapped() method
         os_log_error(OS_LOG_DEFAULT, "[FWADaemon] ERROR: getSharedMemoryName called but SHM is not mapped!");
         if(reply) reply(nil);
         return;
    }
    NSString* name = @(kSharedMemoryName); // Convert C string constant to NSString
    os_log_info(OS_LOG_DEFAULT, "[FWADaemon] Replying to getSharedMemoryName with: %{public}@", name);
    if (reply) {
        reply(name);
    }
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
            SPDLOG_ERROR("Cannot forward log message, internal queue is nil!");
            return;
        }
        dispatch_async(queue, ^{ // Perform XPC calls asynchronously
            for (ClientInfo *info in guiClients) {
                id<FWAClientNotificationProtocol> guiProxy = info.remoteProxy;
                // Perform extra check for proxy validity inside async block
                if (guiProxy && info.connection) {
                    @try {
                        // Use the actual senderID now
                        [guiProxy didReceiveLogMessageFrom:senderID level:level message:message];
                    } @catch (NSException *exception) {
                        // Use spdlog for logging daemon errors now
                        // CONVERT NSString* arguments to const char* using UTF8String
                        const char* senderID_cstr = [senderID UTF8String];
                        const char* clientID_cstr = [info.clientID UTF8String];
                        const char* exception_cstr = [[exception description] UTF8String];

                        // Use the C-string pointers in the spdlog call
                        SPDLOG_ERROR("Exception forwarding log ({}) to GUI client '{}': {}",
                                     senderID_cstr ? senderID_cstr : "<nil_sender>",        // Use ternary for safety
                                     clientID_cstr ? clientID_cstr : "<nil_client_id>",     // Use ternary for safety
                                     exception_cstr ? exception_cstr : "<nil_exception>"); // Use ternary for safety
                        // Maybe invalidate this client connection if exceptions persist?
                        // Consider adding logic here to remove the client if sending fails repeatedly.
                    } @finally {
                        // ARC handles proxy release
                    }
                } else {
                    SPDLOG_WARN("Skipping log forward to client '{}': Proxy or connection invalid.", [info.clientID UTF8String]);
                }
            }
        });
    }
    // No need for an 'else' log here, the sink already decided not to call if no clients.
}
// END ADDED/MOVED Method Implementations +++

@end
