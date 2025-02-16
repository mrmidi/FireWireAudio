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
    
private:
    uint16_t type_{0};                 ///< Type identifier of the info block
    uint16_t compoundLength_{0};       ///< Length of compound data
    uint16_t primaryFieldsLength_{0};  ///< Length of primary fields
    std::vector<uint8_t> rawData_;     ///< Raw data from device
    std::vector<std::shared_ptr<AVCInfoBlock>> nestedBlocks_;  ///< Nested info blocks
    
    /**
     * @brief Parse primary fields from raw data
     */
    void parsePrimaryFields();
};

} // namespace FWA
