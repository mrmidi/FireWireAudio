#pragma once
#include <aspl/Stream.hpp>
#include <arm_neon.h>
#include <mutex> // Required for std::once_flag

#define DEBUG 1

class FWAStream : public aspl::Stream
{
public:
    using aspl::Stream::Stream;          // inherit ctors

    // ➊ Float-domain DSP: volume, mute, pan, etc.
    void ApplyProcessing(Float32* frames,
                         UInt32    frameCount,
                         UInt32    channelCount) const override;

    // ➋ NEW – lossless float→AM824 conversion
    void ConvertToHardwareFormat(const Float32* inputFrames,
                                 void*          outputBuffer,
                                 UInt32         frameCount,
                                 UInt32         channelCount) const;

    // Override format-related methods for proper float/AM824 handling
    AudioStreamBasicDescription GetVirtualFormat() const override;
    // AudioStreamBasicDescription GetPhysicalFormat() const override;
    // OSStatus IsPropertySettable(AudioObjectID objectID, pid_t clientPID,
    //                             const AudioObjectPropertyAddress* address,
    //                             Boolean* outIsSettable) const override;

private:
    // Add these two flags for one-time logging
    mutable std::once_flag log_unconverted_once_flag_;
    mutable std::once_flag log_converted_once_flag_;
};