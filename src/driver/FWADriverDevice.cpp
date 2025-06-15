#include "FWADriverDevice.hpp"
#include <aspl/Device.hpp> // Include necessary ASPL headers
#include <aspl/Tracer.hpp>
#include <aspl/Context.hpp>
#include <string>
#include <algorithm>       // For std::min, std::copy
#include <cstring>
#include <libkern/OSByteOrder.h>
#include <limits>
#include <cassert>
#include "FWADriverHandler.hpp"
#include "FWAStream.hpp"
#include <CoreAudio/AudioServerPlugIn.h>
#include <atomic>

#define DEBUG 1
#include <os/log.h>
#include <cstdio>

constexpr const char* LogPrefix = "FWADriverASPL: ";

// --- Local Helper Function ---
static inline std::string FormatFourCharCode(UInt32 code) {
    char chars[5];
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    UInt32 beCode = OSSwapHostToBigInt32(code);
#else
    UInt32 beCode = code;
#endif
    memcpy(chars, &beCode, 4);
    for (int i = 0; i < 4; ++i) {
        if (chars[i] < 32 || chars[i] > 126) {
            chars[i] = '?';
        }
    }
    chars[4] = '\0';
    return std::string("'") + chars + "'";
}

FWADriverDevice::FWADriverDevice(std::shared_ptr<const aspl::Context> context,
                                 const aspl::DeviceParameters& params)
    : aspl::Device(context, params) // Forward to base class
{
#if DEBUG
    printf("FWADriverDevice::FWADriverDevice - Constructed with ID %u, Name '%s'\n", GetID(), params.Name.c_str());
#endif
    // Custom initialization if needed
}

// --- Property Dispatch Implementations ---

Boolean FWADriverDevice::HasProperty(AudioObjectID objectID,
                                    pid_t clientPID,
                                    const AudioObjectPropertyAddress* address) const
{
    std::string selectorStr = address ? FormatFourCharCode(address->mSelector) : "NULL";
    // Check if it's a property we handle specifically
    if (address && address->mSelector == kAudioDevicePropertyAvailableNominalSampleRates &&
        address->mScope == kAudioObjectPropertyScopeGlobal &&
        address->mElement == kAudioObjectPropertyElementMain) // Use Main
    {
#if DEBUG
        printf("FWADriverDevice::HasProperty - Responding YES for selector %s\n", selectorStr.c_str());
#endif
        return true;
    }

    // Otherwise, let the base class handle it
    bool baseHas = aspl::Device::HasProperty(objectID, clientPID, address);
    return baseHas;
}

OSStatus FWADriverDevice::GetPropertyDataSize(AudioObjectID objectID,
                                            pid_t clientPID,
                                            const AudioObjectPropertyAddress* address,
                                            UInt32 qualifierDataSize,
                                            const void* qualifierData,
                                            UInt32* outDataSize) const
{
    std::string selectorStr = address ? FormatFourCharCode(address->mSelector) : "NULL";
    if (!address || !outDataSize) {
#if DEBUG
        printf("FWADriverDevice::GetPropertyDataSize - ERROR: Invalid address or outDataSize pointer.\n");
#endif
        return kAudioHardwareBadObjectError;
    }

    // Handle our specific property
    if (address->mSelector == kAudioDevicePropertyAvailableNominalSampleRates &&
        address->mScope == kAudioObjectPropertyScopeGlobal &&
        address->mElement == kAudioObjectPropertyElementMain) // Use Main
    {
        // Simulate getting rates (replace with XPC call later)
        auto rates = GetSimulatedAvailableSampleRates();
        size_t requiredSize_t = rates.size() * sizeof(AudioValueRange);
        if (requiredSize_t > std::numeric_limits<UInt32>::max()) {
#if DEBUG
            printf("FWADriverDevice::GetPropertyDataSize - ERROR: Required size (%zu) exceeds UINT32_MAX for selector %s\n", requiredSize_t, selectorStr.c_str());
#endif
            *outDataSize = 0;
            return kAudioHardwareUnspecifiedError;
        }
        *outDataSize = static_cast<UInt32>(requiredSize_t);
#if DEBUG
        printf("FWADriverDevice::GetPropertyDataSize - Reporting size %u for selector %s\n", *outDataSize, selectorStr.c_str());
#endif
        return kAudioHardwareNoError;
    }

    // Let the base class handle others
    OSStatus result = aspl::Device::GetPropertyDataSize(objectID, clientPID, address, qualifierDataSize, qualifierData, outDataSize);
    if (result != kAudioHardwareNoError) {
#if DEBUG
        printf("FWADriverDevice::GetPropertyDataSize - ERROR: Base class failed for selector %s, result %#x\n", selectorStr.c_str(), result);
#endif
    }
    return result;
}

