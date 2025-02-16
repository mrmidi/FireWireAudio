//
//  DuetXPCProtocol.h
//  DuetXPC
//
//  Created by Alexander Shabelnikov on 13.02.2025.
//

#ifndef DuetXPCProtocol_h
#define DuetXPCProtocol_h

#import <Foundation/Foundation.h>
#import "MixedAudioBuffer.h"
#import "DuetXPCClientProtocol.h"

@protocol DuetXPCProtocol

// Sends an audio buffer from the driver.
- (void)sendAudioBuffer:(MixedAudioBuffer *)buffer withReply:(void (^)(BOOL success))reply;

// Returns the stream format (placeholder).
- (void)getStreamFormatWithReply:(void (^)(NSString *format))reply;

// Handshake method for verifying connection.
- (void)handshakeWithReply:(void (^)(BOOL success))reply;

// New method: register the client using its listener endpoint.
- (void)registerClientWithEndpoint:(NSXPCListenerEndpoint *)clientEndpoint;


@end

#endif /* DuetXPCProtocol_h */
