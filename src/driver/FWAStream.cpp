#include "FWAStream.hpp"
#include <libkern/OSByteOrder.h>
#include <algorithm> // For std::clamp
#include <cmath>     // For lrintf
#include <os/log.h>

#if defined(__arm64__) || defined(__aarch64__)
#include <arm_neon.h>
#endif

void FWAStream::ApplyProcessing(Float32* frames,
                                UInt32    frameCount,
                                UInt32    channelCount) const
{

    // no-op for now, as we are not applying any DSP processing
    // const size_t sampleCount = static_cast<size_t>(frameCount) * channelCount;
    // for (size_t i = 0; i < sampleCount; ++i)
    //     frames[i] *= 1.0f;           // unity-gain placeholder (keep for future DSP)
}

// ---- NEW SIMD CONVERTER -----------------------------------------------------

/*
 *  24-bit signed audio in 32-bit big-endian words, label bit 0x40000000 set.
 *  Input  : little-endian IEEE-754 float32, range −1.0 … +1.0
 *  Output : uint32_t* in big-endian format for AM824 transmission
 *           Layout per sample: [ sign-extended 24-bit << 8 ] | 0x40000000
 *  CRITICAL: Convert to big-endian first, then apply label to MSB!
 */
void FWAStream::ConvertToHardwareFormat(const Float32* input,
                                        void*          output,
                                        UInt32         frameCount,
                                        UInt32         channelCount) const
{
    const size_t total = static_cast<size_t>(frameCount) * channelCount;
    auto* dst = static_cast<uint32_t*>(output);

#if defined(__arm64__) || defined(__aarch64__)
    // NEON fully vectorized path
    const float32x4_t scale = vdupq_n_f32(8388607.0f);
    const int32x4_t min_val = vdupq_n_s32(-8388608);
    const int32x4_t max_val = vdupq_n_s32(8388607);
    const uint32x4_t label_mask = vdupq_n_u32(0x40000000);

    size_t i = 0;
    for (; i + 3 < total; i += 4) {
        // Load and scale float samples
        float32x4_t f = vld1q_f32(input + i);
        int32x4_t s = vcvtq_s32_f32(vmulq_f32(f, scale));
        
        // Clamp to 24-bit range
        s = vmaxq_s32(s, min_val);
        s = vminq_s32(s, max_val);
        
        // Shift left by 8 to position 24-bit audio in upper bits
        uint32x4_t shifted = vshlq_n_u32(vreinterpretq_u32_s32(s), 8);
        
        // Apply AM824 label
        uint32x4_t am824 = vorrq_u32(shifted, label_mask);
        
        // Convert to big-endian
        uint32x4_t be = vreinterpretq_u32_u8(vrev32q_u8(vreinterpretq_u8_u32(am824)));
        
        // Store
        vst1q_u32(dst + i, be);
    }
    
    // Handle remaining samples
    for (; i < total; ++i) {
        int32_t sample = std::clamp(static_cast<int32_t>(lrintf(input[i] * 8388607.0f)), 
                                   -8388608, 8388607);
        uint32_t audio_shifted = static_cast<uint32_t>(sample) << 8;
        uint32_t am824_word = audio_shifted | 0x40000000;
        dst[i] = OSSwapHostToBigInt32(am824_word);
    }
#else
    // Scalar fallback (same as above)
    for (size_t i = 0; i < total; ++i) {
        int32_t sample = std::clamp(static_cast<int32_t>(lrintf(input[i] * 8388607.0f)), 
                                   -8388608, 8388607);
        uint32_t audio_shifted = static_cast<uint32_t>(sample) << 8;
        uint32_t am824_word = audio_shifted | 0x40000000;
        dst[i] = OSSwapHostToBigInt32(am824_word);
    }
#endif

#if DEBUG
    // --- START OF LOGGING BLOCK (flooded every call) ---
    os_log(OS_LOG_DEFAULT, "FWAStream::ConvertToHardwareFormat --- FWASTREAM: LOGGING CONVERTED (HARDWARE) AUDIO ---");
    os_log(OS_LOG_DEFAULT, "FWAStream::ConvertToHardwareFormat --- This is the final Big-Endian data after our conversion.");

    const uint8_t* bytes = static_cast<const uint8_t*>(output);
    UInt32 bytes_to_log = std::min((UInt32)32, frameCount * channelCount * 4);

    // Log the raw bytes in Hex to verify byte order
    char hex_string[256] = {};
    for (UInt32 i = 0; i < bytes_to_log; ++i) {
        snprintf(hex_string + (i * 3), 4, "%02X ", bytes[i]);
    }
    os_log(OS_LOG_DEFAULT, "FWAStream::ConvertToHardwareFormat --- First %u converted bytes (Hex): %{public}s", bytes_to_log, hex_string);

    // Log the first few samples interpreted as integers
    os_log(OS_LOG_DEFAULT, "FWAStream::ConvertToHardwareFormat --- Interpreting converted samples:");
    UInt32 samples_to_log = std::min((UInt32)4, frameCount);
    for (UInt32 i = 0; i < samples_to_log; ++i) {
        if (channelCount >= 2) {
            // Read the big-endian values directly from the output buffer
            uint32_t left_be = dst[i * 2 + 0];
            uint32_t right_be = dst[i * 2 + 1];

            // Swap them back to host order ONLY for printing the correct number
            uint32_t left_host = OSSwapBigToHostInt32(left_be);
            uint32_t right_host = OSSwapBigToHostInt32(right_be);

            os_log(OS_LOG_DEFAULT, "FWAStream::ConvertToHardwareFormat --- Converted Sample %u: L_BE=0x%08X (HostVal=0x%08X), R_BE=0x%08X (HostVal=0x%08X)",
                   i, left_be, left_host, right_be, right_host);
        } else {
            // Mono
            uint32_t sample_be = dst[i];
            uint32_t sample_host = OSSwapBigToHostInt32(sample_be);
            os_log(OS_LOG_DEFAULT, "FWAStream::ConvertToHardwareFormat --- Converted Sample %u: BE=0x%08X (HostVal=0x%08X)",
                   i, sample_be, sample_host);
        }
    }
    os_log(OS_LOG_DEFAULT, "FWAStream::ConvertToHardwareFormat --- END CONVERTED LOG ---");
#endif
}

