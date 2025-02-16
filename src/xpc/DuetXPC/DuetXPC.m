#import "DuetXPC.h"

@interface DuetXPC ()
// Private mutable array for internal use.
@property (nonatomic, strong) NSMutableArray<NSXPCConnection *> *mutableClients;
@property (nonatomic, strong) dispatch_queue_t xpcQueue; // Dedicated high-priority queue
@end

@implementation DuetXPC

+ (instancetype)sharedService {
    static DuetXPC *sharedInstance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sharedInstance = [[DuetXPC alloc] init];
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
        NSLog(@"[DuetXPC] High-priority XPC queue created.");
    }
    return self;
}

// Override the getter to expose the mutable array as an immutable NSArray.
- (NSArray<NSXPCConnection *> *)clients {
    return [self.mutableClients copy];
}

- (void)sendAudioBuffer:(MixedAudioBuffer *)buffer withReply:(void (^)(BOOL))reply {
    dispatch_async(self.xpcQueue, ^{
        NSLog(@"[DuetXPC] Sending audio buffer (size: %lu bytes) on high-priority queue", (unsigned long)buffer.pcmData.length);
        
        for (NSXPCConnection *clientConn in self.mutableClients) {
            id<DuetXPCClientProtocol> client = [clientConn remoteObjectProxy];
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
        NSLog(@"[DuetXPC] getStreamFormatWithReply called on high-priority queue.");
        if (reply) {
            reply(@"PCM 48kHz, 24-bit, Stereo");
        }
    });
}

- (void)handshakeWithReply:(void (^)(BOOL))reply {
    dispatch_async(self.xpcQueue, ^{
        NSLog(@"[DuetXPC] Handshake received on high-priority queue.");
        if (reply) {
            reply(YES);
        }
    });
}

- (void)registerClientWithEndpoint:(NSXPCListenerEndpoint *)clientEndpoint {
    NSLog(@"[DuetXPC] Received client endpoint: %@", clientEndpoint);
    NSXPCConnection *clientConnection = [[NSXPCConnection alloc] initWithListenerEndpoint:clientEndpoint];
    clientConnection.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(DuetXPCClientProtocol)];
    [clientConnection resume];
    
    // Add client connection on our high-priority queue.
    dispatch_async(self.xpcQueue, ^{
        [self.mutableClients addObject:clientConnection];
        NSLog(@"[DuetXPC] Client connection registered on high-priority queue.");
    });
}

@end
