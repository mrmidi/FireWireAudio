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

    size_t i = 0;
    for (; i + 3 < total; i += 4) {
        // Load and scale float samples
        float32x4_t f = vld1q_f32(input + i);
        int32x4_t s = vcvtq_s32_f32(vmulq_f32(f, scale));
        
        // Clamp to 24-bit range
        s = vmaxq_s32(s, min_val);
        s = vminq_s32(s, max_val);
        
        // Mask to 24-bit and build AM824 words like scalar version
        uint32x4_t audio_24bit = vandq_u32(vreinterpretq_u32_s32(s), vdupq_n_u32(0x00FFFFFF));
        
        // Extract bytes: [lo, mid, hi, 0] for each sample  
        uint32x4_t byte0 = vandq_u32(audio_24bit, vdupq_n_u32(0xFF));           // lo -> MSB
        uint32x4_t byte1 = vandq_u32(vshrq_n_u32(audio_24bit, 8), vdupq_n_u32(0xFF));  // mid
        uint32x4_t byte2 = vandq_u32(vshrq_n_u32(audio_24bit, 16), vdupq_n_u32(0xFF)); // hi
        uint32x4_t byte3 = vdupq_n_u32(0x40);                                   // label -> LSB
        
        // Build final word: (byte0 << 24) | (byte1 << 16) | (byte2 << 8) | byte3
        uint32x4_t am824 = vorrq_u32(
            vorrq_u32(vshlq_n_u32(byte0, 24), vshlq_n_u32(byte1, 16)),
            vorrq_u32(vshlq_n_u32(byte2, 8), byte3)
        );

        // Store
        vst1q_u32(dst + i, am824);
    }
    
    // Handle remaining samples  
    for (; i < total; ++i) {
        int32_t sample = std::clamp(static_cast<int32_t>(lrintf(input[i] * 8388607.0f)), 
                                   -8388608, 8388607);
        // Build AM824 word - on LE system, reverse byte order to get correct BE result
        uint32_t audio_24bit = static_cast<uint32_t>(sample) & 0x00FFFFFF;
        uint8_t byte0 = audio_24bit & 0xFF;            // audio low byte -> MSB
        uint8_t byte1 = (audio_24bit >> 8) & 0xFF;     // audio mid byte
        uint8_t byte2 = (audio_24bit >> 16) & 0xFF;    // audio high byte  
        uint8_t byte3 = 0x40;                          // AM824 label -> LSB
        dst[i] = (byte0 << 24) | (byte1 << 16) | (byte2 << 8) | byte3;
    }
#else
    // Scalar fallback (same as above)
    for (size_t i = 0; i < total; ++i) {
        int32_t sample = std::clamp(static_cast<int32_t>(lrintf(input[i] * 8388607.0f)), 
                                   -8388608, 8388607);
        // Build AM824 word - on LE system, reverse byte order to get correct BE result
        uint32_t audio_24bit = static_cast<uint32_t>(sample) & 0x00FFFFFF;
        uint8_t byte0 = audio_24bit & 0xFF;            // audio low byte -> MSB
        uint8_t byte1 = (audio_24bit >> 8) & 0xFF;     // audio mid byte
        uint8_t byte2 = (audio_24bit >> 16) & 0xFF;    // audio high byte  
        uint8_t byte3 = 0x40;                          // AM824 label -> LSB
        dst[i] = (byte0 << 24) | (byte1 << 16) | (byte2 << 8) | byte3;
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

