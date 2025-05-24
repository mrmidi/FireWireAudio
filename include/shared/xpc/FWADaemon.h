// FWADaemon.h (Objective-C++ version)
#ifndef FWADaemon_h
#define FWADaemon_h

#import <Foundation/Foundation.h>
#import "shared/xpc/FWADaemonControlProtocol.h" // Protocol it implements

// --- C++ Forward Declaration for PImpl-like pattern if strictly needed ---
// If you want to keep FWADaemon.h pure Objective-C, you'd use a void*
// and an opaque struct forward declaration.
// However, for std::unique_ptr, it's common to make the .h ObjC++.
#ifdef __cplusplus
#include <memory> // For std::unique_ptr
namespace FWA { class DaemonCore; } // Forward declare C++ class
#endif

@interface FWADaemon : NSObject <FWADaemonControlProtocol>
{
#ifdef __cplusplus
    std::unique_ptr<FWA::DaemonCore> _cppCore;
#else
    // If FWADaemon.h must be pure Obj-C (more complex bridging)
    // void* _cppCore_opaque;
#endif
}

+ (instancetype)sharedService;

// Methods for GuiCallbackSink (already present, keep them)
- (BOOL)hasActiveGuiClients;
- (void)forwardLogMessageToClients:(NSString *)senderID level:(int32_t)level message:(NSString *)message;

@end

#endif /* FWADaemon_h */