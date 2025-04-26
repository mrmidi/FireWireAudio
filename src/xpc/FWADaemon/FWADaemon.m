#import "FWADaemon.h"
#import "FWAClientNotificationProtocol.h"
#import "FWADaemonControlProtocol.h"

// Simple class to hold client info
@interface ClientInfo : NSObject
@property (nonatomic, copy) NSString *clientID;
@property (nonatomic, strong) NSXPCConnection *connection;
@property (nonatomic, strong) id<FWAClientNotificationProtocol> remoteProxy;
@end
@implementation ClientInfo
@end

@interface FWADaemon () <FWADaemonControlProtocol>
// Private mutable array for internal use.
@property (nonatomic, strong) NSMutableArray<NSXPCConnection *> *mutableClients;
@property (nonatomic, strong) dispatch_queue_t xpcQueue; // Dedicated high-priority queue
@property (nonatomic, strong) NSMutableDictionary<NSString *, ClientInfo *> *connectedClients;
@property (nonatomic, strong) dispatch_queue_t internalQueue;
@end

@implementation FWADaemon

+ (instancetype)sharedService {
    static FWADaemon *sharedInstance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sharedInstance = [[FWADaemon alloc] init];
    });
    return sharedInstance;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _mutableClients = [NSMutableArray array];
        // Create a dedicated serial dispatch queue with high priority.
        _xpcQueue = dispatch_queue_create("net.mrmidi.xpcQueue",
                                          dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INTERACTIVE, 0));
        NSLog(@"[FWADaemon] High-priority XPC queue created.");
        _internalQueue = dispatch_queue_create("net.mrmidi.FWADaemon.internalQueue", DISPATCH_QUEUE_SERIAL);
        _connectedClients = [NSMutableDictionary dictionary];
        NSLog(@"[FWADaemon] Initialized FWADaemon singleton and internal queue.");
    }
    return self;
}

// Override the getter to expose the mutable array as an immutable NSArray.
- (NSArray<NSXPCConnection *> *)clients {
    return [self.mutableClients copy];
}

- (void)sendAudioBuffer:(MixedAudioBuffer *)buffer withReply:(void (^)(BOOL))reply {
    dispatch_async(self.xpcQueue, ^{
        NSLog(@"[FWADaemon] Sending audio buffer (size: %lu bytes) on high-priority queue", (unsigned long)buffer.pcmData.length);
        
        for (NSXPCConnection *clientConn in self.mutableClients) {
            id<FWADaemonClientProtocol> client = [clientConn remoteObjectProxy];
            if ([client respondsToSelector:@selector(didReceiveAudioBuffer:)]) {
                [client didReceiveAudioBuffer:buffer];
            }
        }
        
        if (reply) {
            reply(YES);
        }
    });
}

- (void)getStreamFormatWithReply:(void (^)(NSString *))reply {
    dispatch_async(self.xpcQueue, ^{
        NSLog(@"[FWADaemon] getStreamFormatWithReply called on high-priority queue.");
        if (reply) {
            reply(@"PCM 48kHz, 24-bit, Stereo");
        }
    });
}

- (void)handshakeWithReply:(void (^)(BOOL))reply {
    dispatch_async(self.xpcQueue, ^{
        NSLog(@"[FWADaemon] Handshake received on high-priority queue.");
        if (reply) {
            reply(YES);
        }
    });
}

- (void)registerClientWithEndpoint:(NSXPCListenerEndpoint *)clientEndpoint {
    NSLog(@"[FWADaemon] Received client endpoint: %@", clientEndpoint);
    NSXPCConnection *clientConnection = [[NSXPCConnection alloc] initWithListenerEndpoint:clientEndpoint];
    clientConnection.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(FWADaemonClientProtocol)];
    [clientConnection resume];
    
    // Add client connection on our high-priority queue.
    dispatch_async(self.xpcQueue, ^{
        [self.mutableClients addObject:clientConnection];
        NSLog(@"[FWADaemon] Client connection registered on high-priority queue.");
    });
}

- (void)requestSetClockSource:(uint64_t)guid clockSourceID:(uint32_t)clockSourceID withReply:(void (^)(BOOL success))reply {
    NSLog(@"[FWADaemon] Received requestSetClockSource for GUID 0x%llx to ID %u", guid, clockSourceID);
    id<FWAClientNotificationProtocol> guiProxy = nil;
    __block NSArray<ClientInfo *> *allClients = nil;
    [self performOnInternalQueue:^{
        allClients = self.connectedClients.allValues;
    }];
    for (ClientInfo *info in allClients) {
        if ([info.clientID hasPrefix:@"GUI"]) {
            guiProxy = info.remoteProxy;
            break;
        }
    }
    if (guiProxy) {
        [guiProxy performSetClockSource:guid clockSourceID:clockSourceID withReply:^(BOOL guiSuccess) {
            if (reply) reply(guiSuccess);
        }];
    } else {
        NSLog(@"[FWADaemon] ERROR: No GUI client for requestSetClockSource.");
        if (reply) reply(NO);
    }
}

