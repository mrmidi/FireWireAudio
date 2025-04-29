#ifndef FWA_CAPI_H
#define FWA_CAPI_H

#include <stddef.h> // For size_t
#include <stdint.h> // For uint64_t, uint8_t
#include <stdbool.h> // For bool
#include <mach/kern_return.h>
#include <IOKit/IOReturn.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Opaque Types ---
typedef struct FWAEngine* FWAEngineRef;
typedef struct FWADevice* FWADeviceRef;

// --- Enums ---
// Explicitly set underlying type to match Swift's Int32 expectation
typedef enum : int32_t {
    FWA_LOG_LEVEL_TRACE = 0,
    FWA_LOG_LEVEL_DEBUG = 1,
    FWA_LOG_LEVEL_INFO = 2,
    FWA_LOG_LEVEL_WARN = 3,
    FWA_LOG_LEVEL_ERROR = 4,
    FWA_LOG_LEVEL_CRITICAL = 5,
    FWA_LOG_LEVEL_OFF = 6
} FWALogLevel;

// --- Callback Function Pointers (Context First) ---
typedef void (*FWALogCallback)(void* user_data, FWALogLevel level, const char* message);
typedef void (*FWADeviceNotificationCallback)(void* user_data, FWADeviceRef device, bool connected);

// --- Engine Management Functions ---
FWAEngineRef FWAEngine_Create();
void FWAEngine_Destroy(FWAEngineRef engine);
IOReturn FWAEngine_SetLogCallback(FWAEngineRef engine, FWALogCallback callback, void* user_data);
IOReturn FWAEngine_Start(FWAEngineRef engine, FWADeviceNotificationCallback notification_callback, void* user_data);
IOReturn FWAEngine_Stop(FWAEngineRef engine);

// --- Device Interaction Functions ---
IOReturn FWADevice_GetGUID(FWADeviceRef device, uint64_t* out_guid);
char* FWADevice_GetDeviceName(FWADeviceRef device);
char* FWADevice_GetVendorName(FWADeviceRef device);
void FWADevice_FreeString(char* str);
void FWADevice_FreeResponseBuffer(uint8_t* resp_data);

// --- ADD MISSING ENGINE-BASED FUNCTIONS ---
/**
 * @brief Gets detailed device information as a JSON string, using the engine and GUID.
 */
char* FWAEngine_GetInfoJSON(FWAEngineRef engine, uint64_t guid);

/**
 * @brief Sends a custom AV/C command to a specific device, identified by GUID.
 */
IOReturn FWAEngine_SendCommand(FWAEngineRef engine,
                               uint64_t guid,
                               const uint8_t* cmd_data,
                               size_t cmd_len,
                               uint8_t** out_resp_data,
                               size_t* out_resp_len);

IOReturn FWAEngine_SetLogLevel(FWAEngineRef engine, FWALogLevel level);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // FWA_CAPI_H
