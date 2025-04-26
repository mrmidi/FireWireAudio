//
//  FWADaemonClientProtocol.h
//  FWADaemon
//
//  Created by Alexander Shabelnikov on 14.02.2025.
//
#ifndef FWADaemonClientProtocol_h
#define FWADaemonClientProtocol_h

#import <Foundation/Foundation.h>
#import "MixedAudioBuffer.h"

@protocol FWADaemonClientProtocol <NSObject>

// Called by the service to deliver an audio buffer.
- (void)didReceiveAudioBuffer:(MixedAudioBuffer *)buffer;

@end

#endif /* FWADaemonClientProtocol_h */
