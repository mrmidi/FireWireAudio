//
//  main.m
//  DuetXPC
//
//  Created by Alexander Shabelnikov on 13.02.2025.
//

#import <Foundation/Foundation.h>
#import "DuetXPC.h"
#import "DuetXPCProtocol.h"

#pragma mark - Service Delegate

@interface DuetXPCService : NSObject <NSXPCListenerDelegate>
@end

@implementation DuetXPCService

- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)newConnection {
    NSLog(@"[DuetXPC] Accepting new XPC connection...");

    newConnection.exportedInterface = [NSXPCInterface interfaceWithProtocol:@protocol(DuetXPCProtocol)];
    // Use the shared service instance for all connections.
    newConnection.exportedObject = [DuetXPC sharedService];

    newConnection.interruptionHandler = ^{
        NSLog(@"[DuetXPC] Connection interrupted.");
    };
    newConnection.invalidationHandler = ^{
        NSLog(@"[DuetXPC] Connection invalidated.");
    };

    [newConnection resume];
    NSLog(@"[DuetXPC] XPC connection established.");

    return YES;
}

@end

#pragma mark - Main

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSLog(@"[DuetXPC] Starting XPC service listener...");

        NSXPCListener *listener = [[NSXPCListener alloc] initWithMachServiceName:@"net.mrmidi.DuetXPC"];
        if (!listener) {
            NSLog(@"[DuetXPC] ERROR: Failed to create XPC listener. Exiting.");
            return 1;
        }

        DuetXPCService *serviceDelegate = [[DuetXPCService alloc] init];
        listener.delegate = serviceDelegate;

        [listener resume];
        NSLog(@"[DuetXPC] Listener resumed. Waiting for XPC connections...");

        [[NSRunLoop currentRunLoop] run];

        NSLog(@"[DuetXPC] Unexpected exit from run loop. Terminating.");
        return 1;
    }
}