OSStatus FWADriverDevice::GetPropertyData(AudioObjectID objectID,
                                        pid_t clientPID,
                                        const AudioObjectPropertyAddress* address,
                                        UInt32 qualifierDataSize,
                                        const void* qualifierData,
                                        UInt32 inDataSize,
                                        UInt32* outDataSize,
                                        void* outData) const
{
    std::string selectorStr = address ? FormatFourCharCode(address->mSelector) : "NULL";
     if (!address || !outDataSize || !outData) {
#if DEBUG
        printf("FWADriverDevice::GetPropertyData - ERROR: Invalid address, outDataSize, or outData pointer.\n");
#endif
        return kAudioHardwareBadObjectError;
    }

    // Handle our specific property
    if (address->mSelector == kAudioDevicePropertyAvailableNominalSampleRates &&
        address->mScope == kAudioObjectPropertyScopeGlobal &&
        address->mElement == kAudioObjectPropertyElementMain) // Use Main
    {
        // Simulate getting rates (replace with XPC call later)
        auto rates = GetSimulatedAvailableSampleRates();
        size_t requiredSize_t = rates.size() * sizeof(AudioValueRange);
        if (requiredSize_t > std::numeric_limits<UInt32>::max()) {
#if DEBUG
            printf("FWADriverDevice::GetPropertyData - ERROR: Required size (%zu) exceeds UINT32_MAX for selector %s\n", requiredSize_t, selectorStr.c_str());
#endif
            *outDataSize = 0;
            return kAudioHardwareUnspecifiedError;
        }
        UInt32 calculatedSize = static_cast<UInt32>(requiredSize_t);

        UInt32 bytesToWrite = std::min(inDataSize, calculatedSize);
        *outDataSize = bytesToWrite; // Return how much we *actually* wrote

        if (bytesToWrite > 0) {
#if DEBUG
             printf("FWADriverDevice::GetPropertyData - Writing %u bytes (of %u needed) for selector %s\n", bytesToWrite, calculatedSize, selectorStr.c_str());
#endif
             memcpy(outData, rates.data(), bytesToWrite);
        } else if (inDataSize == 0) {
#if DEBUG
             printf("FWADriverDevice::GetPropertyData - WARNING: Zero-size buffer provided for selector %s\n", selectorStr.c_str());
#endif
             *outDataSize = 0;
        } else {
#if DEBUG
             printf("FWADriverDevice::GetPropertyData - WARNING: Buffer too small for selector %s (needed %u, got %u), wrote 0 bytes\n", selectorStr.c_str(), calculatedSize, inDataSize);
#endif
             *outDataSize = 0;
        }
         return kAudioHardwareNoError;
    }

    // Let the base class handle others
    OSStatus result = aspl::Device::GetPropertyData(objectID, clientPID, address, qualifierDataSize, qualifierData, inDataSize, outDataSize, outData);
    if (result != kAudioHardwareNoError) {
        os_log(OS_LOG_DEFAULT, "FWADriverDevice::GetPropertyData - ERROR: Base class failed for selector %s, result %#x", selectorStr.c_str(), result);
    }
    return result;
}

// --- Helper Implementation ---

std::vector<AudioValueRange> FWADriverDevice::GetSimulatedAvailableSampleRates() const
{
    // !! Placeholder !! Replace this with an XPC call to the daemon later
    return {
        {44100.0, 44100.0},
        {48000.0, 48000.0},
        {88200.0, 88200.0},
        {96000.0, 96000.0}
    };
}

