#pragma once
#include "AudioPlug.hpp"
#include <vector>
#include <memory>
#include <cstdint>
#include <optional>

namespace FWA {

class MusicSubunit {
public:
    MusicSubunit() = default;
    ~MusicSubunit() = default;
    
    // Plug count getters
    uint32_t getIsoInputCount() const { return isoInputCount_; }
    uint32_t getIsoOutputCount() const { return isoOutputCount_; }
    uint32_t getExternalInputCount() const { return externalInputCount_; }
    uint32_t getExternalOutputCount() const { return externalOutputCount_; }
    uint32_t getMusicDestPlugCount() const { return musicDestPlugCount_; }
    uint32_t getMusicSourcePlugCount() const { return musicSourcePlugCount_; }
    
    // Setters for plug counts
    void setIsoInputCount(uint32_t count) { isoInputCount_ = count; }
    void setIsoOutputCount(uint32_t count) { isoOutputCount_ = count; }
    void setExternalInputCount(uint32_t count) { externalInputCount_ = count; }
    void setExternalOutputCount(uint32_t count) { externalOutputCount_ = count; }
    void setMusicDestPlugCount(uint32_t count) { musicDestPlugCount_ = count; }
    void setMusicSourcePlugCount(uint32_t count) { musicSourcePlugCount_ = count; }
    
    // Plug management
    void addIsoInputPlug(std::shared_ptr<AudioPlug> plug) { isoInputPlugs_.push_back(plug); }
    void addIsoOutputPlug(std::shared_ptr<AudioPlug> plug) { isoOutputPlugs_.push_back(plug); }
    void addExternalInputPlug(std::shared_ptr<AudioPlug> plug) { externalInputPlugs_.push_back(plug); }
    void addExternalOutputPlug(std::shared_ptr<AudioPlug> plug) { externalOutputPlugs_.push_back(plug); }
    void addMusicDestPlug(std::shared_ptr<AudioPlug> plug) { musicDestPlugs_.push_back(plug); }
    void addMusicSourcePlug(std::shared_ptr<AudioPlug> plug) { musicSourcePlugs_.push_back(plug); }
    
    const std::vector<std::shared_ptr<AudioPlug>>& getIsoInputPlugs() const { return isoInputPlugs_; }
    const std::vector<std::shared_ptr<AudioPlug>>& getIsoOutputPlugs() const { return isoOutputPlugs_; }
    const std::vector<std::shared_ptr<AudioPlug>>& getExternalInputPlugs() const { return externalInputPlugs_; }
    const std::vector<std::shared_ptr<AudioPlug>>& getExternalOutputPlugs() const { return externalOutputPlugs_; }
    const std::vector<std::shared_ptr<AudioPlug>>& getMusicDestPlugs() const { return musicDestPlugs_; }
    const std::vector<std::shared_ptr<AudioPlug>>& getMusicSourcePlugs() const { return musicSourcePlugs_; }
    
    // Raw descriptor data (if parsed from the device)
    void setStatusDescriptorData(const std::vector<uint8_t>& data) { statusDescriptorData_ = data; }
    const std::optional<std::vector<uint8_t>>& getStatusDescriptorData() const { return statusDescriptorData_; }
    
private:
    uint32_t isoInputCount_{0};
    uint32_t isoOutputCount_{0};
    uint32_t externalInputCount_{0};
    uint32_t externalOutputCount_{0};
    uint32_t musicDestPlugCount_{0};
    uint32_t musicSourcePlugCount_{0};
    
    std::vector<std::shared_ptr<AudioPlug>> isoInputPlugs_;
    std::vector<std::shared_ptr<AudioPlug>> isoOutputPlugs_;
    std::vector<std::shared_ptr<AudioPlug>> externalInputPlugs_;
    std::vector<std::shared_ptr<AudioPlug>> externalOutputPlugs_;
    std::vector<std::shared_ptr<AudioPlug>> musicDestPlugs_;
    std::vector<std::shared_ptr<AudioPlug>> musicSourcePlugs_;
    
    std::optional<std::vector<uint8_t>> statusDescriptorData_;
};

class AudioSubunit {
public:
    AudioSubunit() = default;
    ~AudioSubunit() = default;
    
    uint32_t getAudioDestPlugCount() const { return audioDestPlugCount_; }
    uint32_t getAudioSourcePlugCount() const { return audioSourcePlugCount_; }
    
    void setAudioDestPlugCount(uint32_t count) { audioDestPlugCount_ = count; }
    void setAudioSourcePlugCount(uint32_t count) { audioSourcePlugCount_ = count; }
    
    void addAudioDestPlug(std::shared_ptr<AudioPlug> plug) { audioDestPlugs_.push_back(plug); }
    void addAudioSourcePlug(std::shared_ptr<AudioPlug> plug) { audioSourcePlugs_.push_back(plug); }
    
    const std::vector<std::shared_ptr<AudioPlug>>& getAudioDestPlugs() const { return audioDestPlugs_; }
    const std::vector<std::shared_ptr<AudioPlug>>& getAudioSourcePlugs() const { return audioSourcePlugs_; }
    
private:
    uint32_t audioDestPlugCount_{0};
    uint32_t audioSourcePlugCount_{0};
    
    std::vector<std::shared_ptr<AudioPlug>> audioDestPlugs_;
    std::vector<std::shared_ptr<AudioPlug>> audioSourcePlugs_;
};

} // namespace FWA
