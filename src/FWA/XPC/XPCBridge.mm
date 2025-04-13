// XPCBridge.mm

#import "FWA/XPC/XPCBridge.h"
#import <Foundation/Foundation.h>
#import "FWA/XPC/DuetXPCProtocol.h"
#import "FWA/XPC/XPCReceiverClient.hpp"
#import "FWA/XPC/MixedAudioBuffer.h"

// C++ Headers
#include "Isoch/interfaces/ITransmitPacketProvider.hpp"
#include <chrono>

// Global variable for our XPC connection TO the service.
static NSXPCConnection *xpcConnection = nil;

// Global variables for the client listener FOR the service.
static NSXPCListener *clientListener = nil;
static XPCReceiverClient *receiverClient = nil; // Handles callbacks *to* this app

// --- SINGLE Global variable to store the Packet Provider pointer ---
// --- This is passed in from C++ during initialization ---
static FWA::Isoch::ITransmitPacketProvider* g_isoPacketProvider = nullptr; // Use ONE name

// Delegate Definition
@interface ClientListenerDelegate : NSObject <NSXPCListenerDelegate>
@end

@implementation ClientListenerDelegate
- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)newConnection {
    NSLog(@"[XPCBridge] Accepting new connection for client callbacks (from DuetXPC service)...");

    NSXPCInterface *clientInterface = [NSXPCInterface interfaceWithProtocol:@protocol(DuetXPCClientProtocol)];
    [clientInterface setClasses:[NSSet setWithObject:[MixedAudioBuffer class]]
                    forSelector:@selector(didReceiveAudioBuffer:)
                  argumentIndex:0
                        ofReply:NO];

    newConnection.exportedInterface = clientInterface;

    // --- Initialize or reuse the XPCReceiverClient ---
    if (!receiverClient) {
        receiverClient = [[XPCReceiverClient alloc] init]; // processor is NULL here
        NSLog(@"[XPCBridge] XPCReceiverClient newly initialized: %@", receiverClient);

        // Retrieve the globally stored pointer passed via XPCBridgeInitialize
        if (g_isoPacketProvider) { 
            receiverClient.processor = g_isoPacketProvider; 
            NSLog(@"[XPCBridge] ---> Assigned IsochPacketProvider* (%p) to XPCReceiverClient.processor", (void*)g_isoPacketProvider); // <<< USE THE CORRECT GLOBAL VARIABLE
        } else {
            NSLog(@"[XPCBridge] ---> WARNING: g_isoPacketProvider is NULL when delegate ran! Cannot assign processor.");
            receiverClient.processor = nullptr; // Ensure it's null
        }
        // --- END FIX ---

    } else {
         NSLog(@"[XPCBridge] Reusing existing XPCReceiverClient: %@", receiverClient);
         // Assign again, just in case it wasn't set or got invalidated
         if (!receiverClient.processor && g_isoPacketProvider) { 
             receiverClient.processor = g_isoPacketProvider; 
             NSLog(@"[XPCBridge] ---> Re-assigned IsochPacketProvider* (%p) to reused XPCReceiverClient", (void*)g_isoPacketProvider); // <<< USE THE CORRECT GLOBAL VARIABLE
         } else if (!receiverClient.processor) {
             NSLog(@"[XPCBridge] ---> Warning: Reused XPCReceiverClient has NULL processor and global pointer is also NULL!");
         }
    }

    newConnection.exportedObject = receiverClient;
    [newConnection resume];
    return YES;
}
@end

// Global delegate variable
static ClientListenerDelegate *clientListenerDelegate = nil;

// Sets up an anonymous listener and returns its endpoint.
NSXPCListenerEndpoint *InitializeClientListenerEndpoint() {
    if (!clientListener) {
        clientListener = [NSXPCListener anonymousListener];
        if (!clientListenerDelegate) {
             clientListenerDelegate = [[ClientListenerDelegate alloc] init];
        }
        clientListener.delegate = clientListenerDelegate;
        [clientListener resume];
        NSLog(@"[XPCBridge] Client listener started.");
    }
    return clientListener.endpoint;
}

