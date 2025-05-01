// src/FWA/DiceAudioDevice.cpp
#include "FWA/DiceAudioDevice.h"
#include "FWA/DiceDefines.hpp"
#include "FWA/DiceEAP.hpp"
#include "FWA/DiceRouter.hpp"
#include "FWA/DeviceController.h"
#include <spdlog/spdlog.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/firewire/IOFireWireLib.h>
#include <IOKit/avc/IOFireWireAVCLib.h>
#include <algorithm>
#include <cstring>
#include <unistd.h> // Added for usleep
#include <vector>		// Added for std::vector
#include <sstream>	// Added for std::stringstream

// Removed incorrect helper functions from anonymous namespace

namespace FWA
{

	// Forward declarations are in header file

	DiceAudioDevice::DiceAudioDevice(std::uint64_t guid,
																	 const std::string &deviceName,
																	 const std::string &vendorName,
																	 io_service_t avcUnit,
																	 DeviceController *deviceController)
			: AudioDevice(guid, deviceName, vendorName, avcUnit, deviceController),
				m_global_reg_offset(0xFFFFFFFF),
				m_global_reg_size(0xFFFFFFFF),
				m_tx_reg_offset(0xFFFFFFFF),
				m_tx_reg_size(0xFFFFFFFF),
				m_rx_reg_offset(0xFFFFFFFF),
				m_rx_reg_size(0xFFFFFFFF),
				m_unused1_reg_offset(0xFFFFFFFF),
				m_unused1_reg_size(0xFFFFFFFF),
				m_unused2_reg_offset(0xFFFFFFFF),
				m_unused2_reg_size(0xFFFFFFFF),
				m_nb_tx(0xFFFFFFFF),
				m_tx_size(0xFFFFFFFF),
				m_nb_rx(0xFFFFFFFF),
				m_rx_size(0xFFFFFFFF),
				chipType_(static_cast<int>(DICE::DiceChipType::Unknown)),
				notifier_(0)
	{
		spdlog::info("Created DiceAudioDevice (GUID: 0x%llx)", guid);
	}

	DiceAudioDevice::~DiceAudioDevice()
	{
		spdlog::info("Destroying DiceAudioDevice (GUID: 0x%llx)", getGuid());

		// Clean up notifier if it exists
		if (notifier_)
		{
			IOObjectRelease(notifier_);
			notifier_ = 0;
		}
	}

	std::expected<void, IOKitError> DiceAudioDevice::init()
	{
		spdlog::info("Initializing DiceAudioDevice (GUID: 0x%llx)", getGuid());

		// Call base class init first
		std::expected<void, IOKitError> baseResult = AudioDevice::init();
		if (!baseResult)
		{
			spdlog::error("Base AudioDevice initialization failed: %d", static_cast<int>(baseResult.error()));
			return std::unexpected(baseResult.error());
		}

		// Initialize DICE I/O functions
		std::expected<void, IOKitError> ioFuncResult = initIoFunctions();
		if (!ioFuncResult)
		{
			spdlog::error("Failed to initialize DICE I/O functions: {}", static_cast<int>(ioFuncResult.error()));
			return std::unexpected(ioFuncResult.error());
		}

		// Detect DICE chip type
		std::expected<uint32_t, IOKitError> generalCapResult = readGlobalReg(DICE_EAP_CAPABILITY_GENERAL);
		if (generalCapResult)
		{
			uint32_t generalCap = generalCapResult.value();
			int chipTypeValue = (generalCap >> DICE_EAP_CAP_GENERAL_CHIP) & 0xFF;
			chipType_ = chipTypeValue;

			switch (static_cast<DICE::DiceChipType>(chipTypeValue))
			{
			case DICE::DiceChipType::DiceII:
				spdlog::info("Detected DICE II chipset");
				break;
			case DICE::DiceChipType::DiceMini:
				spdlog::info("Detected DICE Mini chipset");
				break;
			case DICE::DiceChipType::DiceJr:
				spdlog::info("Detected DICE JR chipset");
				break;
			default:
				spdlog::info("Unknown DICE chipset type: %d", chipTypeValue);
				break;
			}
		}
		else
		{
			spdlog::warn("Could not determine DICE chip type");
		}

		// Initialize EAP if available
		if (DiceEAP::supportsEAP(*this))
		{
			eap_ = std::make_unique<DiceEAP>(*this);
			auto eapInit = eap_->init();
			if (!eapInit)
			{
				spdlog::warn("Could not initialize EAP interface: {}", static_cast<int>(eapInit.error()));
				eap_.reset();
			}
			else
			{
				spdlog::info("EAP interface initialized successfully");

				// Initialize the router if EAP was successful
				router_ = std::make_unique<DiceRouter>(*eap_);
				spdlog::info("Router interface initialized");
			}
		}
		else
		{
			spdlog::debug("This device does not support EAP");
		}

		return {};
	}

	std::expected<void, IOKitError> DiceAudioDevice::discoverCapabilities()
	{
		spdlog::info("Discovering capabilities for DiceAudioDevice (GUID: 0x%llx)", getGuid());

		// Call base class method first
		std::expected<void, IOKitError> baseResult = AudioDevice::discoverCapabilities();
		if (!baseResult)
		{
			spdlog::error("Base AudioDevice capability discovery failed: %d", static_cast<int>(baseResult.error()));
			return std::unexpected(baseResult.error());
		}

		// Read DICE-specific capabilities

		return {};
	}

	bool DiceAudioDevice::isDiceJr() const
	{
		return chipType_ == static_cast<int>(DICE::DiceChipType::DiceJr);
	}

	int DiceAudioDevice::getChipType() const // This one remains const as it doesn't do I/O
	{
		return chipType_;
	}

	int DiceAudioDevice::getCurrentConfig() // Removed const
	{
		std::expected<int, IOKitError> sampleRateResult = getSampleRate();
		if (!sampleRateResult)
		{
			spdlog::error("Could not get sample rate to determine current config");
			return static_cast<int>(DICE::DiceConfig::Unknown);
		}
		int sampleRate = sampleRateResult.value();

		if (sampleRate > 31999 && sampleRate <= 48000)
		{
			return static_cast<int>(DICE::DiceConfig::Low);
		}
		if (sampleRate > 48000 && sampleRate <= 96000)
		{
			return static_cast<int>(DICE::DiceConfig::Mid);
		}
		if (sampleRate > 96000 && sampleRate <= 192000)
		{
			return static_cast<int>(DICE::DiceConfig::High);
		}
		return static_cast<int>(DICE::DiceConfig::Unknown);
	}

