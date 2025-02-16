//
//  XPCReceiverClient.mm
//  avc-apple2
//
//  Created by Alexander Shabelnikov on 14.02.2025.
//

#import "FWA/XPC/XPCReceiverClient.hpp"

@interface XPCReceiverClient ()
@property (nonatomic, strong) dispatch_queue_t audioProcessingQueue;
@end

@implementation XPCReceiverClient

- (instancetype)init {
    self = [super init];
    if (self) {
        _processor = NULL;
        // Create a serial queue dedicated to audio processing.
        // Use a high QoS to ensure timely handling.
        _audioProcessingQueue = dispatch_queue_create("net.mrmidi.audioProcessingQueue", DISPATCH_QUEUE_SERIAL);
        dispatch_set_target_queue(_audioProcessingQueue, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0));
        NSLog(@"[XPCReceiverClient] Initialized");
    }
    return self;
}

- (void)didReceiveAudioBuffer:(MixedAudioBuffer *)buffer {
    if (_processor) {
        // Since we no longer have a direct reference to the processor type,
        // we can't call its methods directly. This needs to be handled elsewhere.
        NSData *pcmData = buffer.pcmData;
        dispatch_async(self.audioProcessingQueue, ^{
            // The audio processing logic needs to be reimplemented
            // without relying on AmdtpTransmitStreamProcessor
            NSLog(@"[XPCReceiverClient] Received audio data (%lu bytes)", (unsigned long)pcmData.length);
            // Audio processing will be implemented in a different way
        });
    } else {
        NSLog(@"[XPCReceiverClient] Warning: Processor is null, audio data dropped");
    }
}

@end
