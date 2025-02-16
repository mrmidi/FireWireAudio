#pragma once
#include "AudioPlug.hpp"
#include <vector>
#include <memory>
#include <cstdint>
#include <optional>

namespace FWA {

/**
 * @brief Music subunit implementation
 * 
 * Represents a music subunit in a FireWire audio device, which typically
 * handles MIDI data and music-related functionality.
 */
class MusicSubunit {
public:
    MusicSubunit() = default;
    ~MusicSubunit() = default;
    
    /**
     * @brief Get ISO input plug count
     * @return uint32_t Number of ISO input plugs
     */
    uint32_t getIsoInputCount() const { return isoInputCount_; }
    
    /**
     * @brief Get ISO output plug count
     * @return uint32_t Number of ISO output plugs
     */
    uint32_t getIsoOutputCount() const { return isoOutputCount_; }
    
    /**
     * @brief Get external input plug count
     * @return uint32_t Number of external input plugs
     */
    uint32_t getExternalInputCount() const { return externalInputCount_; }
    
    /**
     * @brief Get external output plug count
     * @return uint32_t Number of external output plugs
     */
    uint32_t getExternalOutputCount() const { return externalOutputCount_; }
    
    /**
     * @brief Get music destination plug count
     * @return uint32_t Number of music destination plugs
     */
    uint32_t getMusicDestPlugCount() const { return musicDestPlugCount_; }
    
    /**
     * @brief Get music source plug count
     * @return uint32_t Number of music source plugs
     */
    uint32_t getMusicSourcePlugCount() const { return musicSourcePlugCount_; }
    
    /**
     * @brief Set ISO input plug count
     * @param count Number of ISO input plugs
     */
    void setIsoInputCount(uint32_t count) { isoInputCount_ = count; }
    
    /**
     * @brief Set ISO output plug count
     * @param count Number of ISO output plugs
     */
    void setIsoOutputCount(uint32_t count) { isoOutputCount_ = count; }
    
    /**
     * @brief Set external input plug count
     * @param count Number of external input plugs
     */
    void setExternalInputCount(uint32_t count) { externalInputCount_ = count; }
    
    /**
     * @brief Set external output plug count
     * @param count Number of external output plugs
     */
    void setExternalOutputCount(uint32_t count) { externalOutputCount_ = count; }
    
    /**
     * @brief Set music destination plug count
     * @param count Number of music destination plugs
     */
    void setMusicDestPlugCount(uint32_t count) { musicDestPlugCount_ = count; }
    
    /**
     * @brief Set music source plug count
     * @param count Number of music source plugs
     */
    void setMusicSourcePlugCount(uint32_t count) { musicSourcePlugCount_ = count; }
    
    /**
     * @brief Add an ISO input plug
     * @param plug Shared pointer to the ISO input plug
     */
    void addIsoInputPlug(std::shared_ptr<AudioPlug> plug) { isoInputPlugs_.push_back(plug); }
    
    /**
     * @brief Add an ISO output plug
     * @param plug Shared pointer to the ISO output plug
     */
    void addIsoOutputPlug(std::shared_ptr<AudioPlug> plug) { isoOutputPlugs_.push_back(plug); }
    
    /**
     * @brief Add an external input plug
     * @param plug Shared pointer to the external input plug
     */
    void addExternalInputPlug(std::shared_ptr<AudioPlug> plug) { externalInputPlugs_.push_back(plug); }
    
    /**
     * @brief Add an external output plug
     * @param plug Shared pointer to the external output plug
     */
    void addExternalOutputPlug(std::shared_ptr<AudioPlug> plug) { externalOutputPlugs_.push_back(plug); }
    
    /**
     * @brief Add a music destination plug
     * @param plug Shared pointer to the music destination plug
     */
    void addMusicDestPlug(std::shared_ptr<AudioPlug> plug) { musicDestPlugs_.push_back(plug); }
    
    /**
     * @brief Add a music source plug
     * @param plug Shared pointer to the music source plug
     */
    void addMusicSourcePlug(std::shared_ptr<AudioPlug> plug) { musicSourcePlugs_.push_back(plug); }
    
    /**
     * @brief Get the list of ISO input plugs
     * @return const std::vector<std::shared_ptr<AudioPlug>>& List of ISO input plugs
     */
    const std::vector<std::shared_ptr<AudioPlug>>& getIsoInputPlugs() const { return isoInputPlugs_; }
    
    /**
     * @brief Get the list of ISO output plugs
     * @return const std::vector<std::shared_ptr<AudioPlug>>& List of ISO output plugs
     */
    const std::vector<std::shared_ptr<AudioPlug>>& getIsoOutputPlugs() const { return isoOutputPlugs_; }
    
    /**
     * @brief Get the list of external input plugs
     * @return const std::vector<std::shared_ptr<AudioPlug>>& List of external input plugs
     */
    const std::vector<std::shared_ptr<AudioPlug>>& getExternalInputPlugs() const { return externalInputPlugs_; }
    