// Helper function to log an AudioTimeStamp (optional, but keeps DoIOOperation cleaner)
static void LogAudioTimeStamp(const char* prefix, const AudioTimeStamp& ts) {
    // Check for valid flags to determine what to print
    std::string flagsStr;
    if (ts.mFlags & kAudioTimeStampSampleTimeValid) flagsStr += "SampleTimeValid ";
    if (ts.mFlags & kAudioTimeStampHostTimeValid) flagsStr += "HostTimeValid ";
    if (ts.mFlags & kAudioTimeStampRateScalarValid) flagsStr += "RateScalarValid ";
    if (ts.mFlags & kAudioTimeStampWordClockTimeValid) flagsStr += "WordClockTimeValid ";
    if (ts.mFlags & kAudioTimeStampSMPTETimeValid) flagsStr += "SMPTETimeValid ";
    if (ts.mFlags & kAudioTimeStampSampleHostTimeValid) flagsStr += "SampleHostTimeValid "; // macOS 10.15+

    os_log_debug(OS_LOG_DEFAULT, "%s%s: Flags=[%s], SampleTime=%.0f, HostTime=%llu, RateScalar=%.6f",
                 LogPrefix, // Your existing LogPrefix
                 prefix,
                 flagsStr.empty() ? "None" : flagsStr.c_str(),
                 (ts.mFlags & kAudioTimeStampSampleTimeValid) ? ts.mSampleTime : -1.0,
                 (ts.mFlags & kAudioTimeStampHostTimeValid) ? ts.mHostTime : 0ULL,
                 (ts.mFlags & kAudioTimeStampRateScalarValid) ? ts.mRateScalar : 0.0);

    // For SMPTETime, you'd need to break down mSMPTETime structure
    // For WordClockTime, it's just a UInt64
}

// --- IO Operation Support ---
OSStatus FWADriverDevice::WillDoIOOperation(AudioObjectID inDeviceObjectID,
                                           UInt32 inClientID,
                                           UInt32 inOperationID,
                                           Boolean* outWillDo,
                                           Boolean* outWillDoInPlace)
{
    if (!outWillDo || !outWillDoInPlace) {
        return kAudioHardwareBadObjectError;
    }

    // Tell Core Audio which operations we will handle
    switch(inOperationID) {
        case kAudioServerPlugInIOOperationConvertMix:
            *outWillDo = true;
            *outWillDoInPlace = false; // Requires secondary buffer for float->AM824 conversion
            // os_log(OS_LOG_DEFAULT, "FWADriverDevice::WillDoIOOperation - Will handle ConvertMix (not in-place)");
            break;
            
        case kAudioServerPlugInIOOperationWriteMix:
            *outWillDo = true;
            *outWillDoInPlace = true; // Can write directly from buffer
            // os_log(OS_LOG_DEFAULT, "FWADriverDevice::WillDoIOOperation - Will handle WriteMix (in-place)");
            break;
            
        case kAudioServerPlugInIOOperationProcessOutput:
        case kAudioServerPlugInIOOperationProcessMix:
            *outWillDo = true;
            *outWillDoInPlace = true;
            // os_log(OS_LOG_DEFAULT, "FWADriverDevice::WillDoIOOperation - Will handle ProcessOutput/ProcessMix (in-place)");
            break;
            
        default:
            *outWillDo = false;
            *outWillDoInPlace = false;
            // os_log(OS_LOG_DEFAULT, "FWADriverDevice::WillDoIOOperation - Will NOT handle operation %u", inOperationID);
            break;
    }
    
    return noErr;
}

