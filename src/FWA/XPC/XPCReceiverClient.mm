//
//  XPCReceiverClient.mm
//  avc-apple2
//
//  Created by Alexander Shabelnikov on 14.02.2025.
//

#import "FWA/XPC/XPCReceiverClient.hpp"
#import "FWA/XPC/MixedAudioBuffer.h"
// --- Include the C++ Interface ---
#include "Isoch/interfaces/ITransmitPacketProvider.hpp"

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
    // --- Cast to the C++ PROVIDER INTERFACE POINTER ---
    FWA::Isoch::ITransmitPacketProvider* provider = self.processor; // Direct assignment works now

    if (provider) { // Check if the C++ pointer is valid
        NSData *pcmData = buffer.pcmData;
        // double timestamp = buffer.timestamp; // Timestamp might not be needed by provider

        if (!pcmData || [pcmData length] == 0) {
             NSLog(@"[XPCReceiverClient] Warning: Received empty audio data.");
             return;
        }

        const void* rawBytes = [pcmData bytes];
        size_t dataSize = [pcmData length];

        // --- Call pushAudioData on the provider ---
        // This runs on the XPC queue. The provider's pushAudioData needs to be thread-safe
        // (which it should be if using the RAUL::RingBuffer correctly).
        bool success = provider->pushAudioData(rawBytes, dataSize);
        if (!success) {
            // Log if pushAudioData failed (e.g., buffer full)
            // mute this log for now - it might spam the logs
//            NSLog(@"[XPCReceiverClient] Warning: IsochPacketProvider->pushAudioData failed (buffer full?).");
        }

    } else {
        NSLog(@"[XPCReceiverClient] Warning: Processor (IsochPacketProvider*) is null, audio data dropped");
    }
}

@end
