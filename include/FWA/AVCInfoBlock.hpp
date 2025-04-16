#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <optional>

namespace FWA {

/**
 * @brief Represents an AV/C Information Block from a FireWire device
 *
 * This class encapsulates AV/C info block data and provides methods for
 * parsing and displaying the information in a human-readable format.
 */
// --- Helper Structs for Parsed Primary Field Data ---
struct NameInfoData {
    uint8_t nameDataReferenceType = 0;
    uint8_t nameDataAttributes = 0;
    uint16_t maximumNumberOfCharacters = 0;
};
struct GeneralMusicStatusData {
    uint8_t currentTransmitCapability = 0;
    uint8_t currentReceiveCapability = 0;
    uint32_t currentLatencyCapability = 0;
};
struct RoutingStatusData {
     uint8_t numberOfSubunitDestPlugs = 0;
     uint8_t numberOfSubunitSourcePlugs = 0;
     uint16_t numberOfMusicPlugs = 0;
};
struct SubunitPlugInfoData {
    uint8_t subunitPlugId = 0;
    uint16_t signalFormat = 0;
    uint8_t plugType = 0;
    uint16_t numberOfClusters = 0;
    uint16_t numberOfChannels = 0;
};
struct ClusterSignalInfo {
    uint16_t musicPlugId = 0;
    uint8_t streamPosition = 0;
    uint8_t streamLocation = 0;
};
struct ClusterInfoData {
    uint8_t streamFormat = 0;
    uint8_t portType = 0;
    uint8_t numberOfSignals = 0;
    std::vector<ClusterSignalInfo> signals;
};
struct MusicPlugReference {
     uint8_t plugFunctionType = 0;
     uint8_t plugId = 0;
     uint8_t plugFunctionBlockId = 0;
     uint8_t streamPosition = 0;
     uint8_t streamLocation = 0;
};
struct MusicPlugInfoData {
    uint8_t musicPlugType = 0;
    uint16_t musicPlugId = 0;
    uint8_t routingSupport = 0;
    MusicPlugReference source;
    MusicPlugReference destination;
};

class AVCInfoBlock {
public:
    /**
     * @brief Construct a new AVC Info Block
     * @param type Type identifier of the info block
     * @param rawData Raw data bytes from the device
     */
    AVCInfoBlock(uint16_t type, std::vector<uint8_t> rawData)
      : type_(type), rawData_(std::move(rawData)) {}

    ~AVCInfoBlock() = default;
    
    /**
     * @brief Get the type identifier
     * @return uint16_t Type of the info block
     */
    uint16_t getType() const { return type_; }

    /**
     * @brief Get the raw data bytes
     * @return const std::vector<uint8_t>& Raw data from the device
     */
    const std::vector<uint8_t>& getRawData() const { return rawData_; }

    /**
     * @brief Get the compound length
     * @return uint16_t Length of compound data
     */
    uint16_t getCompoundLength() const { return compoundLength_; }

    /**
     * @brief Get the primary fields length
     * @return uint16_t Length of primary fields
     */
    uint16_t getPrimaryFieldsLength() const { return primaryFieldsLength_; }
    
    /**
     * @brief Get the nested info blocks
     * @return const std::vector<std::shared_ptr<AVCInfoBlock>>& Vector of nested blocks
     */
    const std::vector<std::shared_ptr<AVCInfoBlock>>& getNestedBlocks() const { return nestedBlocks_; }
    
    /**
     * @brief Parse the info block data
     * 
     * This method processes the raw data and extracts structured information,
     * including nested info blocks if present.
     */
    void parse();
    
    /**
     * @brief Convert the info block to a human-readable string
     * @param indent Indentation level for formatting (default 0)
     * @return std::string Formatted string representation
     */
    std::string toString(uint32_t indent = 0) const;

    // --- Parsed Primary Field Getters ---
    std::optional<std::string> getRawText() const { return parsed_rawText_; }
    std::optional<NameInfoData> getNameInfo() const { return parsed_nameInfo_; }
    std::optional<GeneralMusicStatusData> getGeneralMusicStatus() const { return parsed_generalMusicStatus_; }
    std::optional<uint8_t> getMusicOutputPlugSourceCount() const { return parsed_musicOutputPlugSourceCount_; }
    std::optional<uint8_t> getSourcePlugNumber() const { return parsed_sourcePlugNumber_; }
    std::optional<uint8_t> getAudioStreamCount() const { return parsed_audioStreamCount_; }
    std::optional<uint8_t> getMidiStreamCount() const { return parsed_midiStreamCount_; }
    std::optional<uint8_t> getSmpteActivity() const { return parsed_smpteActivity_; }
    std::optional<uint8_t> getSampleCountActivity() const { return parsed_sampleCountActivity_; }
    std::optional<uint8_t> getAudioSyncActivity() const { return parsed_audioSyncActivity_; }
    std::optional<RoutingStatusData> getRoutingStatus() const { return parsed_routingStatus_; }
    std::optional<SubunitPlugInfoData> getSubunitPlugInfo() const { return parsed_subunitPlugInfo_; }
    std::optional<ClusterInfoData> getClusterInfo() const { return parsed_clusterInfo_; }
    std::optional<MusicPlugInfoData> getMusicPlugInfo() const { return parsed_musicPlugInfo_; }

private:
    uint16_t type_{0};                 ///< Type identifier of the info block
    uint16_t compoundLength_{0};       ///< Length of compound data
    uint16_t primaryFieldsLength_{0};  ///< Length of primary fields
    std::vector<uint8_t> rawData_;     ///< Raw data from device
    std::vector<std::shared_ptr<AVCInfoBlock>> nestedBlocks_;  ///< Nested info blocks

    // --- Optional Parsed Data Members ---
    std::optional<std::string>                parsed_rawText_;
    std::optional<NameInfoData>               parsed_nameInfo_;
    std::optional<GeneralMusicStatusData>     parsed_generalMusicStatus_;
    std::optional<uint8_t>                    parsed_musicOutputPlugSourceCount_;
    std::optional<uint8_t>                    parsed_sourcePlugNumber_;
    std::optional<uint8_t>                    parsed_audioStreamCount_;
    std::optional<uint8_t>                    parsed_midiStreamCount_;
    std::optional<uint8_t>                    parsed_smpteActivity_;
    std::optional<uint8_t>                    parsed_sampleCountActivity_;
    std::optional<uint8_t>                    parsed_audioSyncActivity_;
    std::optional<RoutingStatusData>          parsed_routingStatus_;
    std::optional<SubunitPlugInfoData>        parsed_subunitPlugInfo_;
    std::optional<ClusterInfoData>            parsed_clusterInfo_;
    std::optional<MusicPlugInfoData>          parsed_musicPlugInfo_;
    
    /**
     * @brief Parse primary fields from raw data
     */
    void parsePrimaryFields();
};

} // namespace FWA
