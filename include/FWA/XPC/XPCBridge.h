//
//  XPCBridge.h
//  avc-apple2
//
//  Created by Alexander Shabelnikov on 14.02.2025.
//

#ifndef XPCBRIDGE_H
#define XPCBRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

// Initializes the XPC bridge with a pointer to your AMDTP transmit processor.
// The processor pointer is passed as a void* and internally cast to the correct type.
void XPCBridgeInitialize(void* processor);

// TODO: expand to handle receiver, commands

#ifdef __cplusplus
}
#endif

#endif // XPCBRIDGE_H
