// include/FWA/DiceEAP.hpp
#pragma once

#include "FWA/DiceDefines.hpp"
#include "FWA/Error.h"
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <cstdint>
#include <expected>

namespace FWA
{
	// Forward declarations
	class DiceAudioDevice;
	class DiceEAP; // Forward declaration for circular dependency

	/**
	 * @brief Router configuration for DICE devices
	 *
	 * Manages the configuration of audio routing on DICE devices.
	 */
	class DiceRouterConfig
	{
	public:
		DiceRouterConfig(DiceEAP &eap);
		~DiceRouterConfig();

		/**
		 * @brief Read router configuration from device
		 * @param offset Base address offset within configuration space
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> read(uint32_t offset);

		/**
		 * @brief Write router configuration to device
		 * @param offset Base address offset within configuration space
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> write(uint32_t offset);

		/**
		 * @brief Clear all routes
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> clearRoutes();

		/**
		 * @brief Create a route between source and destination
		 * @param srcId Source ID
		 * @param dstId Destination ID
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> createRoute(unsigned char srcId, unsigned char dstId);

		/**
		 * @brief Set up a route between source and destination
		 *
		 * If a route with that destination exists, it will be replaced.
		 * If no route exists, a new one will be created.
		 *
		 * @param srcId Source ID
		 * @param dstId Destination ID
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> setupRoute(unsigned char srcId, unsigned char dstId);

		/**
		 * @brief Mute a route (set source to Muted)
		 * @param dstId Destination ID to mute
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> muteRoute(unsigned char dstId);

		/**
		 * @brief Remove a route with the specified destination
		 * @param dstId Destination ID to remove
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> removeRoute(unsigned char dstId);

		/**
		 * @brief Get the source for a given destination
		 * @param dstId Destination ID
		 * @return Source ID or error
		 */
		std::expected<unsigned char, IOKitError> getSourceForDestination(unsigned char dstId);

		/**
		 * @brief Get all destinations for a given source
		 * @param srcId Source ID
		 * @return Vector of destination IDs or error
		 */
		std::expected<std::vector<unsigned char>, IOKitError> getDestinationsForSource(unsigned char srcId);

		/**
		 * @brief Get the number of routes
		 * @return Number of routes
		 */
		size_t getNumRoutes() const { return routes_.size(); }

	private:
		DiceEAP &eap_;

		// Route map: destination -> source
		// Each destination can only have one source
		std::map<unsigned char, unsigned char> routes_;
	};

	/**
	 * @brief Peak meter handling for DICE devices
	 *
	 * Manages peak level measurements for different audio channels
	 */
	class DicePeakSpace
	{
	public:
		DicePeakSpace(DiceEAP &eap);
		~DicePeakSpace();

		/**
		 * @brief Read peak meters from device
		 * @param offset Base address offset within configuration space
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> read(uint32_t offset);

		/**
		 * @brief Get all peak values
		 * @return Map of destination ID to peak level or error
		 */
		std::expected<std::map<unsigned char, int>, IOKitError> getPeaks();

		/**
		 * @brief Get peak value for specific destination
		 * @param dstId Destination ID
		 * @return Peak level or error
		 */
		std::expected<int, IOKitError> getPeak(unsigned char dstId);

	private:
		DiceEAP &eap_;
		std::map<unsigned char, int> peaks_;
	};

	/**
	 * @brief Matrix mixer for DICE devices
	 *
	 * Provides volume control capability between inputs and outputs
	 */
	class DiceMixer
	{
	public:
		DiceMixer(DiceEAP &eap);
		~DiceMixer();

		/**
		 * @brief Initialize the mixer
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> init();

		/**
		 * @brief Load coefficients from device into local cache
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> loadCoefficients();

		/**
		 * @brief Store coefficients from cache to device
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> storeCoefficients();

		/**
		 * @brief Get number of input rows
		 * @return Number of inputs
		 */
		int getRowCount() const;

		/**
		 * @brief Get number of output columns
		 * @return Number of outputs
		 */
		int getColCount() const;

		/**
		 * @brief Check if a specific cell can be written to
		 * @param row Row index (input)
		 * @param col Column index (output)
		 * @return True if writable
		 */
		bool canWrite(int row, int col) const;

		/**
		 * @brief Set value for a specific cell
		 * @param row Row index (input)
		 * @param col Column index (output)
		 * @param value Value to set
		 * @return Actual value set
		 */
		std::expected<double, IOKitError> setValue(int row, int col, double value);