// Initializes the XPC connection TO the service.
void InitializeXPCConnection() {
    if (!xpcConnection) {
        NSLog(@"[XPCBridge] Initializing XPC connection to DuetXPC service...");
        xpcConnection = [[NSXPCConnection alloc] initWithMachServiceName:@"net.mrmidi.DuetXPC"
                                                                  options:NSXPCConnectionPrivileged];
        xpcConnection.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(DuetXPCProtocol)];
        xpcConnection.invalidationHandler = ^{
            NSLog(@"[XPCBridge] XPC connection TO DuetXPC invalidated.");
            xpcConnection = nil; // Allow re-initialization
            g_isoPacketProvider = nullptr; // Clear the global pointer too if connection dies
            if (receiverClient) {
                 // Also clear the processor on the receiver if connection is lost
                 receiverClient.processor = nullptr;
            }
        };
        [xpcConnection resume];
        NSLog(@"[XPCBridge] XPC connection TO DuetXPC resumed.");
    }
}


// Initializes the XPC bridge, storing the packet provider pointer GLOBALLY.
void XPCBridgeInitialize(void* provider) { // provider is ITransmitPacketProvider*
    NSLog(@"[XPCBridge] XPCBridgeInitialize called with provider: %p", provider);

    // --- Store the provider pointer globally ---
    // It will be used later by the listener delegate and potentially by XPCBridgePushAudioData
    g_isoPacketProvider = static_cast<FWA::Isoch::ITransmitPacketProvider*>(provider); // <<< STORE IN THE SINGLE GLOBAL
    if (g_isoPacketProvider) {
         NSLog(@"[XPCBridge] Stored GLOBAL IsochPacketProvider pointer: %p", (void*)g_isoPacketProvider);

        // --- If receiverClient ALREADY exists, set its processor NOW ---
        // --- This handles cases where Initialize might be called again ---
        // --- after the delegate has already created the receiverClient ---
        if (receiverClient) {
            if (!receiverClient.processor) { // Only set if currently null
                receiverClient.processor = g_isoPacketProvider;
                NSLog(@"[XPCBridge] Updated existing receiverClient.processor = %p", (void*)g_isoPacketProvider);
            } else {
                 NSLog(@"[XPCBridge] Existing receiverClient.processor already set (%p).", (void*)receiverClient.processor);
            }
        }
        // -------------------------------------------------------------

    } else {
         NSLog(@"[XPCBridge] WARNING: Initialized with NULL IsochPacketProvider pointer.");
         // If initialized with null, ensure any existing receiver client processor is also nulled
         if (receiverClient) {
             receiverClient.processor = nullptr;
         }
    }
    // -------------------------------------------------------

    // --- Initialize connection TO service and listener FOR service ---
    InitializeXPCConnection();
    NSXPCListenerEndpoint *clientEndpoint = InitializeClientListenerEndpoint();
    // -------------------------------------------------------------

    // Register *this process's* listener endpoint with the DuetXPC service.
    if (xpcConnection && xpcConnection.remoteObjectProxy) {
        id<DuetXPCProtocol> proxy = [xpcConnection remoteObjectProxyWithErrorHandler:^(NSError * _Nonnull error) {
             NSLog(@"[XPCBridge] Error getting proxy to register client endpoint: %@", error);
        }];
        // No need for respondsToSelector check with protocol adoption
        [proxy registerClientWithEndpoint:clientEndpoint];
        NSLog(@"[XPCBridge] Client listener endpoint registration requested with DuetXPC service.");

    } else {
        NSLog(@"[XPCBridge] Warning: XPC connection to DuetXPC or proxy is nil, cannot register endpoint.");
    }
}