	std::expected<int, IOKitError> DiceAudioDevice::getSampleRate()
	{
		std::expected<uint32_t, IOKitError> clockRegResult = readGlobalReg(DICE_REGISTER_GLOBAL_CLOCK_SELECT);
		if (!clockRegResult)
		{
			spdlog::error("Could not read CLOCK_SELECT register");
			return std::unexpected(clockRegResult.error());
		}
		uint32_t clockReg = clockRegResult.value();
		uint8_t rateValue = DICE_GET_RATE(clockReg);

		switch (rateValue)
		{
		case DICE_RATE_32K:
			return 32000;
		case DICE_RATE_44K1:
			return 44100;
		case DICE_RATE_48K:
			return 48000;
		case DICE_RATE_88K2:
			return 88200;
		case DICE_RATE_96K:
			return 96000;
		case DICE_RATE_176K4:
			return 176400;
		case DICE_RATE_192K:
			return 192000;
		default:
			spdlog::error("Unknown sample rate value: %d", rateValue);
			return std::unexpected(IOKitError::Unsupported);
		}
	}

	// Restore missing method signature
	std::expected<void, IOKitError> DiceAudioDevice::setSampleRate(int sampleRate)
	{
		spdlog::info("Setting sample rate to %d Hz", sampleRate);

		bool supported = false;
		uint32_t select = 0;

		switch (sampleRate)
		{
		case 32000:
		{
			std::expected<bool, FWA::IOKitError> supportedResult = maskedCheckNotZeroGlobalReg(
					DICE_REGISTER_GLOBAL_CLOCKCAPABILITIES,
					DICE_CLOCKCAP_RATE_32K);
			if (!supportedResult)
			{
				spdlog::error("Failed to check 32k support: %d", static_cast<int>(supportedResult.error()));
				return std::unexpected(supportedResult.error());
			}
			supported = supportedResult.value();
			select = DICE_RATE_32K;
			break;
		}
		case 44100:
		{
			std::expected<bool, FWA::IOKitError> supportedResult = maskedCheckNotZeroGlobalReg(
					DICE_REGISTER_GLOBAL_CLOCKCAPABILITIES,
					DICE_CLOCKCAP_RATE_44K1);
			if (!supportedResult)
			{
				spdlog::error("Failed to check 44.1k support: %d", static_cast<int>(supportedResult.error()));
				return std::unexpected(supportedResult.error());
			}
			supported = supportedResult.value();
			select = DICE_RATE_44K1;
			break;
		}
		case 48000:
		{
			std::expected<bool, FWA::IOKitError> supportedResult = maskedCheckNotZeroGlobalReg(
					DICE_REGISTER_GLOBAL_CLOCKCAPABILITIES,
					DICE_CLOCKCAP_RATE_48K);
			if (!supportedResult)
			{
				spdlog::error("Failed to check 48k support: %d", static_cast<int>(supportedResult.error()));
				return std::unexpected(supportedResult.error());
			}
			supported = supportedResult.value();
			select = DICE_RATE_48K;
			break;
		}
		case 88200:
		{
			std::expected<bool, FWA::IOKitError> supportedResult = maskedCheckNotZeroGlobalReg(
					DICE_REGISTER_GLOBAL_CLOCKCAPABILITIES,
					DICE_CLOCKCAP_RATE_88K2);
			if (!supportedResult)
			{
				spdlog::error("Failed to check 88.2k support: %d", static_cast<int>(supportedResult.error()));
				return std::unexpected(supportedResult.error());
			}
			supported = supportedResult.value();
			select = DICE_RATE_88K2;
			break;
		}
		case 96000:
		{
			std::expected<bool, FWA::IOKitError> supportedResult = maskedCheckNotZeroGlobalReg(
					DICE_REGISTER_GLOBAL_CLOCKCAPABILITIES,
					DICE_CLOCKCAP_RATE_96K);
			if (!supportedResult)
			{
				spdlog::error("Failed to check 96k support: %d", static_cast<int>(supportedResult.error()));
				return std::unexpected(supportedResult.error());
			}
			supported = supportedResult.value();
			select = DICE_RATE_96K;
			break;
		}
		case 176400:
		{
			std::expected<bool, FWA::IOKitError> supportedResult = maskedCheckNotZeroGlobalReg(
					DICE_REGISTER_GLOBAL_CLOCKCAPABILITIES,
					DICE_CLOCKCAP_RATE_176K4);
			if (!supportedResult)
			{
				spdlog::error("Failed to check 176.4k support: %d", static_cast<int>(supportedResult.error()));
				return std::unexpected(supportedResult.error());
			}
			supported = supportedResult.value();
			select = DICE_RATE_176K4;
			break;
		}
		case 192000:
		{
			std::expected<bool, FWA::IOKitError> supportedResult = maskedCheckNotZeroGlobalReg(
					DICE_REGISTER_GLOBAL_CLOCKCAPABILITIES,
					DICE_CLOCKCAP_RATE_192K);
			if (!supportedResult)
			{
				spdlog::error("Failed to check 192k support: %d", static_cast<int>(supportedResult.error()));
				return std::unexpected(supportedResult.error());
			}
			supported = supportedResult.value();
			select = DICE_RATE_192K;
			break;
		}
		default:
			supported = false;
			break;
		}

		if (!supported)
		{
			spdlog::error("Sample rate %d Hz is not supported by this device", sampleRate);
			return std::unexpected(IOKitError::Unsupported);
		}

		auto isStreamingEnabledResult = isIsoStreamingEnabled();
		if (!isStreamingEnabledResult)
		{
			spdlog::error("Could not check if streaming is enabled");
			return std::unexpected(isStreamingEnabledResult.error());
		}
		if (isStreamingEnabledResult.value())
		{
			spdlog::error("Cannot change sample rate while streaming is enabled");
			return std::unexpected(IOKitError::Busy);
		}

		auto clockRegResult = readGlobalReg(DICE_REGISTER_GLOBAL_CLOCK_SELECT);
		if (!clockRegResult)
		{
			spdlog::error("Could not read CLOCK_SELECT register");
			return std::unexpected(clockRegResult.error());
		}
		uint32_t clockReg = clockRegResult.value();

		clockReg = DICE_SET_RATE(clockReg, select);

		std::expected<void, IOKitError> writeResult = writeGlobalReg(DICE_REGISTER_GLOBAL_CLOCK_SELECT, clockReg);
		if (!writeResult)
		{
			spdlog::error("Could not write CLOCK_SELECT register");
			return std::unexpected(writeResult.error());
		}

		// Verify the write succeeded
		auto verifyRegResult = readGlobalReg(DICE_REGISTER_GLOBAL_CLOCK_SELECT);
		if (!verifyRegResult)
		{
			spdlog::error("Could not read CLOCK_SELECT register for verification");
			return std::unexpected(verifyRegResult.error());
		}
		uint32_t verifyReg = verifyRegResult.value();

		if (clockReg != verifyReg)
		{
			spdlog::error("Sample rate register write failed");
			return std::unexpected(IOKitError::Error);
		}

		// Wait for the device to lock to the new sample rate
		auto statusRegResult = readGlobalReg(DICE_REGISTER_GLOBAL_STATUS);
		if (!statusRegResult)
		{
			spdlog::error("Could not read GLOBAL_STATUS register");
			return std::unexpected(statusRegResult.error());
		}
		uint32_t statusReg = statusRegResult.value();

		int attempts = 0;
		while (((statusReg & 0x1) == 0 || ((clockReg >> 8) & 0xFF) != ((statusReg >> 8) & 0xFF)) && attempts < 20)
		{
			usleep(100000); // 100ms
			statusRegResult = readGlobalReg(DICE_REGISTER_GLOBAL_STATUS);
			if (!statusRegResult)
			{
				spdlog::error("Could not read GLOBAL_STATUS register during wait");
				return std::unexpected(statusRegResult.error());
			}
			statusReg = statusRegResult.value();
			attempts++;
		}

		if (attempts == 20)
		{
			spdlog::warn("Device did not lock to the new sample rate within timeout");
		}

		// Update EAP if available
		if (eap_)
		{
			auto eapUpdate = eap_->update();
			if (!eapUpdate)
			{
				spdlog::warn("Failed to update EAP: {}", static_cast<int>(eapUpdate.error()));
			}
		}

		return {};
	}

