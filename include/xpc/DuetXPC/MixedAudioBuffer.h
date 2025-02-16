//
//  MixedAudioBuffer.h
//  DuetXPC
//
//  Created by Alexander Shabelnikov on 13.02.2025.
//

#ifndef MixedAudioBuffer_h
#define MixedAudioBuffer_h

#import <Foundation/Foundation.h>

@interface MixedAudioBuffer : NSObject <NSSecureCoding>

@property (nonatomic, readonly) double zeroTimestamp;
@property (nonatomic, readonly) double timestamp;
@property (nonatomic, readonly) NSData *pcmData;

- (instancetype)initWithZeroTimestamp:(double)zeroTimestamp
                            timestamp:(double)timestamp
                              pcmData:(NSData *)pcmData NS_DESIGNATED_INITIALIZER;

@end

#endif /* MixedAudioBuffer_h */
