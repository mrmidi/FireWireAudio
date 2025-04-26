//
//  main.m
//  FWADaemon
//
//  Created by Alexander Shabelnikov on 13.02.2025.
//

#import <Foundation/Foundation.h>
#import "FWADaemon.h"
#import "FWADaemonProtocol.h"

#pragma mark - Service Delegate

@interface FWADaemonService : NSObject <NSXPCListenerDelegate>
@end

@implementation FWADaemonService

- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)newConnection {
    NSLog(@"[FWADaemon] Accepting new XPC connection...");

    newConnection.exportedInterface = [NSXPCInterface interfaceWithProtocol:@protocol(FWADaemonProtocol)];
    // Use the shared service instance for all connections.
    newConnection.exportedObject = [FWADaemon sharedService];

    newConnection.interruptionHandler = ^{
        NSLog(@"[FWADaemon] Connection interrupted.");
    };
    newConnection.invalidationHandler = ^{
        NSLog(@"[FWADaemon] Connection invalidated.");
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

        NSXPCListener *listener = [[NSXPCListener alloc] initWithMachServiceName:@"net.mrmidi.FWADaemon"];
        if (!listener) {
            NSLog(@"[FWADaemon] ERROR: Failed to create XPC listener. Exiting.");
            return 1;
        }

        FWADaemonService *serviceDelegate = [[FWADaemonService alloc] init];
        listener.delegate = serviceDelegate;

        [listener resume];
        NSLog(@"[FWADaemon] Listener resumed. Waiting for XPC connections...");

        [[NSRunLoop currentRunLoop] run];

        NSLog(@"[FWADaemon] Unexpected exit from run loop. Terminating.");
        return 1;
    }
}