		/**
		 * @brief Get value for a specific cell
		 * @param row Row index (input)
		 * @param col Column index (output)
		 * @return Value or error
		 */
		std::expected<double, IOKitError> getValue(int row, int col);

		/**
		 * @brief Get row name
		 * @param row Row index
		 * @return Row name
		 */
		std::string getRowName(int row) const;

		/**
		 * @brief Get column name
		 * @param col Column index
		 * @return Column name
		 */
		std::string getColName(int col) const;

		/**
		 * @brief Update the source/destination names cache
		 */
		void updateNameCache();

	private:
		DiceEAP &eap_;
		std::vector<uint32_t> coefficients_; // Matrix mixer coefficients
	};

	/**
	 * @brief Stream configuration for DICE devices
	 *
	 * Manages audio stream configuration settings
	 */
	/**
	 * @brief Standalone configuration for DICE devices
	 *
	 * Manages settings for device operation when disconnected from computer
	 */
	class DiceStandaloneConfig
	{
	public:
		DiceStandaloneConfig(DiceEAP &eap);
		~DiceStandaloneConfig();

		/**
		 * @brief Read standalone configuration from device
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> read();

		/**
		 * @brief Write standalone configuration to device
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> write();

		/**
		 * @brief Get clock source
		 * @return Clock source value
		 */
		uint32_t getClockSource() const { return clockSrc_; }

		/**
		 * @brief Set clock source
		 * @param src Clock source value
		 */
		void setClockSource(uint32_t src) { clockSrc_ = src; }

		/**
		 * @brief Get AES external setting
		 * @return AES external setting
		 */
		uint32_t getAesExt() const { return aesExt_; }

		/**
		 * @brief Set AES external setting
		 * @param ext AES external setting
		 */
		void setAesExt(uint32_t ext) { aesExt_ = ext; }

		/**
		 * @brief Get ADAT external setting
		 * @return ADAT external setting
		 */
		uint32_t getAdatExt() const { return adatExt_; }

		/**
		 * @brief Set ADAT external setting
		 * @param ext ADAT external setting
		 */
		void setAdatExt(uint32_t ext) { adatExt_ = ext; }

		/**
		 * @brief Get word clock external setting
		 * @return Word clock external setting
		 */
		uint32_t getWcExt() const { return wcExt_; }

		/**
		 * @brief Set word clock external setting
		 * @param ext Word clock external setting
		 */
		void setWcExt(uint32_t ext) { wcExt_ = ext; }

		/**
		 * @brief Get internal/external setting
		 * @return Internal/external setting
		 */
		uint32_t getIntExt() const { return intExt_; }

		/**
		 * @brief Set internal/external setting
		 * @param ext Internal/external setting
		 */
		void setIntExt(uint32_t ext) { intExt_ = ext; }

	private:
		DiceEAP &eap_;
		uint32_t clockSrc_; // Current clock source
		uint32_t aesExt_;		// AES external setting
		uint32_t adatExt_;	// ADAT external setting
		uint32_t wcExt_;		// Word clock external setting
		uint32_t intExt_;		// Internal/external setting
	};

	/**
	 * @brief Stream configuration for DICE devices
	 *
	 * Manages audio stream configuration settings
	 */
	class DiceStreamConfig
	{
	public:
		/**
		 * @brief Configuration block structure
		 */
		struct ConfigBlock
		{
			uint32_t numAudio;
			uint32_t numMidi;
			uint32_t names[DICE_EAP_CHANNEL_CONFIG_NAMESTR_LEN_QUADS];
			uint32_t ac3Map;
		};

		DiceStreamConfig(DiceEAP &eap);
		~DiceStreamConfig();

		/**
		 * @brief Read stream configuration from device
		 * @param offset Base address offset within configuration space
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> read(uint32_t offset);

		/**
		 * @brief Write stream configuration to device
		 * @param offset Base address offset within configuration space
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> write(uint32_t offset);

		/**
		 * @brief Get transmit stream names
		 * @param index Stream index
		 * @return Vector of names or error
		 */
		std::expected<std::vector<std::string>, IOKitError> getTxNames(unsigned int index);

		/**
		 * @brief Get receive stream names
		 * @param index Stream index
		 * @return Vector of names or error
		 */
		std::expected<std::vector<std::string>, IOKitError> getRxNames(unsigned int index);

