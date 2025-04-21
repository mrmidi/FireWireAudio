// include/FWA/AudioSubunit.hpp
#pragma once

#include "FWA/Subunit.hpp"      // Inherit from base
#include "FWA/AudioPlug.hpp"    // Contains AudioPlugs
#include <vector>
#include <memory>
#include <cstdint>
#include <string>

namespace FWA {

// Forward declaration
class DeviceParser;

/**
 * @brief Represents an Audio subunit (Type 0x08).
 *
 * Stores information specific to audio subunits, including plug counts and plug lists.
 * Placeholder for future Function Block information.
 */
class AudioSubunit : public Subunit {
     friend class DeviceParser; // Allow parser to modify internal state

public:
    /**
     * @brief Construct a new Audio Subunit object.
     * @param id The subunit instance ID (0-7 typically).
     */
    explicit AudioSubunit(uint8_t id = 0) : Subunit(id) {}
    ~AudioSubunit() override = default;

    // --- Virtual overrides from Subunit ---
    std::string getSubunitTypeName() const override { return "Audio"; }
    SubunitType getSubunitType() const override { return SubunitType::Audio; }

    // --- Public accessors ---
    uint32_t getAudioDestPlugCount() const { return audioDestPlugCount_; }
    uint32_t getAudioSourcePlugCount() const { return audioSourcePlugCount_; }
    const std::vector<std::shared_ptr<AudioPlug>>& getAudioDestPlugs() const { return audioDestPlugs_; }
    const std::vector<std::shared_ptr<AudioPlug>>& getAudioSourcePlugs() const { return audioSourcePlugs_; }
    // Add accessors for function blocks, etc. when implemented

    // --- Setters/helpers for plug counts and plug management ---
    void setAudioDestPlugCount(uint32_t count) { audioDestPlugCount_ = count; }
    void setAudioSourcePlugCount(uint32_t count) { audioSourcePlugCount_ = count; }
    void addAudioDestPlug(std::shared_ptr<AudioPlug> plug) { audioDestPlugs_.push_back(plug); }
    void addAudioSourcePlug(std::shared_ptr<AudioPlug> plug) { audioSourcePlugs_.push_back(plug); }
    void clearPlugs() { audioDestPlugs_.clear(); audioSourcePlugs_.clear(); }
    void clearAudioDestPlugs() { audioDestPlugs_.clear(); }
    void clearAudioSourcePlugs() { audioSourcePlugs_.clear(); }

private:
    // --- Data members managed by DeviceParser ---
    uint32_t audioDestPlugCount_{0};
    uint32_t audioSourcePlugCount_{0};
    std::vector<std::shared_ptr<AudioPlug>> audioDestPlugs_;
    std::vector<std::shared_ptr<AudioPlug>> audioSourcePlugs_;
    // Add members for function blocks, descriptors etc.

    // Prevent copying/moving directly if managing resources uniquely
    AudioSubunit(const AudioSubunit&) = delete;
    AudioSubunit& operator=(const AudioSubunit&) = delete;
    AudioSubunit(AudioSubunit&&) = delete;
    AudioSubunit& operator=(AudioSubunit&&) = delete;
};

} // namespace FWA