//
//  DuetXPCProtocol.h
//  avc-apple2
//
//  Created by Alexander Shabelnikov on 13.02.2025.
//

#ifndef DuetXPCProtocol_h
#define DuetXPCProtocol_h

#import <Foundation/Foundation.h>
#import "MixedAudioBuffer.h"
#import "DuetXPCClientProtocol.h" // Ensure this is imported

@protocol DuetXPCProtocol

// Sends an audio buffer (stream data) to the service.
- (void)sendAudioBuffer:(MixedAudioBuffer *)buffer withReply:(void (^)(BOOL success))reply;

// Returns a placeholder stream format.
- (void)getStreamFormatWithReply:(void (^)(NSString *format))reply;

// A handshake method to verify that the connection is active.
- (void)handshakeWithReply:(void (^)(BOOL success))reply;

// *** NEW: Register a client to receive audio buffers.
- (void)registerClientWithEndpoint:(NSXPCListenerEndpoint *)clientEndpoint;

@end

#endif /* DuetXPCProtocol_h */