	private:
		DiceEAP &eap_;
		uint32_t numTx_;
		uint32_t numRx_;
		std::vector<ConfigBlock> txConfigs_;
		std::vector<ConfigBlock> rxConfigs_;

		std::vector<std::string> getNamesForBlock(const ConfigBlock &block);
	};

	/**
	 * @brief Extended Application Protocol for DICE devices
	 *
	 * The EAP provides advanced functionality for DICE devices, including
	 * router configuration, mixer controls, and standalone settings.
	 */
	class DiceEAP
	{
		friend class DiceMixer; // Allow DiceMixer to access private members

	public:
		/**
		 * @brief Command execution status
		 */
		enum class CommandStatus
		{
			Error,
			Timeout,
			Busy,
			Done
		};

		/**
		 * @brief EAP register base addresses
		 */
		enum class RegBase
		{
			Base,
			Capability,
			Command,
			Mixer,
			Peak,
			NewRouting,
			NewStreamCfg,
			CurrentCfg,
			Standalone,
			Application,
			None
		};

	public:
		/**
		 * @brief Construct a new DiceEAP object
		 * @param device Reference to the parent DiceAudioDevice
		 */
		DiceEAP(DiceAudioDevice &device);

		/**
		 * @brief Destroy the DiceEAP object
		 */
		~DiceEAP();

		/**
		 * @brief Check if this device supports EAP
		 * @param device Device to check
		 * @return true if EAP is supported
		 */
		static bool supportsEAP(DiceAudioDevice &device);

		/**
		 * @brief Initialize the EAP interface
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> init();

		/**
		 * @brief Update EAP state (after device changes)
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> update();

		/**
		 * @brief Load configuration from flash
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> loadFlashConfig();

		/**
		 * @brief Store configuration to flash
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> storeFlashConfig();

		/**
		 * @brief Check if a command is busy
		 * @return Command status
		 */
		CommandStatus operationBusy();

		/**
		 * @brief Wait for a command to complete
		 * @param maxWaitTimeMs Maximum wait time in milliseconds
		 * @return Command status
		 */
		CommandStatus waitForOperationEnd(int maxWaitTimeMs = 100);

		/**
		 * @brief Update configuration cache from device
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> updateConfigurationCache();

		/**
		 * @brief Get active router configuration
		 * @return Pointer to router configuration or nullptr
		 */
		DiceRouterConfig *getActiveRouterConfig();

		/**
		 * @brief Get active stream configuration
		 * @return Pointer to stream configuration or nullptr
		 */
		DiceStreamConfig *getActiveStreamConfig();

		/**
		 * @brief Get standalone configuration
		 * @return Pointer to standalone configuration or nullptr
		 */
		DiceStandaloneConfig *getStandaloneConfig() { return standaloneConfig_.get(); }

		/**
		 * @brief Set up default router configuration
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> setupDefaultRouterConfig();

		/**
		 * @brief Get transmit stream names
		 * @param index Stream index
		 * @return Vector of names or error
		 */
		std::expected<std::vector<std::string>, IOKitError> getTxNames(unsigned int index);

		/**
		 * @brief Get receive stream names
		 * @param index Stream index
		 * @return Vector of names or error
		 */
		std::expected<std::vector<std::string>, IOKitError> getRxNames(unsigned int index);

		/**
		 * @brief Read register from EAP space
		 * @param base Register base
		 * @param offset Register offset
		 * @param value Pointer to store read value
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> readReg(RegBase base, uint32_t offset, uint32_t *value);

		/**
		 * @brief Write register to EAP space
		 * @param base Register base
		 * @param offset Register offset
		 * @param value Value to write
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> writeReg(RegBase base, uint32_t offset, uint32_t value);

		/**
		 * @brief Read block of registers from EAP space
		 * @param base Register base
		 * @param offset Register offset
		 * @param data Buffer to store read values
		 * @param size Size of data to read in bytes
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> readRegBlock(RegBase base, uint32_t offset, uint32_t *data, size_t size);

		/**
		 * @brief Write block of registers to EAP space
		 * @param base Register base
		 * @param offset Register offset
		 * @param data Data to write
		 * @param size Size of data to write in bytes
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> writeRegBlock(RegBase base, uint32_t offset, const uint32_t *data, size_t size);

		/**
		 * @brief Get the human-readable name for a source ID and channel
		 * @param srcId Source ID
		 * @param channel Channel number
		 * @return Human-readable name string
		 */
		std::string getSourceName(DICE::RouteSource srcId, unsigned int channel);