	// Removed const from definition AGAIN and removed incorrect switch block
	std::expected<std::vector<int>, IOKitError> DiceAudioDevice::getSupportedSampleRates()
	{
		std::vector<int> rates;

		std::expected<bool, IOKitError> check32k = maskedCheckNotZeroGlobalReg(DICE_REGISTER_GLOBAL_CLOCKCAPABILITIES, DICE_CLOCKCAP_RATE_32K);
		if (!check32k)
			return std::unexpected(check32k.error());
		if (check32k.value())
		{
			rates.push_back(32000);
		}

		std::expected<bool, IOKitError> check44k1 = maskedCheckNotZeroGlobalReg(DICE_REGISTER_GLOBAL_CLOCKCAPABILITIES, DICE_CLOCKCAP_RATE_44K1);
		if (!check44k1)
			return std::unexpected(check44k1.error());
		if (check44k1.value())
		{
			rates.push_back(44100);
		}

		std::expected<bool, IOKitError> check48k = maskedCheckNotZeroGlobalReg(DICE_REGISTER_GLOBAL_CLOCKCAPABILITIES, DICE_CLOCKCAP_RATE_48K);
		if (!check48k)
			return std::unexpected(check48k.error());
		if (check48k.value())
		{
			rates.push_back(48000);
		}

		std::expected<bool, IOKitError> check88k2 = maskedCheckNotZeroGlobalReg(DICE_REGISTER_GLOBAL_CLOCKCAPABILITIES, DICE_CLOCKCAP_RATE_88K2);
		if (!check88k2)
			return std::unexpected(check88k2.error());
		if (check88k2.value())
		{
			rates.push_back(88200);
		}

		std::expected<bool, IOKitError> check96k = maskedCheckNotZeroGlobalReg(DICE_REGISTER_GLOBAL_CLOCKCAPABILITIES, DICE_CLOCKCAP_RATE_96K);
		if (!check96k)
			return std::unexpected(check96k.error());
		if (check96k.value())
		{
			rates.push_back(96000);
		}

		std::expected<bool, IOKitError> check176k4 = maskedCheckNotZeroGlobalReg(DICE_REGISTER_GLOBAL_CLOCKCAPABILITIES, DICE_CLOCKCAP_RATE_176K4);
		if (!check176k4)
			return std::unexpected(check176k4.error());
		if (check176k4.value())
		{
			rates.push_back(176400);
		}

		std::expected<bool, IOKitError> check192k = maskedCheckNotZeroGlobalReg(DICE_REGISTER_GLOBAL_CLOCKCAPABILITIES, DICE_CLOCKCAP_RATE_192K);
		if (!check192k)
			return std::unexpected(check192k.error());
		if (check192k.value())
		{
			rates.push_back(192000);
		}

		return rates;
	}

	std::expected<std::string, IOKitError> DiceAudioDevice::getNickname()
	{
		std::expected<std::vector<uint32_t>, IOKitError> nameStringResult = readGlobalRegBlock(DICE_REGISTER_GLOBAL_NICK_NAME, DICE_NICK_NAME_SIZE);
		if (!nameStringResult)
		{
			spdlog::error("Could not read nickname string");
			return std::unexpected(nameStringResult.error());
		}

		std::vector<uint32_t> nameData = nameStringResult.value();
		char nameString[DICE_NICK_NAME_SIZE + 1];
		std::memcpy(nameString, nameData.data(), DICE_NICK_NAME_SIZE);

// Strings from the device are always little-endian
#if __BYTE_ORDER == __BIG_ENDIAN
		// Swap bytes for big-endian systems
		for (size_t i = 0; i < DICE_NICK_NAME_SIZE / 4; i++)
		{
			uint32_t *word = reinterpret_cast<uint32_t *>(nameString) + i;
			*word = CFSwapInt32LittleToHost(*word);
		}
#endif

		nameString[DICE_NICK_NAME_SIZE] = '\0';
		return std::string(nameString);
	}

