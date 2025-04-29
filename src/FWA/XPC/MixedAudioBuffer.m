//
//  MixedAudioBuffer.m
//  avc-apple2
//
//  Created by Alexander Shabelnikov on 13.02.2025.
//

#import "FWA/XPC/MixedAudioBuffer.h"

@implementation MixedAudioBuffer

+ (BOOL)supportsSecureCoding {
    return YES;
}

- (instancetype)initWithZeroTimestamp:(double)zeroTimestamp
                            timestamp:(double)timestamp
                              pcmData:(NSData *)pcmData {
    self = [super init];
    if (self) {
        _zeroTimestamp = zeroTimestamp;
        _timestamp = timestamp;
        _pcmData = [pcmData copy];
    }
    return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder {
    double zeroTS = [coder decodeDoubleForKey:@"zeroTimestamp"];
    double ts = [coder decodeDoubleForKey:@"timestamp"];
    NSData *data = [coder decodeObjectOfClass:[NSData class] forKey:@"pcmData"];
    return [self initWithZeroTimestamp:zeroTS timestamp:ts pcmData:data];
}

- (void)encodeWithCoder:(NSCoder *)coder {
    [coder encodeDouble:self.zeroTimestamp forKey:@"zeroTimestamp"];
    [coder encodeDouble:self.timestamp forKey:@"timestamp"];
    [coder encodeObject:self.pcmData forKey:@"pcmData"];
}

@end