		/**
		 * @brief Get the human-readable name for a destination ID and channel
		 * @param dstId Destination ID
		 * @param channel Channel number
		 * @return Human-readable name string
		 */
		std::string getDestinationName(DICE::RouteDestination dstId, unsigned int channel);

		/**
		 * @brief Get the current configuration
		 * @return Current configuration value
		 */
		int getCurrentConfig() const;

		/**
		 * @brief Read application space data from the device
		 *
		 * The application space is used for vendor-specific data and configuration.
		 * This method allows reading that data for inspection or customization.
		 *
		 * @param offset Offset within application space
		 * @param size Size of data to read
		 * @return Application data or error
		 */
		std::expected<std::vector<uint32_t>, IOKitError> readApplicationSpace(uint32_t offset, size_t size);

		/**
		 * @brief Helper method to execute an EAP command
		 * @param cmd Command to execute
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> commandHelper(uint32_t cmd);

	private:
		/**
		 * @brief Calculate register offset
		 * @param base Register base
		 * @param offset Register offset
		 * @param size Size of access
		 * @return Calculated address
		 */
		uint64_t offsetGen(RegBase base, uint32_t offset, size_t size);

		/**
		 * @brief Add a route
		 * @param srcId Source ID
		 * @param srcBase Source base index
		 * @param dstId Destination ID
		 * @param dstBase Destination base index
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> addRoute(
				DICE::RouteSource srcId, unsigned int srcBase,
				DICE::RouteDestination dstId, unsigned int dstBase);

		/**
		 * @brief Set up default router config for low rates (32k-48k)
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> setupDefaultRouterConfigLow();

		/**
		 * @brief Set up default router config for mid rates (88.2k-96k)
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> setupDefaultRouterConfigMid();

		/**
		 * @brief Set up default router config for high rates (176.4k-192k)
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> setupDefaultRouterConfigHigh();

	private:
		DiceAudioDevice &device_;

		// Router configuration
		bool routerExposed_;
		bool routerReadonly_;
		bool routerFlashstored_;
		uint16_t routerNumEntries_;

		// Mixer configuration
		bool mixerExposed_;
		bool mixerReadonly_;
		bool mixerFlashstored_;
		uint8_t mixerTxId_;
		uint8_t mixerRxId_;
		uint8_t mixerNumTx_;
		uint8_t mixerNumRx_;

		// General capabilities
		bool generalSupportDynstream_;
		bool generalSupportFlash_;
		bool generalPeakEnabled_;
		uint8_t generalMaxTx_;
		uint8_t generalMaxRx_;
		bool generalStreamCfgStored_;
		uint16_t generalChip_;

		// EAP space offsets and sizes
		uint32_t capabilityOffset_;
		uint32_t capabilitySize_;
		uint32_t cmdOffset_;
		uint32_t cmdSize_;
		uint32_t mixerOffset_;
		uint32_t mixerSize_;
		uint32_t peakOffset_;
		uint32_t peakSize_;
		uint32_t newRoutingOffset_;
		uint32_t newRoutingSize_;
		uint32_t newStreamCfgOffset_;
		uint32_t newStreamCfgSize_;
		uint32_t currCfgOffset_;
		uint32_t currCfgSize_;
		uint32_t standaloneOffset_;
		uint32_t standaloneSize_;
		uint32_t appOffset_;
		uint32_t appSize_;

		// Configuration objects
		std::unique_ptr<DiceRouterConfig> currCfgRoutingLow_;
		std::unique_ptr<DiceRouterConfig> currCfgRoutingMid_;
		std::unique_ptr<DiceRouterConfig> currCfgRoutingHigh_;
		std::unique_ptr<DiceStreamConfig> currCfgStreamLow_;
		std::unique_ptr<DiceStreamConfig> currCfgStreamMid_;
		std::unique_ptr<DiceStreamConfig> currCfgStreamHigh_;
		std::unique_ptr<DicePeakSpace> peakSpace_;
		std::unique_ptr<DiceStandaloneConfig> standaloneConfig_;
		std::unique_ptr<DiceMixer> mixer_;
	};

} // namespace FWA