	std::expected<void, IOKitError> DiceAudioDevice::setNickname(const std::string &name)
	{
		// Create a vector of uint32_t with the right size (in bytes) to hold the string data
		std::vector<uint32_t> nameData(DICE_NICK_NAME_SIZE / sizeof(uint32_t));

		// Clear the memory first
		std::memset(nameData.data(), 0, DICE_NICK_NAME_SIZE);

		// Copy the string data (respecting null termination)
		std::strncpy(reinterpret_cast<char *>(nameData.data()), name.c_str(), DICE_NICK_NAME_SIZE - 1);

// Strings to the device must be little-endian
#if __BYTE_ORDER == __BIG_ENDIAN
		// Swap bytes for big-endian systems
		for (size_t i = 0; i < nameData.size(); i++)
		{
			nameData[i] = CFSwapInt32HostToLittle(nameData[i]);
		}
#endif

		std::expected<void, IOKitError> writeResult = writeGlobalRegBlock(
				DICE_REGISTER_GLOBAL_NICK_NAME,
				nameData.data(),
				DICE_NICK_NAME_SIZE);
		if (!writeResult)
		{
			spdlog::error("Could not write nickname string");
			return std::unexpected(writeResult.error());
		}

		return {};
	}

	std::expected<void, IOKitError> DiceAudioDevice::enableIsoStreaming()
	{
		std::expected<void, IOKitError> writeResult = writeGlobalReg(DICE_REGISTER_GLOBAL_ENABLE, DICE_ISOSTREAMING_ENABLE);
		if (!writeResult)
		{
			spdlog::error("Could not enable isochronous streaming");
			return std::unexpected(writeResult.error());
		}
		return {};
	}

	std::expected<void, IOKitError> DiceAudioDevice::disableIsoStreaming()
	{
		std::expected<void, IOKitError> writeResult = writeGlobalReg(DICE_REGISTER_GLOBAL_ENABLE, DICE_ISOSTREAMING_DISABLE);
		if (!writeResult)
		{
			spdlog::error("Could not disable isochronous streaming");
			return std::unexpected(writeResult.error());
		}
		return {};
	}

	std::expected<bool, IOKitError> DiceAudioDevice::isIsoStreamingEnabled()
	{
		std::expected<uint32_t, IOKitError> enableRegResult = readGlobalReg(DICE_REGISTER_GLOBAL_ENABLE);
		if (!enableRegResult)
		{
			spdlog::error("Could not read ENABLE register");
			return std::unexpected(enableRegResult.error());
		}
		uint32_t enableReg = enableRegResult.value();
		return (enableReg != DICE_ISOSTREAMING_DISABLE);
	}

	std::expected<bool, IOKitError> DiceAudioDevice::maskedCheckZeroGlobalReg(uint32_t offset, uint32_t mask)
	{
		std::expected<uint32_t, IOKitError> result = readGlobalReg(offset);
		if (!result)
		{
			spdlog::error("Could not read global register at offset 0x{:x} for masked check", offset);
			return std::unexpected(result.error());
		}
		return ((result.value() & mask) == 0);
	}

	std::expected<bool, IOKitError> DiceAudioDevice::maskedCheckNotZeroGlobalReg(uint32_t offset, uint32_t mask)
	{
		std::expected<bool, IOKitError> result = maskedCheckZeroGlobalReg(offset, mask);
		if (!result)
		{
			return std::unexpected(result.error());
		}
		return !result.value();
	}

	uint64_t DiceAudioDevice::globalOffsetGen(uint32_t offset, size_t length)
	{
		// Registry offsets should always be smaller than 0x7FFFFFFF
		// because otherwise base + offset > 64bit
		if (m_global_reg_offset & 0x80000000)
		{
			spdlog::error("Register offset not initialized yet");
			return DICE_INVALID_OFFSET;
		}
		// Out-of-range check
		if (offset + length > m_global_reg_offset + m_global_reg_size)
		{
			spdlog::error("Register offset+length too large: 0x{:04X}", offset + length);
			return DICE_INVALID_OFFSET;
		}

		return offset;
	}

	uint64_t DiceAudioDevice::txOffsetGen(unsigned int i, uint32_t offset, size_t length)
	{
		// Registry offsets should always be smaller than 0x7FFFFFFF
		// because otherwise base + offset > 64bit
		if (m_tx_reg_offset & 0x80000000)
		{
			spdlog::error("Register offset not initialized yet");
			return DICE_INVALID_OFFSET;
		}
		if (m_nb_tx & 0x80000000)
		{
			spdlog::error("m_nb_tx not initialized yet");
			return DICE_INVALID_OFFSET;
		}
		if (m_tx_size & 0x80000000)
		{
			spdlog::error("m_tx_size not initialized yet");
			return DICE_INVALID_OFFSET;
		}
		if (i >= m_nb_tx)
		{
			spdlog::error("TX index out of range");
			return DICE_INVALID_OFFSET;
		}

		uint64_t offset_tx = DICE_REGISTER_TX_PARAM(m_tx_size, i, offset);

		// Out-of-range check (Corrected: compare against end of TX parameter space)
		if (m_tx_reg_offset + offset_tx + length > m_tx_reg_offset + m_tx_reg_size)
		{
			spdlog::error("TX Register offset+length out of bounds: base=0x{:X}, offset=0x{:X}, length={}, total_size=0x{:X}",
										m_tx_reg_offset, offset_tx, length, m_tx_reg_size);
			return DICE_INVALID_OFFSET;
		}

		return offset_tx;
	}

	uint64_t DiceAudioDevice::rxOffsetGen(unsigned int i, uint32_t offset, size_t length)
	{
		// Registry offsets should always be smaller than 0x7FFFFFFF
		// because otherwise base + offset > 64bit
		if (m_rx_reg_offset & 0x80000000)
		{
			spdlog::error("Register offset not initialized yet");
			return DICE_INVALID_OFFSET;
		}
		if (m_nb_rx & 0x80000000)
		{
			spdlog::error("m_nb_rx not initialized yet");
			return DICE_INVALID_OFFSET;
		}
		if (m_rx_size & 0x80000000)
		{
			spdlog::error("m_rx_size not initialized yet");
			return DICE_INVALID_OFFSET;
		}
		if (i >= m_nb_rx)
		{
			spdlog::error("RX index out of range");
			return DICE_INVALID_OFFSET;
		}

		uint64_t offset_rx = DICE_REGISTER_RX_PARAM(m_rx_size, i, offset);

		// Out-of-range check (Corrected: compare against end of RX parameter space)
		if (m_rx_reg_offset + offset_rx + length > m_rx_reg_offset + m_rx_reg_size)
		{
			spdlog::error("RX Register offset+length out of bounds: base=0x{:X}, offset=0x{:X}, length={}, total_size=0x{:X}",
										m_rx_reg_offset, offset_rx, length, m_rx_reg_size);
			return DICE_INVALID_OFFSET;
		}
		return offset_rx;
	}

