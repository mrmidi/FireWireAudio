//
//  main.m
//  FWADaemon
//
//  Created by Alexander Shabelnikov on 13.02.2025.
//

#import <Foundation/Foundation.h>
#import "shared/xpc/FWADaemonControlProtocol.h" // Correct protocol header
#import "FWADaemon.h"                           // Correct implementation header

#pragma mark - Service Delegate

@interface FWADaemonService : NSObject <NSXPCListenerDelegate>
@end

@implementation FWADaemonService

- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)newConnection {
    NSLog(@"[FWADaemon] Accepting new XPC connection...");

    // Use the CORRECT protocol name
    newConnection.exportedInterface = [NSXPCInterface interfaceWithProtocol:@protocol(FWADaemonControlProtocol)];
    // Use the shared service instance for all connections (Class name is FWADaemon)
    newConnection.exportedObject = [FWADaemon sharedService]; // Correct class name

    newConnection.interruptionHandler = ^{
        // Note: Accessing sharedService here might be tricky if the connection
        // interruption means the service is tearing down. Usually just log.
        NSLog(@"[FWADaemon] Connection interrupted.");
        // Potentially notify the sharedService singleton about the disconnection
        // [[FWADaemon sharedService] clientConnectionInterrupted:newConnection];
    };
    newConnection.invalidationHandler = ^{
        NSLog(@"[FWADaemon] Connection invalidated.");
        // Potentially notify the sharedService singleton about the disconnection
        // [[FWADaemon sharedService] clientConnectionInvalidated:newConnection];
    };

    [newConnection resume];
    NSLog(@"[FWADaemon] XPC connection established.");

    return YES;
}

@end

#pragma mark - Main

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSLog(@"[FWADaemon] Starting XPC service listener...");

        // Use the correct service name string
        NSXPCListener *listener = [[NSXPCListener alloc] initWithMachServiceName:@"net.mrmidi.FWADaemon"];
        if (!listener) {
            NSLog(@"[FWADaemon] ERROR: Failed to create XPC listener for net.mrmidi.FWADaemon. Exiting.");
            return 1;
        }

        FWADaemonService *serviceDelegate = [[FWADaemonService alloc] init];
        listener.delegate = serviceDelegate;

        [listener resume];
        NSLog(@"[FWADaemon] Listener resumed. Waiting for XPC connections on net.mrmidi.FWADaemon...");

        // Initialize the singleton early? Optional, but can be good practice.
        [FWADaemon sharedService];
        NSLog(@"[FWADaemon] FWADaemon singleton initialized.");

        [[NSRunLoop currentRunLoop] run];

        NSLog(@"[FWADaemon] Unexpected exit from run loop. Terminating.");
        // Perform any necessary cleanup before exiting if possible
        return 1;
    }
}
