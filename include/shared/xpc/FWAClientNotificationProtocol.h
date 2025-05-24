//
//  FWAClientNotificationProtocol.h
//  FWADaemon
//
//  Created by Alexander Shabelnikov on 26.04.2025.
//

#import <Foundation/Foundation.h>
#import "FWAXPCCommonTypes.h"

NS_ASSUME_NONNULL_BEGIN

@protocol FWAClientNotificationProtocol <NSObject>

@required

// --- Daemon Handshake ---
/**
 * @brief Called by the daemon after client registration to confirm the callback channel is working.
 * @param reply The client should call this reply block with YES.
 */
- (void)daemonHandshake:(void(^)(BOOL clientAcknowledged))reply;


// --- Device Discovery Notifications ---

/**
 * @brief Notifies the client that a new FireWire audio device has been discovered and initialized.
 * @param deviceSummary A dictionary containing basic information (GUID, name, vendor).
 *                      More detailed info can be requested via getDetailedDeviceInfoJSONForGUID.
 */
- (void)deviceAdded:(NSDictionary<NSString *, id> *)deviceSummary;

/**
 * @brief Notifies the client that a previously discovered device has been removed.
 * @param guid The GUID of the removed device.
 */
- (void)deviceRemoved:(uint64_t)guid;

/**
 * @brief Notifies the client that detailed information or status for a device has been updated.
 * (Optional, if daemon proactively sends updates after initial discovery).
 * @param guid The GUID of the updated device.
 * @param updatedInfoJSON A new JSON string containing the updated detailed device information.
 */
- (void)deviceInfoUpdated:(uint64_t)guid newInfoJSON:(NSString *)updatedInfoJSON;


// --- Isochronous Stream Status Notifications ---

/**
 * @brief Notifies the client about a change in the streaming status for a device.
 * @param guid The GUID of the device whose stream status changed.
 * @param isStreaming YES if streams are now active, NO if they stopped.
 * @param error An optional NSError object if the status change was due to an error.
 */
- (void)streamStatusChangedForDevice:(uint64_t)guid
                         isStreaming:(BOOL)isStreaming
                               error:(nullable NSError *)error;


// --- Logging Notifications ---

/**
 * @brief Forwards a log message originating from within the daemon (FWA/Isoch C++ libraries).
 * @param senderID A string indicating the source of the log within the daemon (e.g., "FWA::DeviceParser", "Isoch::AmdtpReceiver").
 * @param level The log level as Int32.
 * @param message The log message string.
 */
- (void)didReceiveLogMessage:(NSString *)senderID
                       level:(int32_t)level
                     message:(NSString *)message;

/**
 * @brief Legacy method: Forwards a log message with FWAXPCLoglevel for daemon internal use.
 * @param message The log message string.
 * @param level The FWAXPCLoglevel of the message.
 * @param senderID A string indicating the source of the log within the daemon.
 */
- (void)didReceiveLogMessageFrom:(NSString *)senderID
                           level:(FWAXPCLoglevel)level
                         message:(NSString *)message;


// --- Legacy Device Notification Methods for Swift Compatibility ---

/**
 * @brief Legacy method: Notifies about device connection status changes.
 * @param guid The GUID of the device.
 * @param isConnected Whether the device is connected.
 * @param isInitialized Whether the device is initialized.
 * @param deviceName The device name.
 * @param vendorName The vendor name.
 */
- (void)daemonDidUpdateDeviceConnectionStatus:(uint64_t)guid
                                 isConnected:(BOOL)isConnected
                               isInitialized:(BOOL)isInitialized
                                  deviceName:(NSString * _Nullable)deviceName
                                  vendorName:(NSString * _Nullable)vendorName;

/**
 * @brief Legacy method: Notifies about device configuration changes.
 * @param guid The GUID of the device.
 * @param configInfo The configuration information dictionary.
 */
- (void)daemonDidUpdateDeviceConfiguration:(uint64_t)guid
                                configInfo:(NSDictionary<id, id> *)configInfo;

// --- Driver Status Notification ---
/**
 * @brief Notifies the client (typically GUI) that the main audio driver's overall connection status to the daemon has changed.
 * This is distinct from individual device connections.
 * @param isConnected YES if the driver is now considered connected/present by the daemon, NO otherwise.
 */
- (void)driverConnectionStatusDidChange:(BOOL)isConnected;


// --- Control Callbacks (Daemon forwarding requests from one client to another, e.g., Driver -> Daemon -> GUI) ---
// These methods mirror the requests in FWADaemonControlProtocol that might be initiated by one client (e.g., driver)
// and require action/confirmation from another (e.g., GUI, which then talks to hardware via FWA C++ lib within daemon).
// The GUI client would implement these, perform the action by calling ITS OWN XPC methods on the daemon
// (which then call the C++ core), and then call the reply block.
//
// **NOTE:** This creates a potential round-trip: Driver -> Daemon -> GUI -> Daemon -> Hardware.
// If the daemon's C++ core can handle these directly, these specific forwarding methods might be simplified or removed,
// and the daemon would reply directly to the initiating client.
// For now, including them to match the pattern of daemon as a message broker.

/**
 * @brief Daemon forwards a request (e.g., from the driver) to the client (e.g., GUI) to set a device's nominal sample rate.
 * The client should attempt the operation (likely by making another XPC call back to the daemon
 * which then uses its internal C++ FWA library) and then call the reply block.
 */
- (void)performSetNominalSampleRate:(uint64_t)guid
                               rate:(double)rate
                          withReply:(void (^)(BOOL success))reply;

- (void)performSetClockSource:(uint64_t)guid
                clockSourceID:(uint32_t)clockSourceID
                    withReply:(void (^)(BOOL success))reply;

- (void)performSetMasterVolumeScalar:(uint64_t)guid
                               scope:(uint32_t)scope
                             element:(uint32_t)element
                         scalarValue:(float)scalarValue
                           withReply:(void (^)(BOOL success))reply;

- (void)performSetMasterMute:(uint64_t)guid
                       scope:(uint32_t)scope
                     element:(uint32_t)element
                   muteState:(BOOL)muteState
                   withReply:(void (^)(BOOL success))reply;

/**
 * @brief Daemon forwards a request to the client to start I/O for a device.
 */
- (void)performStartIO:(uint64_t)guid
             withReply:(void (^)(BOOL success))reply;

/**
 * @brief Daemon forwards a request to the client to stop I/O for a device (fire-and-forget).
 */
- (void)performStopIO:(uint64_t)guid;

@end

NS_ASSUME_NONNULL_END