	std::expected<void, IOKitError> DiceAudioDevice::initIoFunctions()
	{
		// Read offsets and sizes (returned in quadlets, but we use byte values)
		std::expected<uint32_t, IOKitError> global_offset_res = readReg(DICE_REGISTER_GLOBAL_PAR_SPACE_OFF);
		if (!global_offset_res)
		{
			spdlog::error("Could not initialize m_global_reg_offset");
			return std::unexpected(global_offset_res.error());
		}
		m_global_reg_offset = global_offset_res.value() * 4;

		auto global_size_res = readReg(DICE_REGISTER_GLOBAL_PAR_SPACE_SZ);
		if (!global_size_res)
		{
			spdlog::error("Could not initialize m_global_reg_size");
			return std::unexpected(global_size_res.error());
		}
		m_global_reg_size = global_size_res.value() * 4;

		std::expected<uint32_t, IOKitError> tx_offset_res = readReg(DICE_REGISTER_TX_PAR_SPACE_OFF);
		if (!tx_offset_res)
		{
			spdlog::error("Could not initialize m_tx_reg_offset");
			return std::unexpected(tx_offset_res.error());
		}
		m_tx_reg_offset = tx_offset_res.value() * 4;

		auto tx_size_res = readReg(DICE_REGISTER_TX_PAR_SPACE_SZ);
		if (!tx_size_res)
		{
			spdlog::error("Could not initialize m_tx_reg_size");
			return std::unexpected(tx_size_res.error());
		}
		m_tx_reg_size = tx_size_res.value() * 4;

		auto rx_offset_res = readReg(DICE_REGISTER_RX_PAR_SPACE_OFF);
		if (!rx_offset_res)
		{
			spdlog::error("Could not initialize m_rx_reg_offset");
			return std::unexpected(rx_offset_res.error());
		}
		m_rx_reg_offset = rx_offset_res.value() * 4;

		auto rx_size_res = readReg(DICE_REGISTER_RX_PAR_SPACE_SZ);
		if (!rx_size_res)
		{
			spdlog::error("Could not initialize m_rx_reg_size");
			return std::unexpected(rx_size_res.error());
		}
		m_rx_reg_size = rx_size_res.value() * 4;

		auto unused1_offset_res = readReg(DICE_REGISTER_UNUSED1_SPACE_OFF);
		if (!unused1_offset_res)
		{
			spdlog::error("Could not initialize m_unused1_reg_offset");
			return std::unexpected(unused1_offset_res.error());
		}
		m_unused1_reg_offset = unused1_offset_res.value() * 4;

		auto unused1_size_res = readReg(DICE_REGISTER_UNUSED1_SPACE_SZ);
		if (!unused1_size_res)
		{
			spdlog::error("Could not initialize m_unused1_reg_size");
			return std::unexpected(unused1_size_res.error());
		}
		m_unused1_reg_size = unused1_size_res.value() * 4;

		auto unused2_offset_res = readReg(DICE_REGISTER_UNUSED2_SPACE_OFF);
		if (!unused2_offset_res)
		{
			spdlog::error("Could not initialize m_unused2_reg_offset");
			return std::unexpected(unused2_offset_res.error());
		}
		m_unused2_reg_offset = unused2_offset_res.value() * 4;

		auto unused2_size_res = readReg(DICE_REGISTER_UNUSED2_SPACE_SZ);
		if (!unused2_size_res)
		{
			spdlog::error("Could not initialize m_unused2_reg_size");
			return std::unexpected(unused2_size_res.error());
		}
		m_unused2_reg_size = unused2_size_res.value() * 4;

		auto nb_tx_res = readReg(m_tx_reg_offset + DICE_REGISTER_TX_NB_TX);
		if (!nb_tx_res)
		{
			spdlog::error("Could not initialize m_nb_tx");
			return std::unexpected(nb_tx_res.error());
		}
		m_nb_tx = nb_tx_res.value();

		auto tx_size_param_res = readReg(m_tx_reg_offset + DICE_REGISTER_TX_SZ_TX);
		if (!tx_size_param_res)
		{
			spdlog::error("Could not initialize m_tx_size");
			return std::unexpected(tx_size_param_res.error());
		}
		m_tx_size = tx_size_param_res.value() * 4;

		auto nb_rx_res = readReg(m_rx_reg_offset + DICE_REGISTER_RX_NB_RX);
		if (!nb_rx_res)
		{
			spdlog::error("Could not initialize m_nb_rx");
			return std::unexpected(nb_rx_res.error());
		}
		m_nb_rx = nb_rx_res.value();

		auto rx_size_param_res = readReg(m_rx_reg_offset + DICE_REGISTER_RX_SZ_RX);
		if (!rx_size_param_res)
		{
			spdlog::error("Could not initialize m_rx_size");
			return std::unexpected(rx_size_param_res.error());
		}
		m_rx_size = rx_size_param_res.value() * 4;

		spdlog::debug("DICE Parameter Space info:");
		spdlog::debug(" Global  : offset=0x{:04X} size={:04d}", m_global_reg_offset, m_global_reg_size);
		spdlog::debug(" TX      : offset=0x{:04X} size={:04d}", m_tx_reg_offset, m_tx_reg_size);
		spdlog::debug("               nb={:4d} size={:04d}", m_nb_tx, m_tx_size);
		spdlog::debug(" RX      : offset=0x{:04X} size={:04d}", m_rx_reg_offset, m_rx_reg_size);
		spdlog::debug("               nb={:4d} size={:04d}", m_nb_rx, m_rx_size);
		spdlog::debug(" UNUSED1 : offset=0x{:04X} size={:04d}", m_unused1_reg_offset, m_unused1_reg_size);
		spdlog::debug(" UNUSED2 : offset=0x{:04X} size={:04d}", m_unused2_reg_offset, m_unused2_reg_size);

		return {};
	}

