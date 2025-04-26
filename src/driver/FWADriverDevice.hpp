#ifndef FWADRIVERDEVICE_HPP
#define FWADRIVERDEVICE_HPP

#include <aspl/Device.hpp>
#include <CoreAudio/AudioServerPlugIn.h> // Needed for AudioObjectPropertyAddress
#include <memory>
#include <vector> // Needed for std::vector

class FWADriverDevice : public aspl::Device {
public:
    FWADriverDevice(std::shared_ptr<const aspl::Context> context,
                    const aspl::DeviceParameters& params);

    // --- Property Dispatch Overrides ---
    Boolean HasProperty(AudioObjectID objectID,
        pid_t clientPID,
        const AudioObjectPropertyAddress* address) const override;

    OSStatus GetPropertyDataSize(AudioObjectID objectID,
        pid_t clientPID,
        const AudioObjectPropertyAddress* address,
        UInt32 qualifierDataSize,
        const void* qualifierData,
        UInt32* outDataSize) const override;

    OSStatus GetPropertyData(AudioObjectID objectID,
        pid_t clientPID,
        const AudioObjectPropertyAddress* address,
        UInt32 qualifierDataSize,
        const void* qualifierData,
        UInt32 inDataSize,
        UInt32* outDataSize,
        void* outData) const override;

    UInt32 GetTransportType() const override {
        return kAudioDeviceTransportTypeFireWire;
    }

private:
    // Helper to get the simulated supported rates
    std::vector<AudioValueRange> GetSimulatedAvailableSampleRates() const;
};

#endif // FWADRIVERDEVICE_HPP
