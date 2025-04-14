// include/FWA/Subunit.hpp
#pragma once

#include "AudioPlug.hpp"    // Include full AudioPlug definition
#include "AVCInfoBlock.hpp" // Include full AVCInfoBlock definition
#include "Enums.hpp"        // Include Enums
#include <vector>
#include <memory>
#include <cstdint>
#include <optional>
#include <string>

namespace FWA {

/**
 * @brief Abstract base class for AV/C subunits (Music, Audio, etc.).
 *
 * Provides a common interface and basic properties for different subunit types.
 */
class Subunit {
public:
    /**
     * @brief Virtual destructor to ensure proper cleanup in derived classes.
     */
    virtual ~Subunit() = default;

    /**
     * @brief Get the specific type of this subunit.
     * @return SubunitType The enum value representing the subunit type.
     */
    virtual SubunitType getSubunitType() const = 0; // Pure virtual

    /**
     * @brief Get a human-readable name for the subunit type.
     * @return std::string The name of the subunit type (e.g., "Music Subunit").
     */
    virtual std::string getSubunitTypeName() const = 0; // Pure virtual

    // Common properties potentially applicable to multiple subunit types could be added here later.
    // For now, we keep it minimal.

protected:
    // Protected constructor for base class
    Subunit() = default;

    // Allow derived classes (and potentially DeviceParser as a friend) to access members
    // friend class DeviceParser; // Optional: If parser needs direct access to protected members

    // Add common protected members if needed later.
};

/**
 * @brief Represents the Music Subunit (Type 0x0C) capabilities and state.
 *
 * Stores information about plugs specific to the music subunit and its
 * status descriptor data.
 */
class MusicSubunit : public Subunit {
    friend class DeviceParser; // Allow parser to modify private members

public:
    MusicSubunit() = default;
    ~MusicSubunit() override = default;

    // --- Subunit Interface Implementation ---
    SubunitType getSubunitType() const override { return SubunitType::Music; }
    std::string getSubunitTypeName() const override { return "Music Subunit"; }

    // --- Public Accessors ---
    uint32_t getMusicDestPlugCount() const { return musicDestPlugCount_; }
    uint32_t getMusicSourcePlugCount() const { return musicSourcePlugCount_; }
    const std::vector<std::shared_ptr<AudioPlug>>& getMusicDestPlugs() const { return musicDestPlugs_; }
    const std::vector<std::shared_ptr<AudioPlug>>& getMusicSourcePlugs() const { return musicSourcePlugs_; }
    const std::vector<std::shared_ptr<AudioPlug>>& getIsoInputPlugs() const { return isoInputPlugs_; }
    const std::vector<std::shared_ptr<AudioPlug>>& getIsoOutputPlugs() const { return isoOutputPlugs_; }
    const std::optional<std::vector<uint8_t>>& getStatusDescriptorData() const { return statusDescriptorData_; }
    const std::vector<std::shared_ptr<AVCInfoBlock>>& getParsedStatusInfoBlocks() const { return parsedStatusInfoBlocks_; }

private:
    // --- Data members managed by DeviceParser ---
    uint32_t musicDestPlugCount_{0};
    uint32_t musicSourcePlugCount_{0};

    std::vector<std::shared_ptr<AudioPlug>> musicDestPlugs_;
    std::vector<std::shared_ptr<AudioPlug>> musicSourcePlugs_;
    std::vector<std::shared_ptr<AudioPlug>> isoInputPlugs_;            ///< Isochronous input plugs (PCR)
    std::vector<std::shared_ptr<AudioPlug>> isoOutputPlugs_;           ///< Isochronous output plugs (PCR)

    std::optional<std::vector<uint8_t>> statusDescriptorData_;          ///< Raw bytes of the status descriptor
    std::vector<std::shared_ptr<AVCInfoBlock>> parsedStatusInfoBlocks_; ///< Parsed top-level info blocks from status desc

    // --- Private setters/modifiers for DeviceParser ---
    void setMusicDestPlugCount(uint32_t count) { musicDestPlugCount_ = count; }
    void setMusicSourcePlugCount(uint32_t count) { musicSourcePlugCount_ = count; }
    void addMusicDestPlug(std::shared_ptr<AudioPlug> plug) { musicDestPlugs_.push_back(plug); }
    void addMusicSourcePlug(std::shared_ptr<AudioPlug> plug) { musicSourcePlugs_.push_back(plug); }
    void clearMusicDestPlugs() { musicDestPlugs_.clear(); }
    void clearMusicSourcePlugs() { musicSourcePlugs_.clear(); }
    void addIsoInputPlug(std::shared_ptr<AudioPlug> plug) { isoInputPlugs_.push_back(plug); }
    void addIsoOutputPlug(std::shared_ptr<AudioPlug> plug) { isoOutputPlugs_.push_back(plug); }
    void clearIsoInputPlugs() { isoInputPlugs_.clear(); }
    void clearIsoOutputPlugs() { isoOutputPlugs_.clear(); }
    void setStatusDescriptorData(const std::vector<uint8_t>& data) { statusDescriptorData_ = data; }
    void addParsedStatusInfoBlock(std::shared_ptr<AVCInfoBlock> block) { parsedStatusInfoBlocks_.push_back(block); }
    void clearParsedStatusInfoBlocks() { parsedStatusInfoBlocks_.clear(); }
};

/**
 * @brief Represents the Audio Subunit (Type 0x08) capabilities and state.
 *
 * Stores information about plugs specific to the audio subunit.
 * (Function Block information is deferred in this implementation phase).
 */
class AudioSubunit : public Subunit {
     friend class DeviceParser; // Allow parser to modify private members

public:
    AudioSubunit() = default;
    ~AudioSubunit() override = default;

    // --- Subunit Interface Implementation ---
    SubunitType getSubunitType() const override { return SubunitType::Audio; }
    std::string getSubunitTypeName() const override { return "Audio Subunit"; }

    // --- Public Accessors ---
    uint32_t getAudioDestPlugCount() const { return audioDestPlugCount_; }
    uint32_t getAudioSourcePlugCount() const { return audioSourcePlugCount_; }
    const std::vector<std::shared_ptr<AudioPlug>>& getAudioDestPlugs() const { return audioDestPlugs_; }
    const std::vector<std::shared_ptr<AudioPlug>>& getAudioSourcePlugs() const { return audioSourcePlugs_; }
    // Add accessors for function blocks, etc. when implemented

private:
    // --- Data members managed by DeviceParser ---
    uint32_t audioDestPlugCount_{0};
    uint32_t audioSourcePlugCount_{0};

    std::vector<std::shared_ptr<AudioPlug>> audioDestPlugs_;
    std::vector<std::shared_ptr<AudioPlug>> audioSourcePlugs_;
    // Add members for function blocks, descriptors etc. when implemented

     // --- Private setters/modifiers for DeviceParser ---
    void setAudioDestPlugCount(uint32_t count) { audioDestPlugCount_ = count; }
    void setAudioSourcePlugCount(uint32_t count) { audioSourcePlugCount_ = count; }
    void addAudioDestPlug(std::shared_ptr<AudioPlug> plug) { audioDestPlugs_.push_back(plug); }
    void addAudioSourcePlug(std::shared_ptr<AudioPlug> plug) { audioSourcePlugs_.push_back(plug); }
    void clearAudioDestPlugs() { audioDestPlugs_.clear(); }
    void clearAudioSourcePlugs() { audioSourcePlugs_.clear(); }
};

} // namespace FWA