	std::expected<uint32_t, IOKitError> DiceAudioDevice::readGlobalReg(uint32_t offset)
	{
		uint64_t addr = globalOffsetGen(offset, 4);
		if (addr == DICE_INVALID_OFFSET)
			return std::unexpected(IOKitError::BadArgument); // Or a more specific error if possible
		return readReg(addr);
	}

	std::expected<void, IOKitError> DiceAudioDevice::writeGlobalReg(uint32_t offset, uint32_t data)
	{
		uint64_t addr = globalOffsetGen(offset, 4);
		if (addr == DICE_INVALID_OFFSET)
			return std::unexpected(IOKitError::BadArgument); // Or a more specific error if possible
		return writeReg(addr, data);
	}

	std::expected<std::vector<uint32_t>, IOKitError> DiceAudioDevice::readGlobalRegBlock(uint32_t offset, size_t length)
	{
		uint64_t addr = globalOffsetGen(offset, length);
		if (addr == DICE_INVALID_OFFSET)
			return std::unexpected(IOKitError::BadArgument); // Or a more specific error if possible
		return readRegBlock(addr, length);
	}

	std::expected<void, IOKitError> DiceAudioDevice::writeGlobalRegBlock(uint32_t offset, const uint32_t *data, size_t length)
	{
		uint64_t addr = globalOffsetGen(offset, length);
		if (addr == DICE_INVALID_OFFSET)
			return std::unexpected(IOKitError::BadArgument); // Or a more specific error if possible
		return writeRegBlock(addr, data, length);
	}

	// --- Implementations for TX/RX register access ---

	std::expected<uint32_t, IOKitError> DiceAudioDevice::readTxReg(unsigned int i, uint32_t offset)
	{
		uint64_t addr = txOffsetGen(i, offset, 4);
		if (addr == DICE_INVALID_OFFSET)
			return std::unexpected(IOKitError::BadArgument);
		return readReg(m_tx_reg_offset + addr);
	}

	std::expected<void, IOKitError> DiceAudioDevice::writeTxReg(unsigned int i, uint32_t offset, uint32_t data)
	{
		uint64_t addr = txOffsetGen(i, offset, 4);
		if (addr == DICE_INVALID_OFFSET)
			return std::unexpected(IOKitError::BadArgument);
		return writeReg(m_tx_reg_offset + addr, data);
	}

	std::expected<std::vector<uint32_t>, IOKitError> DiceAudioDevice::readTxRegBlock(unsigned int i, uint32_t offset, size_t length)
	{
		uint64_t addr = txOffsetGen(i, offset, length);
		if (addr == DICE_INVALID_OFFSET)
			return std::unexpected(IOKitError::BadArgument);
		return readRegBlock(m_tx_reg_offset + addr, length);
	}

	std::expected<void, IOKitError> DiceAudioDevice::writeTxRegBlock(unsigned int i, uint32_t offset, const uint32_t *data, size_t length)
	{
		uint64_t addr = txOffsetGen(i, offset, length);
		if (addr == DICE_INVALID_OFFSET)
			return std::unexpected(IOKitError::BadArgument);
		return writeRegBlock(m_tx_reg_offset + addr, data, length);
	}

	std::expected<uint32_t, IOKitError> DiceAudioDevice::readRxReg(unsigned int i, uint32_t offset)
	{
		uint64_t addr = rxOffsetGen(i, offset, 4);
		if (addr == DICE_INVALID_OFFSET)
			return std::unexpected(IOKitError::BadArgument);
		return readReg(m_rx_reg_offset + addr);
	}

	std::expected<void, IOKitError> DiceAudioDevice::writeRxReg(unsigned int i, uint32_t offset, uint32_t data)
	{
		uint64_t addr = rxOffsetGen(i, offset, 4);
		if (addr == DICE_INVALID_OFFSET)
			return std::unexpected(IOKitError::BadArgument);
		return writeReg(m_rx_reg_offset + addr, data);
	}

	std::expected<std::vector<uint32_t>, IOKitError> DiceAudioDevice::readRxRegBlock(unsigned int i, uint32_t offset, size_t length)
	{
		uint64_t addr = rxOffsetGen(i, offset, length);
		if (addr == DICE_INVALID_OFFSET)
			return std::unexpected(IOKitError::BadArgument);
		return readRegBlock(m_rx_reg_offset + addr, length);
	}

	std::expected<void, IOKitError> DiceAudioDevice::writeRxRegBlock(unsigned int i, uint32_t offset, const uint32_t *data, size_t length)
	{
		uint64_t addr = rxOffsetGen(i, offset, length);
		if (addr == DICE_INVALID_OFFSET)
			return std::unexpected(IOKitError::BadArgument);
		return writeRegBlock(m_rx_reg_offset + addr, data, length);
	}

	// --- Implementations for base register access ---

	std::expected<uint32_t, IOKitError> DiceAudioDevice::readReg(uint64_t offset)
	{
		if (!deviceInterface)
		{
			spdlog::error("DiceAudioDevice::readReg - deviceInterface is null.");
			return std::unexpected(IOKitError::NoDevice);
		}

		UInt32 result;
		UInt16 nodeID = 0; // Local node (assuming we read from the device itself)
		FWAddress addr;
		addr.addressHi = static_cast<UInt16>((offset >> 32) & 0xFFFF);
		addr.addressLo = static_cast<UInt32>(offset & 0xFFFFFFFF);

		UInt32 generation;
		IOReturn status = (*deviceInterface)->GetBusGeneration(deviceInterface, &generation);
		if (status != kIOReturnSuccess)
		{
			spdlog::error("DiceAudioDevice::readReg - Failed to get bus generation: 0x{:x}", status);
			return std::unexpected(static_cast<IOKitError>(status));
		}

		status = ((IOFireWireDeviceInterface_t *)deviceInterface)->ReadQuadlet(deviceInterface, 0, &addr, &result, kFWFailOnReset, generation);

		if (status != kIOReturnSuccess)
		{
			spdlog::error("DiceAudioDevice::readReg - ReadQuadlet failed for offset 0x{:x} with status 0x{:x}", offset, status);
			// Attempt to map IOReturn to IOKitError, default to general Error if not found
			return std::unexpected(static_cast<IOKitError>(status));
		}

		// DICE registers are little-endian, so we need to swap bytes on read
		result = CFSwapInt32LittleToHost(result);
		return result;
	}

