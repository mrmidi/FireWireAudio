#include "FWA/MusicSubunitCapabilities.hpp"
#include "FWA/JsonHelpers.hpp" // Include for helpers
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <spdlog/spdlog.h>

using json = nlohmann::json;
using namespace FWA::JsonHelpers; // Use helpers from this namespace

namespace FWA {

nlohmann::json MusicSubunitCapabilities::toJson() const {

    spdlog::debug("MusicSubunitCapabilities::toJson: Serializing Music Subunit Capabilities to JSON.");

    json j;

    // Version
    std::stringstream ssVer;
    ssVer << static_cast<int>(musicSubunitVersion >> 4) << "." << static_cast<int>(musicSubunitVersion & 0x0F);
    j["musicSubunitVersion"] = ssVer.str();

    // Basic Capability Flags
    json capFlags = json::object();
    capFlags["hasGeneralCapability"] = hasGeneralCapability;
    capFlags["hasAudioCapability"] = hasAudioCapability;
    capFlags["hasMidiCapability"] = hasMidiCapability;
    capFlags["hasSmpteTimeCodeCapability"] = hasSmpteTimeCodeCapability;
    capFlags["hasSampleCountCapability"] = hasSampleCountCapability;
    capFlags["hasAudioSyncCapability"] = hasAudioSyncCapability;
    j["capabilityPresenceFlags"] = capFlags;

    // General Capabilities
    json genCap = json::object();
    if (transmitCapabilityFlags) {
        json txFlags = json::object();
        txFlags["raw"] = *transmitCapabilityFlags;
        txFlags["supportsBlocking"] = supportsBlockingTransmit();
        txFlags["supportsNonBlocking"] = supportsNonBlockingTransmit();
        genCap["transmit"] = txFlags;
    } else {
        genCap["transmit"] = nullptr;
    }
    if (receiveCapabilityFlags) {
         json rxFlags = json::object();
         rxFlags["raw"] = *receiveCapabilityFlags;
         rxFlags["supportsBlocking"] = supportsBlockingReceive();
         rxFlags["supportsNonBlocking"] = supportsNonBlockingReceive();
         genCap["receive"] = rxFlags;
    } else {
        genCap["receive"] = nullptr;
    }
    genCap["latency"] = latencyCapability ? json(*latencyCapability) : json(nullptr);
    j["generalCapabilities"] = genCap;

    // Audio Capabilities
    json audioCap = json::object();
    audioCap["maxInputChannels"] = maxAudioInputChannels ? json(*maxAudioInputChannels) : json(nullptr);
    audioCap["maxOutputChannels"] = maxAudioOutputChannels ? json(*maxAudioOutputChannels) : json(nullptr);
    if (availableAudioFormats) {
        json formatsArr = json::array();
        for(const auto& fmt : *availableAudioFormats) formatsArr.push_back(fmt.toJson());
        audioCap["availableFormats"] = formatsArr;
    } else {
        audioCap["availableFormats"] = json::array();
    }
    j["audioCapabilities"] = audioCap;

    // MIDI Capabilities
    json midiCap = json::object();
    midiCap["maxInputPorts"] = maxMidiInputPorts ? json(*maxMidiInputPorts) : json(nullptr);
    midiCap["maxOutputPorts"] = maxMidiOutputPorts ? json(*maxMidiOutputPorts) : json(nullptr);
    if (midiVersionMajor && midiVersionMinor) {
        std::stringstream ssMidiVer;
        ssMidiVer << static_cast<int>(*midiVersionMajor) << "." << static_cast<int>(*midiVersionMinor);
        midiCap["midiSpecVersion"] = ssMidiVer.str();
    } else {
        midiCap["midiSpecVersion"] = nullptr;
    }
    midiCap["adaptationLayerVersion"] = midiAdaptationLayerVersion ? json(*midiAdaptationLayerVersion) : json(nullptr);
    j["midiCapabilities"] = midiCap;

    // SMPTE Capabilities
    json smpteCap = json::object();
    if (smpteTimeCodeCapabilityFlags) {
        smpteCap["rawFlags"] = *smpteTimeCodeCapabilityFlags;
        smpteCap["canReceive"] = supportsSmpteReceive();
        smpteCap["canTransmit"] = supportsSmpteTransmit();
    } else {
        smpteCap = nullptr;
    }
    j["smpteTimeCodeCapabilities"] = smpteCap; // Changed key name slightly

    // Sample Count Capabilities
    json sampleCap = json::object();
    if (sampleCountCapabilityFlags) {
        sampleCap["rawFlags"] = *sampleCountCapabilityFlags;
        sampleCap["canReceive"] = supportsSampleCountReceive();
        sampleCap["canTransmit"] = supportsSampleCountTransmit();
    } else {
        sampleCap = nullptr;
    }
    j["sampleCountCapabilities"] = sampleCap; // Changed key name slightly

    // Audio SYNC Capabilities
    json syncCap = json::object();
    if (audioSyncCapabilityFlags) {
        syncCap["rawFlags"] = *audioSyncCapabilityFlags;
        syncCap["canReceiveFromBus"] = supportsAudioSyncReceiveFromBus();
        syncCap["canReceiveFromExternal"] = supportsAudioSyncReceiveFromExternal();
    } else {
        syncCap = nullptr;
    }
    j["audioSyncCapabilities"] = syncCap; // Changed key name slightly

    return j;
}

} // namespace FWA
