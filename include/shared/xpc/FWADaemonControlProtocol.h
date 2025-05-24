//
//  FWADaemonControlProtocol.h
//  FWADaemon
//
//  Created by Alexander Shabelnikov on 26.04.2025.
//

#import <Foundation/Foundation.h>
#import "FWAXPCCommonTypes.h"

NS_ASSUME_NONNULL_BEGIN

// Forward declaration for the client notification protocol
@protocol FWAClientNotificationProtocol;

@protocol FWADaemonControlProtocol <NSObject>

@required

// --- Engine Lifecycle & Discovery ---

/**
 * @brief Legacy method for backward compatibility with existing Swift client code.
 * Registers a client and starts discovery with an older interface.
 * @param clientID A unique identifier for this client.
 * @param clientNotificationEndpoint The XPC listener endpoint for notifications back to this client.
 * @param reply Reply block called with YES on success, NO on failure, and optional daemon info.
 */
- (void)registerClient:(NSString *)clientID
clientNotificationEndpoint:(NSXPCListenerEndpoint *)clientNotificationEndpoint
             withReply:(void (^)(BOOL success, NSDictionary * _Nullable daemonInfo))reply;

/**
 * @brief Tells the daemon to initialize its FWA components and start device discovery.
 * The daemon will use its FWAClientNotificationProtocol to send deviceAdded/deviceRemoved notifications.
 * @param clientID A unique identifier for this client.
 * @param clientNotificationEndpoint The XPC listener endpoint the daemon can use to send notifications back to this client.
 * @param reply Reply block called with YES on success, NO on failure, and an optional error.
 */
- (void)registerClientAndStartEngine:(NSString *)clientID
          clientNotificationEndpoint:(NSXPCListenerEndpoint *)clientNotificationEndpoint
                           withReply:(void (^)(BOOL success, NSError * _Nullable error))reply;

/**
 * @brief Tells the daemon to stop device discovery and release FWA engine resources.
 * This might not immediately stop active audio streams if other clients are using them,
 * but it stops this client from receiving further discovery notifications.
 * @param clientID The client ID to unregister.
 * @param reply Reply block.
 */
- (void)unregisterClientAndStopEngine:(NSString *)clientID
                            withReply:(void (^)(BOOL success, NSError * _Nullable error))reply;


// --- Device Information ---

/**
 * @brief Requests a list of summaries for currently connected and known devices.
 * A device summary might include GUID, device name, vendor name.
 * @param reply Reply block with an array of dictionaries (device summaries) or an error.
 */
- (void)getConnectedDeviceSummariesWithReply:(void (^)(NSArray<NSDictionary *> * _Nullable deviceSummaries, NSError * _Nullable error))reply;

/**
 * @brief Requests detailed information for a specific device, serialized as a JSON string.
 * The daemon will perform necessary FWA parsing if info isn't cached.
 * @param guid The GUID of the device.
 * @param reply Reply block with the device info JSON string or an error.
 */
- (void)getDetailedDeviceInfoJSONForGUID:(uint64_t)guid
                               withReply:(void (^)(NSString * _Nullable deviceInfoJSON, NSError * _Nullable error))reply;


// --- AV/C Commands ---

/**
 * @brief Sends a raw AV/C command to a specific device.
 * @param guid The GUID of the target device.
 * @param commandData The NSData object containing the AV/C command bytes.
 * @param reply Reply block with the NSData response from the device or an error.
 */
- (void)sendAVCCommandToDevice:(uint64_t)guid
                       command:(NSData *)commandData
                     withReply:(void (^)(NSData * _Nullable responseData, NSError * _Nullable error))reply;


// --- Isochronous Stream Control ---

/**
 * @brief Requests the daemon to start isochronous audio streams for a specific device.
 * @param guid The GUID of the device.
 * @param reply Reply block indicating success/failure and an optional error.
 */
- (void)startAudioStreamsForDevice:(uint64_t)guid
                         withReply:(void (^)(BOOL success, NSError * _Nullable error))reply;