	std::expected<void, IOKitError> DiceAudioDevice::writeReg(uint64_t offset, uint32_t data)
	{
		if (!deviceInterface)
		{
			spdlog::error("DiceAudioDevice::writeReg - deviceInterface is null.");
			return std::unexpected(IOKitError::NoDevice);
		}

		UInt16 nodeID = 0; // Local node
		FWAddress addr;
		addr.addressHi = static_cast<UInt16>((offset >> 32) & 0xFFFF);
		addr.addressLo = static_cast<UInt32>(offset & 0xFFFFFFFF);

		UInt32 generation;
		IOReturn status = (*deviceInterface)->GetBusGeneration(deviceInterface, &generation);
		if (status != kIOReturnSuccess)
		{
			spdlog::error("DiceAudioDevice::writeReg - Failed to get bus generation: 0x{:x}", status);
			return std::unexpected(static_cast<IOKitError>(status));
		}

		// DICE registers are little-endian, so we need to swap bytes before writing
		uint32_t dataToWrite = CFSwapInt32HostToLittle(data);

		status = ((IOFireWireDeviceInterface_t *)deviceInterface)->WriteQuadlet(deviceInterface, 0, &addr, dataToWrite, kFWFailOnReset, generation);

		if (status != kIOReturnSuccess)
		{
			spdlog::error("DiceAudioDevice::writeReg - WriteQuadlet failed for offset 0x{:x} with status 0x{:x}", offset, status);
			// Attempt to map IOReturn to IOKitError, default to general Error if not found
			return std::unexpected(static_cast<IOKitError>(status));
		}
		return {};
	}

	std::expected<std::vector<uint32_t>, IOKitError> DiceAudioDevice::readRegBlock(uint64_t offset, size_t length)
	{
		if (length % 4 != 0)
		{
			spdlog::error("DiceAudioDevice::readRegBlock - Length must be a multiple of 4.");
			return std::unexpected(IOKitError::BadArgument);
		}
		size_t numQuadlets = length / 4;
		std::vector<uint32_t> data(numQuadlets);
		for (size_t i = 0; i < numQuadlets; ++i)
		{
			auto result = readReg(offset + (i * 4));
			if (!result)
			{
				spdlog::error("DiceAudioDevice::readRegBlock - Failed reading quadlet {} at offset 0x{:x}", i, offset + (i * 4));
				return std::unexpected(result.error());
			}
			data[i] = result.value();
			// Note: readReg already handles byte swapping
		}
		return data;
	}

	std::expected<void, IOKitError> DiceAudioDevice::writeRegBlock(uint64_t offset, const uint32_t *data, size_t length)
	{
		if (length % 4 != 0)
		{
			spdlog::error("DiceAudioDevice::writeRegBlock - Length must be a multiple of 4.");
			return std::unexpected(IOKitError::BadArgument);
		}
		size_t numQuadlets = length / 4;
		// DICE registers are little-endian, so create a temporary buffer with byte-swapped data
		std::vector<uint32_t> dataToWrite(numQuadlets);
		for (size_t i = 0; i < numQuadlets; ++i)
		{
			dataToWrite[i] = CFSwapInt32HostToLittle(data[i]);
		}

		for (size_t i = 0; i < numQuadlets; ++i)
		{
			auto result = writeReg(offset + (i * 4), dataToWrite[i]);
			if (!result)
			{
				spdlog::error("DiceAudioDevice::writeRegBlock - Failed writing quadlet {} at offset 0x{:x}", i, offset + (i * 4));
				return std::unexpected(result.error());
			}
		}
		return {};
	}

	// --- Missing method implementations for lock() and unlock() ---

	std::expected<void, IOKitError> DiceAudioDevice::lock()
	{
		spdlog::info("Locking DICE device (GUID: 0x{:x})", getGuid());

		// Already locked?
		if (notifier_ != 0)
		{
			spdlog::warn("Device already locked");
			return {};
		}

		// Set up notification handler for device events
		io_object_t notifier = 0;
		IONotificationPortRef port = getNotificationPort();
		if (!port)
		{
			spdlog::error("Notification port is null, cannot set up notifier");
			return std::unexpected(IOKitError::NoResources);
		}

		// Register ourselves as the device owner
		uint64_t ownerRegAddr = DICE_REGISTER_BASE + m_global_reg_offset + DICE_REGISTER_GLOBAL_OWNER;

		// We need to read the current owner value (we can read only 32 bits at a time)
		uint64_t currentOwner = 0;

		// First read high 32 bits
		auto ownerHighResult = readReg(ownerRegAddr);
		if (!ownerHighResult)
		{
			spdlog::error("Failed to read current owner (high): {}", static_cast<int>(ownerHighResult.error()));
			return std::unexpected(ownerHighResult.error());
		}

		// Then read low 32 bits
		auto ownerLowResult = readReg(ownerRegAddr + 4);
		if (!ownerLowResult)
		{
			spdlog::error("Failed to read current owner (low): {}", static_cast<int>(ownerLowResult.error()));
			return std::unexpected(ownerLowResult.error());
		}

		// Combine to form 64-bit owner ID
		currentOwner = (static_cast<uint64_t>(ownerHighResult.value()) << 32) | ownerLowResult.value();

		// Our ownership ID is a unique value that includes our NodeID
		uint32_t nodeID = 0; // We would get this from FireWire API
		uint64_t ourOwnerValue = ((static_cast<uint64_t>(0xFFC0 | nodeID)) << 48) | 0x0001;

		// Try to register as owner using compare-swap
		if (deviceInterface)
		{
			uint32_t generation;
			IOReturn status = (*deviceInterface)->GetBusGeneration(deviceInterface, &generation);
			if (status != kIOReturnSuccess)
			{
				spdlog::error("Failed to get bus generation: 0x{:x}", status);
				return std::unexpected(static_cast<IOKitError>(status));
			}

			// Try to write our owner value if current owner is "no owner"
			if (currentOwner == DICE_OWNER_NO_OWNER)
			{
				// Write high 32 bits of our owner value
				auto writeHighResult = writeReg(ownerRegAddr, static_cast<uint32_t>((ourOwnerValue >> 32) & 0xFFFFFFFF));
				if (!writeHighResult)
				{
					spdlog::error("Could not register as device owner (high bits)");
					return std::unexpected(writeHighResult.error());
				}

				// Write low 32 bits of our owner value
				auto writeLowResult = writeReg(ownerRegAddr + 4, static_cast<uint32_t>(ourOwnerValue & 0xFFFFFFFF));
				if (!writeLowResult)
				{
					spdlog::error("Could not register as device owner (low bits)");
					return std::unexpected(writeLowResult.error());
				}
			}
			else
			{
				spdlog::warn("Device already has an owner: 0x{:x}", currentOwner);
			}
		}
		else
		{
			spdlog::error("Device interface is null");
			return std::unexpected(IOKitError::NoDevice);
		}

		notifier_ = notifier;
		return {};
	}

