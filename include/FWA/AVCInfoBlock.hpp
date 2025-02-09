#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <optional>

namespace FWA {

class AVCInfoBlock {
public:
    AVCInfoBlock(uint16_t type, std::vector<uint8_t> rawData)
      : type_(type), rawData_(std::move(rawData)) {

      }
    ~AVCInfoBlock() = default;
    
    // Getters
    uint16_t getType() const { return type_; }
    const std::vector<uint8_t>& getRawData() const { return rawData_; }
    uint16_t getCompoundLength() const { return compoundLength_; }
    uint16_t getPrimaryFieldsLength() const { return primaryFieldsLength_; }
    
    const std::vector<std::shared_ptr<AVCInfoBlock>>& getNestedBlocks() const { return nestedBlocks_; }
    
    // Parse the info block (stub; to be expanded as needed)
    void parse();
    
    // Return a human-readable representation (with indentation)
    std::string toString(uint32_t indent = 0) const;
    
private:
    uint16_t type_{0};
    uint16_t compoundLength_{0};
    uint16_t primaryFieldsLength_{0};
    std::vector<uint8_t> rawData_;
    std::vector<std::shared_ptr<AVCInfoBlock>> nestedBlocks_;
    
    // Internal helper to parse primary fields (stub)
    void parsePrimaryFields();
};

} // namespace FWA