OSStatus FWADriverDevice::DoIOOperation(AudioObjectID objectID,
                                       AudioObjectID streamID,
                                       UInt32 clientID,
                                       UInt32 operationID,
                                       UInt32 ioBufferFrameSize,
                                       const AudioServerPlugInIOCycleInfo* ioCycleInfo,
                                       void* ioMainBuffer,
                                       void* ioSecondaryBuffer)
{

    // --- Operation Name Logging ---
    const char* opName = "unknown";
    switch(operationID) {
        case kAudioServerPlugInIOOperationThread: opName = "Thread"; break;
        case kAudioServerPlugInIOOperationCycle: opName = "Cycle"; break;
        case kAudioServerPlugInIOOperationReadInput: opName = "ReadInput"; break;
        case kAudioServerPlugInIOOperationConvertInput: opName = "ConvertInput"; break;
        case kAudioServerPlugInIOOperationProcessInput: opName = "ProcessInput"; break;
        case kAudioServerPlugInIOOperationProcessOutput: opName = "ProcessOutput"; break;
        case kAudioServerPlugInIOOperationMixOutput: opName = "MixOutput"; break;
        case kAudioServerPlugInIOOperationProcessMix: opName = "ProcessMix"; break;
        case kAudioServerPlugInIOOperationConvertMix: opName = "ConvertMix"; break;
        case kAudioServerPlugInIOOperationWriteMix: opName = "WriteMix"; break;
    }
    // os_log(OS_LOG_DEFAULT,  "FWADriverDevice::DoIOOperation - Operation: %s", opName);

    static std::once_flag io_once_flag;
    std::call_once(io_once_flag, [&]() {
        // os_log(OS_LOG_DEFAULT, "FWADriverDevice::DoIOOperation - I/O loop has started for the first time.");
    });

    auto stream = GetStreamByID(streamID);
    if (!stream) {
        // os_log(OS_LOG_DEFAULT, "FWADriverDevice::DoIOOperation - ERROR: Bad stream ID %u", streamID);
        return kAudioHardwareBadStreamError;
    }
    
    auto* fwaStream = static_cast<FWAStream*>(stream.get());
    auto* handler = static_cast<FWADriverHandler*>(GetIOHandler());
    
    // CRITICAL: Use VIRTUAL format for input, PHYSICAL for output
    const auto& virtualFmt = fwaStream->GetVirtualFormat();
    const auto& physicalFmt = fwaStream->GetPhysicalFormat();
    
    const bool nonInterleaved = 
        (virtualFmt.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;

    switch (operationID) {
        case kAudioServerPlugInIOOperationWriteMix:
            // no-op: data already written in ConvertMix
            return noErr;

        case kAudioServerPlugInIOOperationReadInput:
            // Handle read input
            return noErr;

        case kAudioServerPlugInIOOperationConvertMix:
        {

        if ((ioBufferFrameSize & (ioBufferFrameSize-1)) != 0) {
        // static bool warned = false;
        // we should determine if we are getting not power-of-two sizes.
        // this is warning, we should determine how to handle it later
            os_log(OS_LOG_DEFAULT,
                "%sWARNING: CoreAudio block size %u is not a power-of-two – "
                "driver will re-chunk which costs extra CPU.",
                LogPrefix, ioBufferFrameSize);
                // warned = true
        }

            // Reserve a slot in shared memory
            uint32_t* hwPtr = handler->reserveRingSlot(
                ioBufferFrameSize,
                ioCycleInfo->mOutputTime
            );
            // Fallback to local buffer if reservation fails
            if (!hwPtr) {
                hwPtr = static_cast<uint32_t*>(ioSecondaryBuffer);
                std::fill_n(hwPtr,
                            ioBufferFrameSize * physicalFmt.mChannelsPerFrame,
                            OSSwapHostToBigInt32(0x40000000));
            }
            // Convert floats to AM824 directly into hwPtr
            fwaStream->ConvertToHardwareFormat(
                static_cast<const Float32*>(ioMainBuffer),
                hwPtr,
                ioBufferFrameSize,
                virtualFmt.mChannelsPerFrame
            );
            // Commit if using shared memory
            if (hwPtr != ioSecondaryBuffer) {
                handler->commitRingSlot();
            }
            return noErr;
        }

        default:
            // os_log(OS_LOG_DEFAULT,
            //        "FWADriverDevice::DoIOOperation - WARNING: Received unknown operationID: %u",
            //        operationID);
            return noErr;
    }
}