	std::expected<void, IOKitError> DiceAudioDevice::unlock()
	{
		spdlog::info("Unlocking DICE device (GUID: 0x{:x})", getGuid());

		if (notifier_ == 0)
		{
			spdlog::warn("Device not locked, cannot unlock");
			return {};
		}

		// Unregister ourselves as device owner
		uint64_t ownerRegAddr = DICE_REGISTER_BASE + m_global_reg_offset + DICE_REGISTER_GLOBAL_OWNER;

		// Write the "no owner" value to release ownership (64-bit value)
		uint64_t noOwnerValue = DICE_OWNER_NO_OWNER;

		if (deviceInterface)
		{
			uint32_t generation;
			IOReturn status = (*deviceInterface)->GetBusGeneration(deviceInterface, &generation);
			if (status != kIOReturnSuccess)
			{
				spdlog::error("Failed to get bus generation: 0x{:x}", status);
				return std::unexpected(static_cast<IOKitError>(status));
			}

			// Write high 32 bits of the "no owner" value
			auto writeHighResult = writeReg(ownerRegAddr, static_cast<uint32_t>((noOwnerValue >> 32) & 0xFFFFFFFF));
			if (!writeHighResult)
			{
				spdlog::error("Could not unregister as device owner (high bits)");
				return std::unexpected(writeHighResult.error());
			}

			// Write low 32 bits of the "no owner" value
			auto writeLowResult = writeReg(ownerRegAddr + 4, static_cast<uint32_t>(noOwnerValue & 0xFFFFFFFF));
			if (!writeLowResult)
			{
				spdlog::error("Could not unregister as device owner (low bits)");
				return std::unexpected(writeLowResult.error());
			}
		}
		else
		{
			spdlog::error("Device interface is null");
			return std::unexpected(IOKitError::NoDevice);
		}

		// Clean up notifier
		IOObjectRelease(notifier_);
		notifier_ = 0;

		return {};
	}

	// --- Implementations for missing private helper functions ---

	std::vector<std::string> DiceAudioDevice::splitNameString(const std::string &in)
	{
		// Placeholder implementation - splits by null terminator found within the string
		std::vector<std::string> result;
		std::string current;
		for (char c : in)
		{
			if (c == '\0')
			{
				if (!current.empty())
				{
					result.push_back(current);
					current.clear();
				}
			}
			else
			{
				current += c;
			}
		}
		if (!current.empty())
		{
			result.push_back(current);
		}
		// If no null terminators, return the whole string as one element
		if (result.empty() && !in.empty())
		{
			result.push_back(in);
		}
		return result;
	}

	std::vector<std::string> DiceAudioDevice::getTxNameString(unsigned int i)
	{
		char nameBuffer[DICE_TX_NAMES_SIZE];
		std::expected<std::vector<uint32_t>, IOKitError> readResult = readTxRegBlock(i, DICE_REGISTER_TX_NAMES_BASE, DICE_TX_NAMES_SIZE);
		if (!readResult)
		{
			spdlog::error("Failed to read TX name string for index {}: {}", i, static_cast<int>(readResult.error()));
			return std::vector<std::string>();
		}
		std::vector<uint32_t> nameData = readResult.value();
		std::memcpy(nameBuffer, nameData.data(), DICE_TX_NAMES_SIZE);

		// Ensure null termination for safety
		nameBuffer[DICE_TX_NAMES_SIZE - 1] = '\0';
		return splitNameString(std::string(nameBuffer));
	}

	std::vector<std::string> DiceAudioDevice::getRxNameString(unsigned int i)
	{
		char nameBuffer[DICE_RX_NAMES_SIZE];
		std::expected<std::vector<uint32_t>, IOKitError> readResult = readRxRegBlock(i, DICE_REGISTER_RX_NAMES_BASE, DICE_RX_NAMES_SIZE);
		if (!readResult)
		{
			spdlog::error("Failed to read RX name string for index {}: {}", i, static_cast<int>(readResult.error()));
			return std::vector<std::string>();
		}
		std::vector<uint32_t> nameData = readResult.value();
		std::memcpy(nameBuffer, nameData.data(), DICE_RX_NAMES_SIZE);

		// Ensure null termination for safety
		nameBuffer[DICE_RX_NAMES_SIZE - 1] = '\0';
		return splitNameString(std::string(nameBuffer));
	}

	std::vector<std::string> DiceAudioDevice::getClockSourceNameString()
	{
		char nameBuffer[DICE_CLOCKSOURCENAMES_SIZE];
		std::expected<std::vector<uint32_t>, IOKitError> readResult = readGlobalRegBlock(DICE_REGISTER_GLOBAL_CLOCKSOURCENAMES, DICE_CLOCKSOURCENAMES_SIZE);
		if (!readResult)
		{
			spdlog::error("Failed to read clock source name string: {}", static_cast<int>(readResult.error()));
			return std::vector<std::string>();
		}
		std::vector<uint32_t> nameData = readResult.value();
		std::memcpy(nameBuffer, nameData.data(), DICE_CLOCKSOURCENAMES_SIZE);

		// Ensure null termination for safety
		nameBuffer[DICE_CLOCKSOURCENAMES_SIZE - 1] = '\0';
		return splitNameString(std::string(nameBuffer));
	}

} // namespace FWA