/**
 * @brief Requests the daemon to stop isochronous audio streams for a specific device.
 * @param guid The GUID of the device.
 * @param reply Reply block indicating success/failure and an optional error.
 */
- (void)stopAudioStreamsForDevice:(uint64_t)guid
                        withReply:(void (^)(BOOL success, NSError * _Nullable error))reply;


// --- Logging Control ---

/**
 * @brief Sets the logging level for the FWA/Isoch components running within the daemon.
 * @param level The desired FWAXPCLoglevel.
 * @param reply Reply block indicating success.
 */
- (void)setDaemonLogLevel:(FWAXPCLoglevel)level
                withReply:(void (^)(BOOL success))reply;

/**
 * @brief Gets the current logging level of the FWA/Isoch components within the daemon.
 * @param reply Reply block with the current FWAXPCLoglevel.
 */
- (void)getDaemonLogLevelWithReply:(void (^)(FWAXPCLoglevel currentLevel))reply;


// --- Driver Interaction (from GUI to Daemon, then Daemon talks to Driver via its own XPC if needed) ---

/**
 * @brief Informs the daemon about the overall presence/absence of the driver.
 * This would typically be called by the driver itself during its init/finalize.
 * GUI might call this if it's managing a manual driver load/unload simulation or for testing.
 * @param isPresent YES if the driver is loaded and ready, NO otherwise.
 */
- (void)setDriverPresenceStatus:(BOOL)isPresent; // Fire-and-forget from client

/**
 * @brief Requests the current driver presence status known by the daemon.
 * @param reply Block called with YES if the daemon believes the driver is present.
 */
- (void)getIsDriverConnectedWithReply:(void (^)(BOOL isConnected))reply;


// --- Shared Memory (primarily for the Driver client) ---

/**
 * @brief Requests the name of the shared memory segment the daemon has set up for audio data.
 * Primarily for the ASPL driver to connect to the correct SHM.
 * @param reply Reply block with the shared memory name (e.g., "/fwa_daemon_shm_v1") or nil if not available.
 */
- (void)getSharedMemoryNameWithReply:(void (^)(NSString * _Nullable shmName))reply;


// --- Legacy Methods for Swift Client Compatibility ---

/**
 * @brief Legacy method: Get list of connected device GUIDs.
 * @param reply Reply block with array of NSNumber objects containing UInt64 GUIDs.
 */
- (void)getConnectedDeviceGUIDsWithReply:(void (^)(NSArray<NSNumber *> * _Nullable guids))reply;

/**
 * @brief Legacy method: Get device connection status.
 * @param guid The GUID of the device.
 * @param reply Reply block with status info dictionary.
 */
- (void)getDeviceConnectionStatus:(uint64_t)guid withReply:(void (^)(NSDictionary * _Nullable statusInfo))reply;

/**
 * @brief Legacy method: Get device configuration.
 * @param guid The GUID of the device.
 * @param reply Reply block with config info dictionary.
 */
- (void)getDeviceConfiguration:(uint64_t)guid withReply:(void (^)(NSDictionary * _Nullable configInfo))reply;

/**
 * @brief Legacy method: Request to set nominal sample rate.
 * @param guid The GUID of the device.
 * @param rate The sample rate to set.
 * @param reply Reply block with success status.
 */
- (void)requestSetNominalSampleRate:(uint64_t)guid rate:(double)rate withReply:(void (^)(BOOL success))reply;

/**
 * @brief Legacy method: Request to start IO.
 * @param guid The GUID of the device.
 * @param reply Reply block with success status.
 */
- (void)requestStartIO:(uint64_t)guid withReply:(void (^)(BOOL success))reply;

/**
 * @brief Legacy method: Request to stop IO (fire-and-forget).
 * @param guid The GUID of the device.
 */
- (void)requestStopIO:(uint64_t)guid;

@end

NS_ASSUME_NONNULL_END