    /**
     * @brief Get the list of external output plugs
     * @return const std::vector<std::shared_ptr<AudioPlug>>& List of external output plugs
     */
    const std::vector<std::shared_ptr<AudioPlug>>& getExternalOutputPlugs() const { return externalOutputPlugs_; }
    
    /**
     * @brief Get the list of music destination plugs
     * @return const std::vector<std::shared_ptr<AudioPlug>>& List of music destination plugs
     */
    const std::vector<std::shared_ptr<AudioPlug>>& getMusicDestPlugs() const { return musicDestPlugs_; }
    
    /**
     * @brief Get the list of music source plugs
     * @return const std::vector<std::shared_ptr<AudioPlug>>& List of music source plugs
     */
    const std::vector<std::shared_ptr<AudioPlug>>& getMusicSourcePlugs() const { return musicSourcePlugs_; }
    
    /**
     * @brief Set the status descriptor data
     * @param data Vector of status descriptor data
     */
    void setStatusDescriptorData(const std::vector<uint8_t>& data) { statusDescriptorData_ = data; }
    
    /**
     * @brief Get the status descriptor data
     * @return const std::optional<std::vector<uint8_t>>& Status descriptor data
     */
    const std::optional<std::vector<uint8_t>>& getStatusDescriptorData() const { return statusDescriptorData_; }
    
private:
    uint32_t isoInputCount_{0};     ///< Number of ISO input plugs
    uint32_t isoOutputCount_{0};    ///< Number of ISO output plugs
    uint32_t externalInputCount_{0};///< Number of external input plugs
    uint32_t externalOutputCount_{0};///< Number of external output plugs
    uint32_t musicDestPlugCount_{0};///< Number of music destination plugs
    uint32_t musicSourcePlugCount_{0};///< Number of music source plugs
    
    std::vector<std::shared_ptr<AudioPlug>> isoInputPlugs_;    ///< List of ISO input plugs
    std::vector<std::shared_ptr<AudioPlug>> isoOutputPlugs_;   ///< List of ISO output plugs
    std::vector<std::shared_ptr<AudioPlug>> externalInputPlugs_;///< List of external input plugs
    std::vector<std::shared_ptr<AudioPlug>> externalOutputPlugs_;///< List of external output plugs
    std::vector<std::shared_ptr<AudioPlug>> musicDestPlugs_;   ///< List of music destination plugs
    std::vector<std::shared_ptr<AudioPlug>> musicSourcePlugs_; ///< List of music source plugs
    
    std::optional<std::vector<uint8_t>> statusDescriptorData_; ///< Status descriptor data
};

/**
 * @brief Audio subunit implementation
 * 
 * Represents an audio subunit in a FireWire audio device, which handles
 * audio data processing and routing.
 */
class AudioSubunit {
public:
    AudioSubunit() = default;
    ~AudioSubunit() = default;
    
    /**
     * @brief Get audio destination plug count
     * @return uint32_t Number of audio destination plugs
     */
    uint32_t getAudioDestPlugCount() const { return audioDestPlugCount_; }
    
    /**
     * @brief Get audio source plug count
     * @return uint32_t Number of audio source plugs
     */
    uint32_t getAudioSourcePlugCount() const { return audioSourcePlugCount_; }
    
    /**
     * @brief Set audio destination plug count
     * @param count Number of audio destination plugs
     */
    void setAudioDestPlugCount(uint32_t count) { audioDestPlugCount_ = count; }
    
    /**
     * @brief Set audio source plug count
     * @param count Number of audio source plugs
     */
    void setAudioSourcePlugCount(uint32_t count) { audioSourcePlugCount_ = count; }
    
    /**
     * @brief Add an audio destination plug
     * @param plug Shared pointer to the audio destination plug
     */
    void addAudioDestPlug(std::shared_ptr<AudioPlug> plug) { audioDestPlugs_.push_back(plug); }
    
    /**
     * @brief Add an audio source plug
     * @param plug Shared pointer to the audio source plug
     */
    void addAudioSourcePlug(std::shared_ptr<AudioPlug> plug) { audioSourcePlugs_.push_back(plug); }
    
    /**
     * @brief Get the list of audio destination plugs
     * @return const std::vector<std::shared_ptr<AudioPlug>>& List of audio destination plugs
     */
    const std::vector<std::shared_ptr<AudioPlug>>& getAudioDestPlugs() const { return audioDestPlugs_; }
    
    /**
     * @brief Get the list of audio source plugs
     * @return const std::vector<std::shared_ptr<AudioPlug>>& List of audio source plugs
     */
    const std::vector<std::shared_ptr<AudioPlug>>& getAudioSourcePlugs() const { return audioSourcePlugs_; }
    
private:
    uint32_t audioDestPlugCount_{0};    ///< Number of audio destination plugs
    uint32_t audioSourcePlugCount_{0};  ///< Number of audio source plugs
    
    std::vector<std::shared_ptr<AudioPlug>> audioDestPlugs_;   ///< List of audio destination plugs
    std::vector<std::shared_ptr<AudioPlug>> audioSourcePlugs_; ///< List of audio source plugs
};

} // namespace FWA
