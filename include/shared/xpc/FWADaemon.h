// src/xpc/FWADaemon/FWADaemon.h
#ifndef FWADaemon_h
#define FWADaemon_h

#import <Foundation/Foundation.h>
#import "shared/xpc/FWADaemonControlProtocol.h" // Include the protocol it implements

// Declare the class that implements the protocol
@interface FWADaemon : NSObject <FWADaemonControlProtocol>

// Declare the singleton accessor
+ (instancetype)sharedService;

// Declare any other methods the main.m might *theoretically* need,
// though usually just the singleton is enough.

@end

#endif /* FWADaemon_h */