- (void)requestSetMasterVolumeScalar:(uint64_t)guid scope:(uint32_t)scope element:(uint32_t)element scalarValue:(float)scalarValue withReply:(void (^)(BOOL success))reply {
    NSLog(@"[FWADaemon] Received requestSetMasterVolumeScalar for GUID 0x%llx (Scope %u, Elem %u) to %.3f", guid, scope, element, scalarValue);
    id<FWAClientNotificationProtocol> guiProxy = nil;
    __block NSArray<ClientInfo *> *allClients = nil;
    [self performOnInternalQueue:^{
        allClients = self.connectedClients.allValues;
    }];
    for (ClientInfo *info in allClients) {
        if ([info.clientID hasPrefix:@"GUI"]) {
            guiProxy = info.remoteProxy;
            break;
        }
    }
    if (guiProxy) {
        [guiProxy performSetMasterVolumeScalar:guid scope:scope element:element scalarValue:scalarValue withReply:^(BOOL guiSuccess) {
            if (reply) reply(guiSuccess);
        }];
    } else {
        NSLog(@"[FWADaemon] ERROR: No GUI client for requestSetMasterVolumeScalar.");
        if (reply) reply(NO);
    }
}

- (void)requestSetMasterMute:(uint64_t)guid scope:(uint32_t)scope element:(uint32_t)element muteState:(BOOL)muteState withReply:(void (^)(BOOL success))reply {
    NSLog(@"[FWADaemon] Received requestSetMasterMute for GUID 0x%llx (Scope %u, Elem %u) to %d", guid, scope, element, muteState);
    id<FWAClientNotificationProtocol> guiProxy = nil;
    __block NSArray<ClientInfo *> *allClients = nil;
    [self performOnInternalQueue:^{
        allClients = self.connectedClients.allValues;
    }];
    for (ClientInfo *info in allClients) {
        if ([info.clientID hasPrefix:@"GUI"]) {
            guiProxy = info.remoteProxy;
            break;
        }
    }
    if (guiProxy) {
        [guiProxy performSetMasterMute:guid scope:scope element:element muteState:muteState withReply:^(BOOL guiSuccess) {
            if (reply) reply(guiSuccess);
        }];
    } else {
        NSLog(@"[FWADaemon] ERROR: No GUI client for requestSetMasterMute.");
        if (reply) reply(NO);
    }
}

- (void)performOnInternalQueue:(dispatch_block_t)block {
    dispatch_sync(self.internalQueue, block);
}

#pragma mark - FWADaemonControlProtocol Implementation Stubs

// --- Registration & Lifecycle ---
- (void)registerClient:(NSString *)clientID
clientNotificationEndpoint:(NSXPCListenerEndpoint *)clientNotificationEndpoint
           withReply:(void (^)(BOOL success, NSDictionary * _Nullable daemonInfo))reply
{
    NSLog(@"[FWADaemon] Received registerClient request from '%@'", clientID);
    if (!clientID || [clientID length] == 0 || !clientNotificationEndpoint) {
        NSLog(@"[FWADaemon] ERROR: Registration failed - invalid clientID or endpoint for '%@'.", clientID ?: @"<nil>");
        if (reply) reply(NO, nil);
        return;
    }
    NSXPCConnection *clientConnection = [[NSXPCConnection alloc] initWithListenerEndpoint:clientNotificationEndpoint];
    clientConnection.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(FWAClientNotificationProtocol)];
    ClientInfo *newClientInfo = [[ClientInfo alloc] init];
    newClientInfo.clientID = clientID;
    newClientInfo.connection = clientConnection;
    newClientInfo.remoteProxy = [clientConnection remoteObjectProxyWithErrorHandler:^(NSError * _Nonnull error) {
        NSLog(@"[FWADaemon] ERROR: XPC error calling remote proxy for client '%@': %@", clientID, error);
        dispatch_async(self.internalQueue, ^{
             ClientInfo *info = self.connectedClients[clientID];
             if (info && info.connection == clientConnection) {
                 NSLog(@"[FWADaemon] Removing client '%@' due to remote proxy error.", clientID);
                 [self.connectedClients removeObjectForKey:clientID];
             }
         });
    }];
    clientConnection.invalidationHandler = ^{
        NSLog(@"[FWADaemon] Client connection invalidated for '%@'.", clientID);
        dispatch_async(self.internalQueue, ^{
            ClientInfo *info = self.connectedClients[clientID];
            if (info && info.connection == clientConnection) {
                NSLog(@"[FWADaemon] Removing client '%@' from registry.", clientID);
                [self.connectedClients removeObjectForKey:clientID];
            } else {
                 NSLog(@"[FWADaemon] Invalidation handler: Client '%@' not found or connection mismatch.", clientID);
            }
        });
    };
    [clientConnection resume];
    [self performOnInternalQueue:^{
        if (self.connectedClients[clientID]) {
            NSLog(@"[FWADaemon] WARNING: Client ID '%@' already registered. Overwriting.", clientID);
            [self.connectedClients[clientID].connection invalidate];
        }
        self.connectedClients[clientID] = newClientInfo;
        NSLog(@"[FWADaemon] Client '%@' successfully registered.", clientID);
    }];
    NSDictionary *daemonInfo = @{ @"daemonVersion": @"0.1.0-stub" };
    if (reply) reply(YES, daemonInfo);
}

