#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <CoreAudio/AudioServerPlugIn.h>

#ifdef __cplusplus
extern "C" {
#endif

// XPC Command constants (example)
enum {
    kXPCCommand_StartStream = 1,
    kXPCCommand_StopStream = 2,
};

// Handshake with the daemon, returns true on success
bool FWADriver_HandshakeWithDaemon(void);

// Send a command to the daemon (start/stop stream, etc.)
bool FWADriver_SendCommand(int command);

// Notify the daemon that new data is available in shared memory
void FWADriver_NotifyDataAvailable(void);

// Query the daemon for the current zero timestamp
OSStatus FWADriver_QueryZeroTimestamp(uint64_t* outHostTime, double* outSampleTime, uint64_t* outSeed);

#ifdef __cplusplus
}
#endif
