//
//  FWAClientNotificationProtocol.h
//  FWADaemon
//
//  Created by Alexander Shabelnikov on 26.04.2025.
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// Protocol defining callbacks the DAEMON makes TO registered CLIENTS (GUI, Driver)
@protocol FWAClientNotificationProtocol <NSObject>

@required // Methods clients should generally implement
- (void)daemonHandshake:(void(^)(BOOL ok))reply;
- (void)daemonDidUpdateDeviceConnectionStatus:(uint64_t)guid
                                isConnected:(BOOL)isConnected
                              isInitialized:(BOOL)isInitialized
                                 deviceName:(nullable NSString *)deviceName
                                 vendorName:(nullable NSString *)vendorName;

- (void)daemonDidUpdateDeviceConfiguration:(uint64_t)guid configInfo:(NSDictionary *)configInfo;

- (void)didReceiveLogMessageFrom:(NSString *)senderID level:(int32_t)level message:(NSString *)message;

// --- Control Requests FORWARDED to GUI Client ---
- (void)performSetNominalSampleRate:(uint64_t)guid rate:(double)rate withReply:(void (^)(BOOL success))reply;
- (void)performSetClockSource:(uint64_t)guid clockSourceID:(uint32_t)clockSourceID withReply:(void (^)(BOOL success))reply;
- (void)performSetMasterVolumeScalar:(uint64_t)guid scope:(uint32_t)scope element:(uint32_t)element scalarValue:(float)scalarValue withReply:(void (^)(BOOL success))reply;
- (void)performSetMasterMute:(uint64_t)guid scope:(uint32_t)scope element:(uint32_t)element muteState:(BOOL)muteState withReply:(void (^)(BOOL success))reply;
- (void)performStartIO:(uint64_t)guid withReply:(void (^)(BOOL success))reply;
- (void)performStopIO:(uint64_t)guid;

/**
 * @brief [Daemon -> GUI] Notifies the client that the driver's overall connection status has changed.
 * @param isConnected YES if the driver is now considered connected/present, NO otherwise.
 */
- (void)driverConnectionStatusDidChange:(BOOL)isConnected;

@optional // Optional notifications clients might care about

- (void)clientDidChangeConnectionStatus:(NSString *)clientID isConnected:(BOOL)isConnected;
- (void)daemonDidEncounterIssue:(NSString *)issueDetails isError:(BOOL)isError;

@end

NS_ASSUME_NONNULL_END