AudioStreamBasicDescription FWAStream::GetVirtualFormat() const
{
    // Request 32-bit float from Core Audio
    AudioStreamBasicDescription fmt = {};
    fmt.mSampleRate       = GetSampleRate();
    fmt.mFormatID         = kAudioFormatLinearPCM;
    fmt.mFormatFlags      = kAudioFormatFlagIsFloat | 
                           kAudioFormatFlagsNativeEndian;
    fmt.mBitsPerChannel   = 32;
    fmt.mChannelsPerFrame = GetChannelCount();
    fmt.mBytesPerFrame    = sizeof(Float32) * GetChannelCount();
    fmt.mFramesPerPacket  = 1;
    fmt.mBytesPerPacket   = fmt.mBytesPerFrame;
    // os_log(OS_LOG_DEFAULT, "FWAStream::GetVirtualFormat() - Returning Float32 format: SampleRate=%.2f, Channels=%u",
    //        fmt.mSampleRate, fmt.mChannelsPerFrame);
    return fmt;
}

// OSStatus FWAStream::IsPropertySettable(AudioObjectID objectID, pid_t clientPID,
//                                         const AudioObjectPropertyAddress* address,
//                                         Boolean* outIsSettable) const
// {
//     // If a client asks if they can change the virtual format, tell them NO.
//     if (address->mSelector == kAudioStreamPropertyVirtualFormat) {
//         *outIsSettable = false;
//         return noErr;
//     }

//     // For all other properties, defer to the base class implementation.
//     return aspl::Stream::IsPropertySettable(objectID, clientPID, address, outIsSettable);
// }