- (void)unregisterClient:(NSString *)clientID {
     NSLog(@"[FWADaemon] Received unregisterClient request from '%@'", clientID);
     [self performOnInternalQueue:^{
         ClientInfo *info = self.connectedClients[clientID];
         if (info) {
             NSLog(@"[FWADaemon] STUB: Invalidating connection and removing client '%@'.", clientID);
             [info.connection invalidate];
             [self.connectedClients removeObjectForKey:clientID];
         } else {
             NSLog(@"[FWADaemon] WARNING: unregisterClient called for unknown clientID '%@'.", clientID);
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
    NSLog(@"[FWADaemon] Received updateDeviceConnectionStatus for GUID 0x%llx: connected=%d, initialized=%d, name='%@', vendor='%@'",
          guid, isConnected, isInitialized, deviceName, vendorName);
    // STUB: Log receipt, will later cache and broadcast.
}

- (void)updateDeviceConfiguration:(uint64_t)guid configInfo:(NSDictionary *)configInfo {
    NSLog(@"[FWADaemon] Received updateDeviceConfiguration for GUID 0x%llx: %@", guid, configInfo);
    // STUB: Log receipt, will later cache and broadcast.
}

- (void)getDeviceConnectionStatus:(uint64_t)guid withReply:(void (^)(NSDictionary * _Nullable statusInfo))reply {
    NSLog(@"[FWADaemon] Received getDeviceConnectionStatus request for GUID 0x%llx", guid);
    NSLog(@"[FWADaemon] STUB: Replying nil to getDeviceConnectionStatus for GUID 0x%llx.", guid);
    if (reply) reply(nil);
}

- (void)getDeviceConfiguration:(uint64_t)guid withReply:(void (^)(NSDictionary * _Nullable configInfo))reply {
    NSLog(@"[FWADaemon] Received getDeviceConfiguration request for GUID 0x%llx", guid);
    NSLog(@"[FWADaemon] STUB: Replying nil to getDeviceConfiguration for GUID 0x%llx.", guid);
    if (reply) reply(nil);
}

- (void)getConnectedDeviceGUIDsWithReply:(void (^)(NSArray<NSNumber *> * _Nullable guids))reply {
     NSLog(@"[FWADaemon] Received getConnectedDeviceGUIDs request");
     NSLog(@"[FWADaemon] STUB: Replying empty array to getConnectedDeviceGUIDs.");
     if (reply) reply(@[]);
}

// --- Control Commands (Driver -> Daemon -> GUI) ---
- (void)requestSetNominalSampleRate:(uint64_t)guid rate:(double)rate withReply:(void (^)(BOOL success))reply {
    NSLog(@"[FWADaemon] Received requestSetNominalSampleRate for GUID 0x%llx to %.1f Hz", guid, rate);
    NSLog(@"[FWADaemon] STUB: Replying success to requestSetNominalSampleRate.");
    if (reply) reply(YES);
}

// --- IO State (Driver -> Daemon -> GUI) ---
- (void)requestStartIO:(uint64_t)guid withReply:(void (^)(BOOL success))reply {
    NSLog(@"[FWADaemon] Received requestStartIO for GUID 0x%llx", guid);
    NSLog(@"[FWADaemon] STUB: Replying success to requestStartIO.");
    if (reply) reply(YES);
}

- (void)requestStopIO:(uint64_t)guid {
    NSLog(@"[FWADaemon] Received requestStopIO for GUID 0x%llx", guid);
    NSLog(@"[FWADaemon] STUB: Logged requestStopIO.");
}

// --- Logging (Driver -> Daemon -> GUI) ---
- (void)forwardLogMessageFromDriver:(int32_t)level message:(NSString *)message {
     NSLog(@"[FWADaemon] Received log from Driver (Level %d): %@", level, message);
     __block NSArray<ClientInfo *> *allClients;
     [self performOnInternalQueue:^{
        allClients = self.connectedClients.allValues;
    }];
    for (ClientInfo *info in allClients) {
         if ([info.clientID hasPrefix:@"GUI"]) {
              id<FWAClientNotificationProtocol> guiProxy = info.remoteProxy;
              if (guiProxy) {
                    NSLog(@"[FWADaemon] STUB: Forwarding driver log to GUI client '%@'", info.clientID);
                    [guiProxy didReceiveLogMessageFrom:@"FWADriver" level:level message:message];
              }
         }
    }
}

@end
