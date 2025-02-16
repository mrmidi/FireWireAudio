//
//  XPCBridge.mm
//  avc-apple2
//
//  Created by Alexander Shabelnikov on 14.02.2025.
//

// XPCBridge.mm

#import "FWA/XPC/XPCBridge.h"
#import <Foundation/Foundation.h>
#import "FWA/XPC/DuetXPCProtocol.h"
#import "FWA/XPC/XPCReceiverClient.hpp"
#import "FWA/XPC/MixedAudioBuffer.h" // Import MixedAudioBuffer definition

// --- Include the necessary C++ header ---
#include "Isoch/interfaces/ITransmitPacketProvider.hpp"
#include <chrono>

// Global variable for our XPC connection to the service.
static NSXPCConnection *xpcConnection = nil;

// Global variables for the client listener (for callbacks FROM the service TO the app).
static NSXPCListener *clientListener = nil;
static XPCReceiverClient *receiverClient = nil; // Handles callbacks *to* the app

// --- MOVED Delegate Definition UP ---
// Delegate for the anonymous client listener.
@interface ClientListenerDelegate : NSObject <NSXPCListenerDelegate>
@end

@implementation ClientListenerDelegate
- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)newConnection {
    NSLog(@"[XPCBridge] Accepting new connection for client callbacks...");

    NSXPCInterface *clientInterface = [NSXPCInterface interfaceWithProtocol:@protocol(DuetXPCClientProtocol)];

    // Allow MixedAudioBuffer for the receive buffer method (callback TO the app)
    [clientInterface setClasses:[NSSet setWithObject:[MixedAudioBuffer class]]
                    forSelector:@selector(didReceiveAudioBuffer:)
                  argumentIndex:0
                        ofReply:NO];

    newConnection.exportedInterface = clientInterface;

    // --- Initialize or reuse the XPCReceiverClient ---
    // This object handles methods called *by* the daemon *on* the client app.
    if (!receiverClient) {
         // --- Use default init ---
        receiverClient = [[XPCReceiverClient alloc] init]; // Use default init
        NSLog(@"[XPCBridge] XPCReceiverClient newly initialized: %@", receiverClient);
    }
    // ----------------------------------------------------

    newConnection.exportedObject = receiverClient;
    [newConnection resume];
    return YES;
}
@end
// --- END Delegate Definition Move ---


// --- Global delegate variable declared AFTER the definition ---
static ClientListenerDelegate *clientListenerDelegate = nil; // Delegate for the listener
// ------------------------------------------------------------


// Global variable to store the Packet Provider pointer (for transmitting TO the daemon)
static FWA::Isoch::ITransmitPacketProvider* g_packetProvider = nullptr;


// Sets up an anonymous listener and returns its endpoint.
NSXPCListenerEndpoint *InitializeClientListenerEndpoint() {
    if (!clientListener) {
        clientListener = [NSXPCListener anonymousListener];
        // Create delegate only once
        if (!clientListenerDelegate) {
             clientListenerDelegate = [[ClientListenerDelegate alloc] init];
        }
        clientListener.delegate = clientListenerDelegate;
        [clientListener resume];
        NSLog(@"[XPCBridge] Client listener started.");
    }
    return clientListener.endpoint;
}

// Initializes the XPC connection.
void InitializeXPCConnection() {
    if (!xpcConnection) {
        NSLog(@"[XPCBridge] Initializing XPC connection...");
        xpcConnection = [[NSXPCConnection alloc] initWithMachServiceName:@"net.mrmidi.DuetXPC"
                                                                  options:NSXPCConnectionPrivileged];
        xpcConnection.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(DuetXPCProtocol)];
        xpcConnection.invalidationHandler = ^{
            NSLog(@"[XPCBridge] XPC connection invalidated.");
            xpcConnection = nil; // Allow re-initialization
            g_packetProvider = nullptr; // Clear provider pointer on invalidation
        };
        [xpcConnection resume];
        NSLog(@"[XPCBridge] XPC connection resumed.");
    }
}

// --- UPDATED: Public interface ---
// Initializes the XPC bridge, storing the packet provider pointer.
void XPCBridgeInitialize(void* provider) { // Renamed arg for clarity
    NSLog(@"[XPCBridge] XPCBridgeInitialize called with provider: %p", provider);

    // Ensure the XPC connection to the service is set up.
    InitializeXPCConnection();

    // Set up the client listener endpoint (for callbacks *from* the service).
    NSXPCListenerEndpoint *clientEndpoint = InitializeClientListenerEndpoint();

    // --- Store the ITransmitPacketProvider pointer globally ---
    // Cast the void* to the correct C++ type
    g_packetProvider = static_cast<FWA::Isoch::ITransmitPacketProvider*>(provider);
    if (g_packetProvider) {
         NSLog(@"[XPCBridge] Stored ITransmitPacketProvider pointer: %p", (void*)g_packetProvider);
    } else {
         NSLog(@"[XPCBridge] WARNING: Initialized with NULL ITransmitPacketProvider pointer.");
    }
    // -------------------------------------------------------

    // --- Remove XPCReceiverClient initialization/update from here ---
    // This was mixing transmitter setup with receiver callback handling.
    // The XPCReceiverClient instance used by the listener delegate
    // is managed within the delegate callback itself.
    // ------------------------------------------------------------

    // Register the client's listener endpoint with the XPC service.
    if (xpcConnection && xpcConnection.remoteObjectProxy) {
        // Use ARC's inference or explicitly cast if needed for respondsToSelector
        id<DuetXPCProtocol> proxy = [xpcConnection remoteObjectProxy];
        // The respondsToSelector warning should be resolved if proxy type is known.
        // If it persists, you might need @try/@catch or cast `proxy` to `NSObject*`
        // for this check, but that's usually unnecessary.
        if ([proxy respondsToSelector:@selector(registerClientWithEndpoint:)]) {
            [proxy registerClientWithEndpoint:clientEndpoint];
            NSLog(@"[XPCBridge] Client listener endpoint registration requested with service.");
        } else {
            NSLog(@"[XPCBridge] Warning: XPC proxy does not respond to registerClientWithEndpoint:");
        }
    } else {
        NSLog(@"[XPCBridge] Warning: XPC connection or proxy is nil, cannot register client endpoint.");
    }
}

// --- NEW C Function to be called by XPC handler when audio data arrives ---
// --- This function pushes data TO the daemon's transmitter ---
extern "C" bool XPCBridgePushAudioData(const void* buffer, size_t bufferSizeInBytes) {
    if (!g_packetProvider) {
        // Log periodically if needed
        static auto lastLogTime = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
         if (now - lastLogTime > std::chrono::seconds(5)) {
             NSLog(@"[XPCBridge] Error: XPCBridgePushAudioData called but g_packetProvider is NULL.");
             lastLogTime = now;
         }
        return false;
    }

    // Call the pushAudioData method on the stored provider instance
    return g_packetProvider->pushAudioData(buffer, bufferSizeInBytes);
}
// -------------------------------------------------------------------------
