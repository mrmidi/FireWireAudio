//
//  DuetXPCClientProtocol.h
//  avc-apple2
//
//  Created by Alexander Shabelnikov on 13.02.2025.
//

#ifndef DuetXPCClientProtocol_h
#define DuetXPCClientProtocol_h

#import <Foundation/Foundation.h>
#import "MixedAudioBuffer.h"

@protocol DuetXPCClientProtocol

// Called by the service to deliver an audio buffer.
- (void)didReceiveAudioBuffer:(MixedAudioBuffer *)buffer;

@end

#endif /* DuetXPCClientProtocol_h */
