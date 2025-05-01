// include/FWA/DiceAudioDevice.h
#pragma once

#include "FWA/AudioDevice.h"
#include "FWA/Error.h"
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <expected>

namespace FWA
{

	class DiceEAP;		// Forward declaration for Extended Application Protocol
	class DiceRouter; // Forward declaration for Router Interface

	/**
	 * @brief Specialized AudioDevice class for DICE-based FireWire audio interfaces
	 *
	 * @note The DiceEAP class is declared as a friend to allow direct access
	 * to register operations for better performance when implementing the EAP.
	 * This class extends the base AudioDevice to provide support for devices
	 * using the DICE chipset from TC Applied Technologies (TCAT).
	 */
	class DiceAudioDevice : public AudioDevice
	{
		friend class DiceEAP; // Allow DiceEAP to access private methods
	public:
		/**
		 * @brief Construct a new DICE Audio Device
		 * @param guid Global Unique Identifier for the device
		 * @param deviceName Name of the device
		 * @param vendorName Name of the device vendor
		 * @param avcUnit IOKit service representing the AVC unit
		 * @param deviceController Pointer to the device controller
		 */
		DiceAudioDevice(std::uint64_t guid,
										const std::string &deviceName,
										const std::string &vendorName,
										io_service_t avcUnit,
										DeviceController *deviceController);

		/**
		 * @brief Destroy the DICE Audio Device
		 */
		~DiceAudioDevice();

		/**
		 * @brief Initialize the device after construction
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> init();

		/**
		 * @brief Discover the capabilities of this device
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> discoverCapabilities();

		/**
		 * @brief Check if this device uses a DICE JR chipset
		 * @return true if the device uses DICE JR, false otherwise
		 */
		bool isDiceJr() const;

		/**
		 * @brief Get the DICE chip type
		 * @return The DICE chip type (0=DiceII, 1=DiceMini, 2=DiceJr, 255=Unknown)
		 */
		int getChipType() const;

		/**
		 * @brief Get the current sample rate configuration
		 * @return The current configuration (0=Unknown, 1=Low, 2=Mid, 3=High)
		 */
		int getCurrentConfig(); // Removed const

		/**
		 * @brief Get the sample rate
		 * @return The current sample rate in Hz or an error
		 */
		std::expected<int, IOKitError> getSampleRate();

		/**
		 * @brief Set the sample rate
		 * @param sampleRate The desired sample rate in Hz
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> setSampleRate(int sampleRate);

		/**
		 * @brief Get supported sample rates
		 * @return Vector of supported sample rates in Hz or an error
		 */
		std::expected<std::vector<int>, IOKitError> getSupportedSampleRates();

		/**
		 * @brief Get the nickname of the device
		 * @return The device nickname or an error
		 */
		std::expected<std::string, IOKitError> getNickname();

		/**
		 * @brief Set the nickname of the device
		 * @param name The new nickname
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> setNickname(const std::string &name);

		/**
		 * @brief Enable isochronous streaming
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> enableIsoStreaming();

		/**
		 * @brief Disable isochronous streaming
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> disableIsoStreaming();

		/**
		 * @brief Check if isochronous streaming is enabled
		 * @return true if streaming is enabled, false otherwise, or an error
		 */
		std::expected<bool, IOKitError> isIsoStreamingEnabled();

		/**
		 * @brief Get access to the Extended Application Protocol interface
		 * @return Pointer to the EAP interface or nullptr if not supported
		 */
		DiceEAP *getEAP() const { return eap_.get(); }

		/**
		 * @brief Get access to the Router interface
		 * @return Pointer to the Router interface or nullptr if not supported
		 */
		DiceRouter *getRouter() const { return router_.get(); }

		/**
		 * @brief Lock the device for exclusive access.
		 * @return Success or error status.
		 */
		std::expected<void, IOKitError> lock();

		/**
		 * @brief Unlock the device, releasing exclusive access.
		 * @return Success or error status.
		 */
		std::expected<void, IOKitError> unlock();

	private:
		// DICE register access methods
		std::expected<uint32_t, IOKitError> readReg(uint64_t offset);
		std::expected<void, IOKitError> writeReg(uint64_t offset, uint32_t data);
		std::expected<std::vector<uint32_t>, IOKitError> readRegBlock(uint64_t offset, size_t length);
		std::expected<void, IOKitError> writeRegBlock(uint64_t offset, const uint32_t *data, size_t length);

		std::expected<uint32_t, IOKitError> readGlobalReg(uint32_t offset);
		std::expected<void, IOKitError> writeGlobalReg(uint32_t offset, uint32_t data);
		std::expected<std::vector<uint32_t>, IOKitError> readGlobalRegBlock(uint32_t offset, size_t length);
		std::expected<void, IOKitError> writeGlobalRegBlock(uint32_t offset, const uint32_t *data, size_t length);
		uint64_t globalOffsetGen(uint32_t offset, size_t length);

		std::expected<uint32_t, IOKitError> readTxReg(unsigned int i, uint32_t offset);
		std::expected<void, IOKitError> writeTxReg(unsigned int i, uint32_t offset, uint32_t data);
		std::expected<std::vector<uint32_t>, IOKitError> readTxRegBlock(unsigned int i, uint32_t offset, size_t length);
		std::expected<void, IOKitError> writeTxRegBlock(unsigned int i, uint32_t offset, const uint32_t *data, size_t length);
		uint64_t txOffsetGen(unsigned int i, uint32_t offset, size_t length);

		std::expected<uint32_t, IOKitError> readRxReg(unsigned int i, uint32_t offset);
		std::expected<void, IOKitError> writeRxReg(unsigned int i, uint32_t offset, uint32_t data);
		std::expected<std::vector<uint32_t>, IOKitError> readRxRegBlock(unsigned int i, uint32_t offset, size_t length);
		std::expected<void, IOKitError> writeRxRegBlock(unsigned int i, uint32_t offset, const uint32_t *data, size_t length);
		uint64_t rxOffsetGen(unsigned int i, uint32_t offset, size_t length);

		// Helper methods
		std::expected<void, IOKitError> initIoFunctions();
		std::expected<bool, IOKitError> maskedCheckZeroGlobalReg(uint32_t offset, uint32_t mask);
		std::expected<bool, IOKitError> maskedCheckNotZeroGlobalReg(uint32_t offset, uint32_t mask);
		std::vector<std::string> splitNameString(const std::string &in);
		std::vector<std::string> getTxNameString(unsigned int i);
		std::vector<std::string> getRxNameString(unsigned int i);
		std::vector<std::string> getClockSourceNameString();

		// DICE register offsets and sizes
		uint32_t m_global_reg_offset;
		uint32_t m_global_reg_size;
		uint32_t m_tx_reg_offset;
		uint32_t m_tx_reg_size;
		uint32_t m_rx_reg_offset;
		uint32_t m_rx_reg_size;
		uint32_t m_unused1_reg_offset;
		uint32_t m_unused1_reg_size;
		uint32_t m_unused2_reg_offset;
		uint32_t m_unused2_reg_size;

		// DICE transmit/receive configuration
		uint32_t m_nb_tx;
		uint32_t m_tx_size;
		uint32_t m_nb_rx;
		uint32_t m_rx_size;

		// DICE chip type
		int chipType_;

		// Extended Application Protocol interface
		std::unique_ptr<DiceEAP> eap_;

		// Router interface for configuring device routing
		std::unique_ptr<DiceRouter> router_;

		// Notifier for device events
		io_object_t notifier_;
	};

} // namespace FWA
