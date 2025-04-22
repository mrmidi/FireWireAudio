// include/FWA/MusicSubunit.hpp
#pragma once

#include "FWA/Subunit.hpp"      // Inherit from base
#include "FWA/AudioPlug.hpp"    // Contains AudioPlugs
#include "FWA/AVCInfoBlock.hpp" // Contains AVCInfoBlocks
#include <vector>
#include <memory>
#include <cstdint>
#include <optional>
#include <string>
#include <nlohmann/json_fwd.hpp>

namespace FWA {

// Forward declaration
class DeviceParser;

/**
 * @brief Represents a Music subunit (Type 0x0C).
 *
 * Stores information specific to music subunits, including plug counts,
 * plug lists, and potentially parsed status descriptor information.
 */
class MusicSubunit : public Subunit {
    friend class DeviceParser; // Allow parser to modify internal state

public:
    /**
     * @brief Construct a new Music Subunit object.
     * @param id The subunit instance ID (0-7 typically).
     */
    explicit MusicSubunit(uint8_t id = 0) : Subunit(id) {}
    ~MusicSubunit() override = default;

    // --- Virtual overrides from Subunit ---
    std::string getSubunitTypeName() const override { return "Music"; }
    SubunitType getSubunitType() const override { return SubunitType::Music; }

    // --- Public accessors ---
    uint32_t getMusicDestPlugCount() const { return musicDestPlugCount_; }
    uint32_t getMusicSourcePlugCount() const { return musicSourcePlugCount_; }
    const std::vector<std::shared_ptr<AudioPlug>>& getMusicDestPlugs() const { return musicDestPlugs_; }
    const std::vector<std::shared_ptr<AudioPlug>>& getMusicSourcePlugs() const { return musicSourcePlugs_; }
    const std::optional<std::vector<uint8_t>>& getStatusDescriptorData() const { return statusDescriptorData_; }
    const std::vector<std::shared_ptr<AVCInfoBlock>>& getParsedStatusInfoBlocks() const { return parsedStatusInfoBlocks_; }

    void setMusicDestPlugCount(uint32_t count) { musicDestPlugCount_ = count; }
    void setMusicSourcePlugCount(uint32_t count) { musicSourcePlugCount_ = count; }
    void addMusicDestPlug(std::shared_ptr<AudioPlug> plug) { musicDestPlugs_.push_back(plug); }
    void addMusicSourcePlug(std::shared_ptr<AudioPlug> plug) { musicSourcePlugs_.push_back(plug); }
    void setStatusDescriptorData(const std::vector<uint8_t>& data) { statusDescriptorData_ = data; }
    void addParsedStatusInfoBlock(std::shared_ptr<AVCInfoBlock> block) { parsedStatusInfoBlocks_.push_back(block); }
    void clearParsedStatusInfoBlocks() { parsedStatusInfoBlocks_.clear(); }
    void clearPlugs() { musicDestPlugs_.clear(); musicSourcePlugs_.clear(); }
    void clearMusicDestPlugs() { musicDestPlugs_.clear(); }
    void clearMusicSourcePlugs() { musicSourcePlugs_.clear(); }

    nlohmann::json toJson() const;

private:
    // --- Data members managed by DeviceParser ---
    uint32_t musicDestPlugCount_{0};
    uint32_t musicSourcePlugCount_{0};

    std::vector<std::shared_ptr<AudioPlug>> musicDestPlugs_;
    std::vector<std::shared_ptr<AudioPlug>> musicSourcePlugs_;

    std::optional<std::vector<uint8_t>> statusDescriptorData_;          ///< Raw status descriptor bytes
    std::vector<std::shared_ptr<AVCInfoBlock>> parsedStatusInfoBlocks_; ///< Parsed info blocks from status descriptor

    // Prevent copying/moving directly if managing resources uniquely
    MusicSubunit(const MusicSubunit&) = delete;
    MusicSubunit& operator=(const MusicSubunit&) = delete;
    MusicSubunit(MusicSubunit&&) = delete;
    MusicSubunit& operator=(MusicSubunit&&) = delete;
};

} // namespace FWA