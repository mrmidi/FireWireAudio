//
//  XPCReceiverClientProtocol.h
//  DuetXPC
//
//  Created by Alexander Shabelnikov on 14.02.2025.
//

#ifndef DuetXPCClientProtocol_h
#define DuetXPCClientProtocol_h

#import <Foundation/Foundation.h>
#import "MixedAudioBuffer.h"

@protocol DuetXPCClientProtocol

// The service calls this method to push an audio buffer to the client.
- (void)didReceiveAudioBuffer:(MixedAudioBuffer *)buffer;

@end

#endif /* DuetXPCClientProtocol_h */
