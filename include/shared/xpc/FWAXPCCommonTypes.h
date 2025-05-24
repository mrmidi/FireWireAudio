//
//  FWAXPCCommonTypes.h
//  FWADaemon
//
//  Created by Alexander Shabelnikov on 24.05.2025.
//

#ifndef FWA_XPC_COMMON_TYPES_H
#define FWA_XPC_COMMON_TYPES_H

#import <Foundation/Foundation.h>

// Define an enum for log levels that matches the FWALogLevel enum values from fwa_capi.h
typedef NS_ENUM(NSInteger, FWAXPCLoglevel) {
    FWAXPCLoglevelTrace = 0,
    FWAXPCLoglevelDebug = 1,
    FWAXPCLoglevelInfo = 2,
    FWAXPCLoglevelWarn = 3,
    FWAXPCLoglevelError = 4,
    FWAXPCLoglevelCritical = 5,
    FWAXPCLoglevelOff = 6
};

// Add extern NSString * const FWADaemonErrorDomain; here too later

#endif /* FWA_XPC_COMMON_TYPES_H */
