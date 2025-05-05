//
//  FWADaemonControlProtocol.h
//  FWADaemon
//
//  Created by Alexander Shabelnikov on 26.04.2025.
//

#import <Foundation/Foundation.h>

// Forward declaration
@protocol FWAClientNotificationProtocol;

NS_ASSUME_NONNULL_BEGIN

// Interface exported BY the DAEMON SERVICE (net.mrmidi.FWADaemon)
@protocol FWADaemonControlProtocol <NSObject>

// --- Registration & Lifecycle ---
- (void)registerClient:(NSString *)clientID
clientNotificationEndpoint:(NSXPCListenerEndpoint *)clientNotificationEndpoint
           withReply:(void (^)(BOOL success, NSDictionary * _Nullable daemonInfo))reply;
- (void)unregisterClient:(NSString *)clientID;

// --- Status & Config (GUI -> Daemon -> Driver) ---
- (void)updateDeviceConnectionStatus:(uint64_t)guid
                         isConnected:(BOOL)isConnected
                       isInitialized:(BOOL)isInitialized
                          deviceName:(NSString *)deviceName
                          vendorName:(NSString *)vendorName;
- (void)updateDeviceConfiguration:(uint64_t)guid configInfo:(NSDictionary *)configInfo;
- (void)getDeviceConnectionStatus:(uint64_t)guid withReply:(void (^)(NSDictionary * _Nullable statusInfo))reply;
- (void)getDeviceConfiguration:(uint64_t)guid withReply:(void (^)(NSDictionary * _Nullable configInfo))reply;
- (void)getConnectedDeviceGUIDsWithReply:(void (^)(NSArray<NSNumber *> * _Nullable guids))reply;

// --- Control Commands (Driver -> Daemon -> GUI) ---
- (void)requestSetNominalSampleRate:(uint64_t)guid rate:(double)rate withReply:(void (^)(BOOL success))reply;
- (void)requestSetClockSource:(uint64_t)guid clockSourceID:(uint32_t)clockSourceID withReply:(void (^)(BOOL success))reply;
- (void)requestSetMasterVolumeScalar:(uint64_t)guid scope:(uint32_t)scope element:(uint32_t)element scalarValue:(float)scalarValue withReply:(void (^)(BOOL success))reply;
- (void)requestSetMasterMute:(uint64_t)guid scope:(uint32_t)scope element:(uint32_t)element muteState:(BOOL)muteState withReply:(void (^)(BOOL success))reply;

// --- IO State (Driver -> Daemon -> GUI) ---
- (void)requestStartIO:(uint64_t)guid withReply:(void (^)(BOOL success))reply;
- (void)requestStopIO:(uint64_t)guid;

// --- Logging (Driver -> Daemon -> GUI) ---
- (void)forwardLogMessageFromDriver:(int32_t)level message:(NSString *)message;

/**
 * @brief [Driver -> Daemon] Informs the daemon about the overall presence/absence of the driver.
 * @param isPresent YES if the driver is loaded, initialized, and ready to interact, NO otherwise.
 */
- (void)setDriverPresenceStatus:(BOOL)isPresent;

/**
 * @brief [GUI -> Daemon] Requests the current driver presence status.
 * @param reply Block called with YES if the driver is considered present, NO otherwise.
 */
- (void)getIsDriverConnectedWithReply:(void (^)(BOOL isConnected))reply;



// --- Shared Memory (Driver -> Daemon) ---
- (void)getSharedMemoryNameWithReply:(void (^)(NSString * _Nullable shmName))reply;

@end

NS_ASSUME_NONNULL_END