// src/FWA/DiceEAP.cpp
#include "FWA/DiceEAP.hpp"
#include "FWA/DiceAudioDevice.h"
#include "FWA/Error.h"
#include <spdlog/spdlog.h>
#include <vector>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <thread>

namespace FWA
{
	//--------------------------------------------------------------------------
	// DiceRouterConfig Implementation
	//--------------------------------------------------------------------------

	FWA::DiceRouterConfig::DiceRouterConfig(DiceEAP &eap)
			: eap_(eap)
	{
	}

	DiceRouterConfig::~DiceRouterConfig()
	{
	}

	std::expected<void, IOKitError> DiceRouterConfig::read(uint32_t offset)
	{
		spdlog::debug("Reading router configuration from offset 0x{:x}", offset);

		// First clear all existing routes
		routes_.clear();

		// Read the number of routes
		uint32_t nbRoutes = 0;
		std::expected<void, IOKitError> nbRoutesRes = eap_.readReg(DiceEAP::RegBase::CurrentCfg, offset, &nbRoutes);
		if (!nbRoutesRes)
		{
			spdlog::error("Failed to read number of routes");
			return std::unexpected(nbRoutesRes.error());
		}

		if (nbRoutes == 0)
		{
			spdlog::debug("No routes defined in configuration");
			return {};
		}

		// Read all route entries
		std::vector<uint32_t> routesData(nbRoutes);
		std::expected<void, IOKitError> routesDataRes =
				eap_.readRegBlock(DiceEAP::RegBase::CurrentCfg, offset + 4, routesData.data(), nbRoutes * 4);
		if (!routesDataRes)
		{
			spdlog::error("Failed to read route configuration data");
			return std::unexpected(routesDataRes.error());
		}

		// Parse routes - format is destination in the lower byte, source in the next byte
		for (uint32_t i = 0; i < nbRoutes; i++)
		{
			unsigned char dest = routesData[i] & 0xFF;
			unsigned char src = (routesData[i] >> 8) & 0xFF;
			routes_[dest] = src;
			spdlog::debug("Read route: Source 0x{:02x} -> Destination 0x{:02x}", src, dest);
		}

		spdlog::info("Read {} routes from configuration", routes_.size());
		return {};
	}

	std::expected<void, IOKitError> DiceRouterConfig::write(uint32_t offset)
	{
		spdlog::debug("Writing router configuration to offset 0x{:x}", offset);

		// Get the number of routes
		uint32_t nbRoutes = static_cast<uint32_t>(routes_.size());

		// Ensure we don't exceed the maximum number of routes
		// DICE devices typically support up to 128 routes
		if (nbRoutes > 128)
		{
			spdlog::warn("More than 128 routes defined ({}) - truncating to 128", nbRoutes);
			nbRoutes = 128;
		}

		spdlog::debug("Writing {} routes to configuration", nbRoutes);

		// Convert routes to the format expected by the device
		std::vector<uint32_t> routesData(nbRoutes);
		size_t idx = 0;

		for (const auto &route : routes_)
		{
			if (idx >= nbRoutes)
				break;

			// Format: destination in lower byte, source in next byte
			routesData[idx++] = (static_cast<uint32_t>(route.second) << 8) | route.first;
		}

		// First write zeros to clear any previous configuration
		// DICE devices typically have a maximum of 128 routes plus the count
		std::vector<uint32_t> zeros(129, 0);
		std::expected<void, IOKitError> clearRes =
				eap_.writeRegBlock(DiceEAP::RegBase::CurrentCfg, offset, zeros.data(), zeros.size() * 4);
		if (!clearRes)
		{
			spdlog::error("Failed to clear route configuration");
			return std::unexpected(clearRes.error());
		}

		// Write the actual routes
		if (nbRoutes > 0)
		{
			std::expected<void, IOKitError> routesRes =
					eap_.writeRegBlock(DiceEAP::RegBase::CurrentCfg, offset + 4, routesData.data(), routesData.size() * 4);
			if (!routesRes)
			{
				spdlog::error("Failed to write route configuration data");
				return std::unexpected(routesRes.error());
			}
		}

		// Write the number of routes
		std::expected<void, IOKitError> nbRoutesRes =
				eap_.writeReg(DiceEAP::RegBase::CurrentCfg, offset, nbRoutes);
		if (!nbRoutesRes)
		{
			spdlog::error("Failed to write number of routes");
			return std::unexpected(nbRoutesRes.error());
		}

		spdlog::info("Successfully wrote {} routes to configuration", nbRoutes);
		return {};
	}

	std::expected<void, IOKitError> DiceRouterConfig::clearRoutes()
	{
		spdlog::debug("Clearing router configuration");
		routes_.clear();
		return {};
	}

	std::expected<void, IOKitError> DiceRouterConfig::createRoute(unsigned char srcId, unsigned char dstId)
	{
		spdlog::debug("Creating route: Source {} -> Destination {}", srcId, dstId);
		routes_[dstId] = srcId;
		return {};
	}

	std::expected<void, IOKitError> DiceRouterConfig::setupRoute(unsigned char srcId, unsigned char dstId)
	{
		spdlog::debug("Setting up route: Source {} -> Destination {}", srcId, dstId);
		routes_[dstId] = srcId;
		return {};
	}

	std::expected<void, IOKitError> DiceRouterConfig::muteRoute(unsigned char dstId)
	{
		spdlog::debug("Muting route for destination {}", dstId);
		// Set source to Muted (value 15)
		routes_[dstId] = static_cast<unsigned char>(DICE::RouteSource::Muted);
		return {};
	}

	std::expected<void, IOKitError> DiceRouterConfig::removeRoute(unsigned char dstId)
	{
		spdlog::debug("Removing route for destination {}", dstId);
		routes_.erase(dstId);
		return {};
	}

	std::expected<unsigned char, IOKitError> DiceRouterConfig::getSourceForDestination(unsigned char dstId)
	{
		auto it = routes_.find(dstId);
		if (it != routes_.end())
		{
			return it->second;
		}
		spdlog::debug("No source found for destination {}", dstId);
		return std::unexpected(IOKitError::NotFound);
	}

	std::expected<std::vector<unsigned char>, IOKitError> DiceRouterConfig::getDestinationsForSource(unsigned char srcId)
	{
		std::vector<unsigned char> destinations;
		for (const auto &route : routes_)
		{
			if (route.second == srcId)
			{
				destinations.push_back(route.first);
			}
		}
		return destinations;
	}

	//--------------------------------------------------------------------------
	// DicePeakSpace Implementation
	//--------------------------------------------------------------------------

	FWA::DicePeakSpace::DicePeakSpace(DiceEAP &eap)
			: eap_(eap)
	{
	}

	DicePeakSpace::~DicePeakSpace()
	{
	}

	std::expected<void, IOKitError> DicePeakSpace::read(uint32_t offset)
	{
		spdlog::debug("Reading peak space from offset 0x{:x}", offset);

		// Clear existing peaks
		peaks_.clear();

		// Get the router configuration to determine how many peak values to read
		DiceRouterConfig *router = eap_.getActiveRouterConfig();
		if (!router)
		{
			spdlog::error("No active router configuration");
			return std::unexpected(IOKitError::NoDevice);
		}

		size_t numRoutes = router->getNumRoutes();
		if (numRoutes == 0)
		{
			spdlog::debug("No routes defined, no peak data to read");
			return {};
		}

		// Read all peak data entries
		std::vector<uint32_t> peakData(numRoutes);
		std::expected<void, IOKitError> peakDataRes =
				eap_.readRegBlock(DiceEAP::RegBase::Peak, offset, peakData.data(), numRoutes * 4);
		if (!peakDataRes)
		{
			spdlog::error("Failed to read peak data");
			return std::unexpected(peakDataRes.error());
		}

		// Parse peak data - format is destination in the lower byte, peak value in bits 16-27
		for (size_t i = 0; i < numRoutes; i++)
		{
			unsigned char dest = peakData[i] & 0xFF;
			int peakValue = (peakData[i] & 0x0FFF0000) >> 16;

			// Only update if the new peak is higher than any existing peak for this destination
			auto it = peaks_.find(dest);
			if (it == peaks_.end() || it->second < peakValue)
			{
				peaks_[dest] = peakValue;
				spdlog::debug("Peak for destination 0x{:02x}: {}", dest, peakValue);
			}
		}

		return {};
	}

	std::expected<std::map<unsigned char, int>, IOKitError> DicePeakSpace::getPeaks()
	{
		spdlog::debug("Getting all peak values");
		return peaks_;
	}

	std::expected<int, IOKitError> DicePeakSpace::getPeak(unsigned char dstId)
	{
		auto it = peaks_.find(dstId);
		if (it != peaks_.end())
		{
			return it->second;
		}
		spdlog::debug("No peak value found for destination {}", dstId);
		return std::unexpected(IOKitError::NotFound);
	}

	//--------------------------------------------------------------------------
	// DiceStandaloneConfig Implementation
	//--------------------------------------------------------------------------

	FWA::DiceStandaloneConfig::DiceStandaloneConfig(DiceEAP &eap)
			: eap_(eap), clockSrc_(0), aesExt_(0), adatExt_(0), wcExt_(0), intExt_(0)
	{
	}

	DiceStandaloneConfig::~DiceStandaloneConfig()
	{
	}

	std::expected<void, IOKitError> DiceStandaloneConfig::read()
	{
		spdlog::debug("Reading standalone configuration");

		// Read the entire standalone configuration
		std::vector<uint32_t> data(5, 0);
		std::expected<void, IOKitError> result =
				eap_.readRegBlock(DiceEAP::RegBase::Standalone, 0, data.data(), data.size() * 4);
		if (!result)
		{
			spdlog::error("Failed to read standalone configuration");
			return std::unexpected(result.error());
		}

		// Parse the configuration
		clockSrc_ = data[0];
		aesExt_ = data[1];
		adatExt_ = data[2];
		wcExt_ = data[3];
		intExt_ = data[4];

		spdlog::debug("Standalone configuration:");
		spdlog::debug("  Clock source: 0x{:08x}", clockSrc_);
		spdlog::debug("  AES external: 0x{:08x}", aesExt_);
		spdlog::debug("  ADAT external: 0x{:08x}", adatExt_);
		spdlog::debug("  Word clock external: 0x{:08x}", wcExt_);
		spdlog::debug("  Internal/external: 0x{:08x}", intExt_);

		return {};
	}

	std::expected<void, IOKitError> DiceStandaloneConfig::write()
	{
		spdlog::debug("Writing standalone configuration");

		// Create the configuration data
		std::vector<uint32_t> data = {
				clockSrc_,
				aesExt_,
				adatExt_,
				wcExt_,
				intExt_};

		// Write the configuration
		std::expected<void, IOKitError> result =
				eap_.writeRegBlock(DiceEAP::RegBase::Standalone, 0, data.data(), data.size() * 4);
		if (!result)
		{
			spdlog::error("Failed to write standalone configuration");
			return std::unexpected(result.error());
		}

		spdlog::debug("Successfully wrote standalone configuration");
		return {};
	}

	//--------------------------------------------------------------------------
	// DiceMixer Implementation
	//--------------------------------------------------------------------------

	FWA::DiceMixer::DiceMixer(DiceEAP &eap)
			: eap_(eap)
	{
	}

	DiceMixer::~DiceMixer()
	{
	}

	std::expected<void, IOKitError> DiceMixer::init()
	{
		spdlog::debug("Initializing DICE mixer");

		if (!eap_.mixerExposed_)
		{
			spdlog::error("Mixer is not exposed on this device");
			return std::unexpected(IOKitError::Unsupported);
		}

		// Get the coefficient array size based on inputs and outputs
		int numInputs = eap_.mixerNumTx_;
		int numOutputs = eap_.mixerNumRx_;

		// Resize the coefficient array
		coefficients_.resize(numInputs * numOutputs, 0);

		// Load initial values from the device
		return loadCoefficients();
	}

	std::expected<void, IOKitError> DiceMixer::loadCoefficients()
	{
		spdlog::debug("Loading mixer coefficients");

		if (coefficients_.empty())
		{
			spdlog::error("Coefficient cache not initialized");
			return std::unexpected(IOKitError::NotInitialized);
		}

		int numInputs = eap_.mixerNumTx_;
		int numOutputs = eap_.mixerNumRx_;
		size_t totalSize = numInputs * numOutputs * sizeof(uint32_t);

		// Read the mixer coefficients from the device
		auto result = eap_.readRegBlock(DiceEAP::RegBase::Mixer, 4, coefficients_.data(), totalSize);
		if (!result)
		{
			spdlog::error("Failed to read mixer coefficients");
			return std::unexpected(result.error());
		}

		spdlog::debug("Successfully loaded {} coefficients", coefficients_.size());
		return {};
	}

	std::expected<void, IOKitError> DiceMixer::storeCoefficients()
	{
		spdlog::debug("Storing mixer coefficients");

		if (coefficients_.empty())
		{
			spdlog::error("Coefficient cache not initialized");
			return std::unexpected(IOKitError::NotInitialized);
		}

		if (eap_.mixerReadonly_)
		{
			spdlog::warn("Mixer is read-only");
			return std::unexpected(IOKitError::ReadOnly);
		}

		int numInputs = eap_.mixerNumTx_;
		int numOutputs = eap_.mixerNumRx_;
		size_t totalSize = numInputs * numOutputs * sizeof(uint32_t);

		// Write the mixer coefficients to the device
		auto result = eap_.writeRegBlock(DiceEAP::RegBase::Mixer, 4, coefficients_.data(), totalSize);
		if (!result)
		{
			spdlog::error("Failed to write mixer coefficients");
			return std::unexpected(result.error());
		}

		spdlog::debug("Successfully stored {} coefficients", coefficients_.size());
		return {};
	}

	int DiceMixer::getRowCount() const
	{
		return eap_.mixerNumTx_;
	}

	int DiceMixer::getColCount() const
	{
		return eap_.mixerNumRx_;
	}

	bool DiceMixer::canWrite(int row, int col) const
	{
		if (eap_.mixerReadonly_)
		{
			return false;
		}
		return (row >= 0 && row < eap_.mixerNumTx_ && col >= 0 && col < eap_.mixerNumRx_);
	}

	std::expected<double, IOKitError> DiceMixer::setValue(int row, int col, double value)
	{
		if (eap_.mixerReadonly_)
		{
			spdlog::warn("Mixer is read-only");
			return std::unexpected(IOKitError::ReadOnly);
		}

		int numOutputs = eap_.mixerNumTx_;
		int index = (numOutputs * col) + row;

		if (index < 0 || index >= static_cast<int>(coefficients_.size()))
		{
			spdlog::error("Invalid coefficient index: {}", index);
			return std::unexpected(IOKitError::BadArgument);
		}

		// Convert value to uint32_t
		uint32_t intValue = static_cast<uint32_t>(value);

		// Write directly to device
		auto result = eap_.writeRegBlock(DiceEAP::RegBase::Mixer, 4 + (index * sizeof(uint32_t)), &intValue, sizeof(uint32_t));
		if (!result)
		{
			spdlog::error("Failed to write coefficient");
			return std::unexpected(result.error());
		}

		// Update the local cache
		coefficients_[index] = intValue;
		return static_cast<double>(intValue);
	}

	std::expected<double, IOKitError> DiceMixer::getValue(int row, int col)
	{
		int numOutputs = eap_.mixerNumTx_;
		int index = (numOutputs * col) + row;

		if (index < 0 || index >= static_cast<int>(coefficients_.size()))
		{
			spdlog::error("Invalid coefficient index: {}", index);
			return std::unexpected(IOKitError::BadArgument);
		}

		// Read directly from device to ensure we have the latest value
		uint32_t value;
		auto result = eap_.readRegBlock(DiceEAP::RegBase::Mixer, 4 + (index * sizeof(uint32_t)), &value, sizeof(uint32_t));
		if (!result)
		{
			spdlog::error("Failed to read coefficient");
			return std::unexpected(result.error());
		}

		// Update the local cache
		coefficients_[index] = value;
		return static_cast<double>(value);
	}

	std::string DiceMixer::getRowName(int row) const
	{
		if (row < 0 || row >= eap_.mixerNumTx_)
		{
			return "Invalid";
		}

		// Get the mixer input destination ID
		unsigned int destId = ((static_cast<unsigned int>(DICE::RouteDestination::Mixer0) << 4) + row);

		// Create a combined name that includes the source routed to this mixer input
		std::string mixerInputName = eap_.getDestinationName(DICE::RouteDestination::Mixer0, row);

		// Get the active router
		DiceRouterConfig *router = eap_.getActiveRouterConfig();
		if (router)
		{
			// Try to get source for this destination
			auto sourceResult = router->getSourceForDestination(destId);
			if (sourceResult)
			{
				unsigned char source = sourceResult.value();
				unsigned char srcId = source >> 4;
				unsigned char srcChannel = source & 0x0F;

				// Convert source ID to RouteSource enum and get its name
				if (srcId < static_cast<unsigned char>(DICE::RouteSource::Invalid))
				{
					DICE::RouteSource routeSource = static_cast<DICE::RouteSource>(srcId);
					std::string sourceName = eap_.getSourceName(routeSource, srcChannel);
					mixerInputName += " (" + sourceName + ")";
				}
			}
		}

		return mixerInputName;
	}

	std::string DiceMixer::getColName(int col) const
	{
		if (col < 0 || col >= eap_.mixerNumRx_)
		{
			return "Invalid";
		}

		// Get the mixer output source ID
		unsigned int srcId = ((static_cast<unsigned int>(DICE::RouteSource::Mixer) << 4) + col);

		// Create a combined name that includes destinations this mixer output is routed to
		std::string mixerOutputName = eap_.getSourceName(DICE::RouteSource::Mixer, col);

		// Get the active router
		DiceRouterConfig *router = eap_.getActiveRouterConfig();
		if (router)
		{
			// Try to get destinations for this source
			auto destsResult = router->getDestinationsForSource(srcId);
			if (destsResult)
			{
				std::vector<unsigned char> destinations = destsResult.value();
				if (!destinations.empty())
				{
					mixerOutputName += " (";
					bool first = true;

					for (unsigned char dest : destinations)
					{
						unsigned char dstId = dest >> 4;
						unsigned char dstChannel = dest & 0x0F;

						// Convert destination ID to RouteDestination enum and get its name
						if (dstId < static_cast<unsigned char>(DICE::RouteDestination::Invalid))
						{
							if (!first)
							{
								mixerOutputName += ", ";
							}
							DICE::RouteDestination routeDest = static_cast<DICE::RouteDestination>(dstId);
							mixerOutputName += eap_.getDestinationName(routeDest, dstChannel);
							first = false;
						}
					}

					mixerOutputName += ")";
				}
			}
		}

		return mixerOutputName;
	}

	void DiceMixer::updateNameCache()
	{
		// Nothing to do here as names are fetched dynamically
	}

	//--------------------------------------------------------------------------
	// DiceStreamConfig Implementation
	//--------------------------------------------------------------------------

	FWA::DiceStreamConfig::DiceStreamConfig(DiceEAP &eap)
			: eap_(eap), numTx_(0), numRx_(0)
	{
	}

	DiceStreamConfig::~DiceStreamConfig()
	{
	}

	std::expected<void, IOKitError> DiceStreamConfig::read(uint32_t offset)
	{
		spdlog::debug("Reading stream configuration from offset 0x{:x}", offset);

		// Clear existing configuration
		txConfigs_.clear();
		rxConfigs_.clear();

		// Read the number of TX and RX streams
		std::expected<void, IOKitError> nbTxRes = eap_.readReg(DiceEAP::RegBase::CurrentCfg, offset, &numTx_);
		if (!nbTxRes)
		{
			spdlog::error("Failed to read number of TX streams");
			return std::unexpected(nbTxRes.error());
		}

		std::expected<void, IOKitError> nbRxRes = eap_.readReg(DiceEAP::RegBase::CurrentCfg, offset + 4, &numRx_);
		if (!nbRxRes)
		{
			spdlog::error("Failed to read number of RX streams");
			return std::unexpected(nbRxRes.error());
		}

		spdlog::debug("Stream configuration: TX={}, RX={}", numTx_, numRx_);

		if (numTx_ > 0)
		{
			// Resize the TX configs array
			txConfigs_.resize(numTx_);

			// Read TX configs
			for (uint32_t i = 0; i < numTx_; i++)
			{
				uint32_t configOffset = offset + 8 + (i * sizeof(ConfigBlock));
				std::expected<void, IOKitError> cfgRes =
						eap_.readRegBlock(DiceEAP::RegBase::CurrentCfg, configOffset,
															reinterpret_cast<uint32_t *>(&txConfigs_[i]), sizeof(ConfigBlock));
				if (!cfgRes)
				{
					spdlog::error("Failed to read TX config block {}", i);
					return std::unexpected(cfgRes.error());
				}

				spdlog::debug("TX stream {}: Audio={}, MIDI={}",
											i, txConfigs_[i].numAudio, txConfigs_[i].numMidi);
			}
		}

		if (numRx_ > 0)
		{
			// Resize the RX configs array
			rxConfigs_.resize(numRx_);

			// Read RX configs
			for (uint32_t i = 0; i < numRx_; i++)
			{
				uint32_t configOffset = offset + 8 + (numTx_ * sizeof(ConfigBlock)) + (i * sizeof(ConfigBlock));
				std::expected<void, IOKitError> cfgRes =
						eap_.readRegBlock(DiceEAP::RegBase::CurrentCfg, configOffset,
															reinterpret_cast<uint32_t *>(&rxConfigs_[i]), sizeof(ConfigBlock));
				if (!cfgRes)
				{
					spdlog::error("Failed to read RX config block {}", i);
					return std::unexpected(cfgRes.error());
				}

				spdlog::debug("RX stream {}: Audio={}, MIDI={}",
											i, rxConfigs_[i].numAudio, rxConfigs_[i].numMidi);
			}
		}

		return {};
	}

	std::expected<void, IOKitError> DiceStreamConfig::write(uint32_t offset)
	{
		spdlog::debug("Writing stream configuration to offset 0x{:x}", offset);

		// First write the number of TX and RX streams
		std::expected<void, IOKitError> nbTxRes = eap_.writeReg(DiceEAP::RegBase::NewStreamCfg, offset, numTx_);
		if (!nbTxRes)
		{
			spdlog::error("Failed to write number of TX streams");
			return std::unexpected(nbTxRes.error());
		}

		std::expected<void, IOKitError> nbRxRes = eap_.writeReg(DiceEAP::RegBase::NewStreamCfg, offset + 4, numRx_);
		if (!nbRxRes)
		{
			spdlog::error("Failed to write number of RX streams");
			return std::unexpected(nbRxRes.error());
		}

		spdlog::debug("Writing stream configuration: TX={}, RX={}", numTx_, numRx_);

		// Write TX configs
		for (uint32_t i = 0; i < numTx_; i++)
		{
			uint32_t configOffset = offset + 8 + (i * sizeof(ConfigBlock));
			std::expected<void, IOKitError> cfgRes =
					eap_.writeRegBlock(DiceEAP::RegBase::NewStreamCfg, configOffset,
														 reinterpret_cast<const uint32_t *>(&txConfigs_[i]), sizeof(ConfigBlock));
			if (!cfgRes)
			{
				spdlog::error("Failed to write TX config block {}", i);
				return std::unexpected(cfgRes.error());
			}

			spdlog::debug("Wrote TX stream {}: Audio={}, MIDI={}",
										i, txConfigs_[i].numAudio, txConfigs_[i].numMidi);
		}

		// Write RX configs
		for (uint32_t i = 0; i < numRx_; i++)
		{
			uint32_t configOffset = offset + 8 + (numTx_ * sizeof(ConfigBlock)) + (i * sizeof(ConfigBlock));
			std::expected<void, IOKitError> cfgRes =
					eap_.writeRegBlock(DiceEAP::RegBase::NewStreamCfg, configOffset,
														 reinterpret_cast<const uint32_t *>(&rxConfigs_[i]), sizeof(ConfigBlock));
			if (!cfgRes)
			{
				spdlog::error("Failed to write RX config block {}", i);
				return std::unexpected(cfgRes.error());
			}

			spdlog::debug("Wrote RX stream {}: Audio={}, MIDI={}",
										i, rxConfigs_[i].numAudio, rxConfigs_[i].numMidi);
		}

		// Now we need to activate the new configuration
		uint32_t cmd = DICE_EAP_CMD_OPCODE_LD_STRM_CFG;

		// Set the appropriate flags based on current sample rate
		int config = eap_.getCurrentConfig();
		switch (static_cast<DICE::DiceConfig>(config))
		{
		case DICE::DiceConfig::Low:
			cmd |= DICE_EAP_CMD_OPCODE_FLAG_LD_LOW;
			break;
		case DICE::DiceConfig::Mid:
			cmd |= DICE_EAP_CMD_OPCODE_FLAG_LD_MID;
			break;
		case DICE::DiceConfig::High:
			cmd |= DICE_EAP_CMD_OPCODE_FLAG_LD_HIGH;
			break;
		default:
			spdlog::error("Unknown configuration, cannot apply stream config");
			return std::unexpected(IOKitError::Unsupported);
		}

		// Add the execute flag
		cmd |= DICE_EAP_CMD_OPCODE_FLAG_LD_EXECUTE;

		// Execute the command
		std::expected<void, IOKitError> cmdRes = eap_.commandHelper(cmd);
		if (!cmdRes)
		{
			spdlog::error("Failed to activate new stream configuration");
			return std::unexpected(cmdRes.error());
		}

		spdlog::info("Successfully wrote and activated stream configuration");
		return {};
	}

	std::expected<std::vector<std::string>, IOKitError> DiceStreamConfig::getTxNames(unsigned int index)
	{
		if (index >= numTx_)
		{
			spdlog::error("TX stream index out of range: {}", index);
			return std::unexpected(IOKitError::BadArgument);
		}
		return getNamesForBlock(txConfigs_[index]);
	}

	std::expected<std::vector<std::string>, IOKitError> DiceStreamConfig::getRxNames(unsigned int index)
	{
		if (index >= numRx_)
		{
			spdlog::error("RX stream index out of range: {}", index);
			return std::unexpected(IOKitError::BadArgument);
		}
		return getNamesForBlock(rxConfigs_[index]);
	}

	std::vector<std::string> DiceStreamConfig::getNamesForBlock(const ConfigBlock &block)
	{
		std::vector<std::string> result;

		// Names are stored as null-terminated strings in the names array
		char nameBuffer[DICE_EAP_CHANNEL_CONFIG_NAMESTR_LEN_BYTES + 1];
		std::memcpy(nameBuffer, block.names, DICE_EAP_CHANNEL_CONFIG_NAMESTR_LEN_BYTES);
		nameBuffer[DICE_EAP_CHANNEL_CONFIG_NAMESTR_LEN_BYTES] = '\0';

		// Split into individual names
		const char *current = nameBuffer;
		while (*current)
		{
			std::string name(current);
			result.push_back(name);
			current += name.length() + 1;
			// Skip past any additional null terminators
			while (*current == '\0' && current < nameBuffer + DICE_EAP_CHANNEL_CONFIG_NAMESTR_LEN_BYTES)
			{
				current++;
			}
		}

		return result;
	}

	//--------------------------------------------------------------------------
	// DiceEAP Implementation
	//--------------------------------------------------------------------------

	DiceEAP::DiceEAP(DiceAudioDevice &device)
			: device_(device),
				routerExposed_(false),
				routerReadonly_(false),
				routerFlashstored_(false),
				routerNumEntries_(0),
				mixerExposed_(false),
				mixerReadonly_(false),
				mixerFlashstored_(false),
				mixerTxId_(0),
				mixerRxId_(0),
				mixerNumTx_(0),
				mixerNumRx_(0),
				generalSupportDynstream_(false),
				generalSupportFlash_(false),
				generalPeakEnabled_(false),
				generalMaxTx_(0),
				generalMaxRx_(0),
				generalStreamCfgStored_(false),
				generalChip_(0),
				capabilityOffset_(0),
				capabilitySize_(0),
				cmdOffset_(0),
				cmdSize_(0),
				mixerOffset_(0),
				mixerSize_(0),
				peakOffset_(0),
				peakSize_(0),
				newRoutingOffset_(0),
				newRoutingSize_(0),
				newStreamCfgOffset_(0),
				newStreamCfgSize_(0),
				currCfgOffset_(0),
				currCfgSize_(0),
				standaloneOffset_(0),
				standaloneSize_(0),
				appOffset_(0),
				appSize_(0)
	{
		spdlog::debug("DiceEAP constructor");
	}

	DiceEAP::~DiceEAP()
	{
		spdlog::debug("DiceEAP destructor");
	}

	bool DiceEAP::supportsEAP(DiceAudioDevice &device)
	{
		// Use a basic test to see if we can read the EAP capability register
		std::expected<uint32_t, IOKitError> res = device.readReg(DICE_EAP_BASE + DICE_EAP_CAPABILITY_SPACE_OFF);
		if (!res)
		{
			spdlog::debug("Device does not support EAP: Could not read capability offset");
			return false;
		}

		// Also check for space size
		std::expected<uint32_t, IOKitError> sizeRes = device.readReg(DICE_EAP_BASE + DICE_EAP_CAPABILITY_SPACE_SZ);
		if (!sizeRes)
		{
			spdlog::debug("Device does not support EAP: Could not read capability size");
			return false;
		}

		spdlog::debug("Device supports EAP: Capability offset=0x{:x}, size=0x{:x}", res.value(), sizeRes.value());
		return true;
	}

	std::expected<void, IOKitError> DiceEAP::init()
	{
		spdlog::info("Initializing DICE EAP");

		// Read register space offsets and sizes
		// offsets and sizes are returned in quadlets, but we use byte values
		std::expected<uint32_t, IOKitError> res;

		res = device_.readReg(DICE_EAP_BASE + DICE_EAP_CAPABILITY_SPACE_OFF);
		if (!res)
		{
			spdlog::error("Could not read capability offset");
			return std::unexpected(res.error());
		}
		capabilityOffset_ = res.value() * 4; // Convert quadlets to bytes

		res = device_.readReg(DICE_EAP_BASE + DICE_EAP_CAPABILITY_SPACE_SZ);
		if (!res)
		{
			spdlog::error("Could not read capability size");
			return std::unexpected(res.error());
		}
		capabilitySize_ = res.value() * 4;

		res = device_.readReg(DICE_EAP_BASE + DICE_EAP_CMD_SPACE_OFF);
		if (!res)
		{
			spdlog::error("Could not read cmd offset");
			return std::unexpected(res.error());
		}
		cmdOffset_ = res.value() * 4;

		res = device_.readReg(DICE_EAP_BASE + DICE_EAP_CMD_SPACE_SZ);
		if (!res)
		{
			spdlog::error("Could not read cmd size");
			return std::unexpected(res.error());
		}
		cmdSize_ = res.value() * 4;

		res = device_.readReg(DICE_EAP_BASE + DICE_EAP_MIXER_SPACE_OFF);
		if (!res)
		{
			spdlog::error("Could not read mixer offset");
			return std::unexpected(res.error());
		}
		mixerOffset_ = res.value() * 4;

		res = device_.readReg(DICE_EAP_BASE + DICE_EAP_MIXER_SPACE_SZ);
		if (!res)
		{
			spdlog::error("Could not read mixer size");
			return std::unexpected(res.error());
		}
		mixerSize_ = res.value() * 4;

		res = device_.readReg(DICE_EAP_BASE + DICE_EAP_PEAK_SPACE_OFF);
		if (!res)
		{
			spdlog::error("Could not read peak offset");
			return std::unexpected(res.error());
		}
		peakOffset_ = res.value() * 4;

		res = device_.readReg(DICE_EAP_BASE + DICE_EAP_PEAK_SPACE_SZ);
		if (!res)
		{
			spdlog::error("Could not read peak size");
			return std::unexpected(res.error());
		}
		peakSize_ = res.value() * 4;

		res = device_.readReg(DICE_EAP_BASE + DICE_EAP_NEW_ROUTING_SPACE_OFF);
		if (!res)
		{
			spdlog::error("Could not read new routing offset");
			return std::unexpected(res.error());
		}
		newRoutingOffset_ = res.value() * 4;

		res = device_.readReg(DICE_EAP_BASE + DICE_EAP_NEW_ROUTING_SPACE_SZ);
		if (!res)
		{
			spdlog::error("Could not read new routing size");
			return std::unexpected(res.error());
		}
		newRoutingSize_ = res.value() * 4;

		res = device_.readReg(DICE_EAP_BASE + DICE_EAP_NEW_STREAM_CFG_SPACE_OFF);
		if (!res)
		{
			spdlog::error("Could not read new stream cfg offset");
			return std::unexpected(res.error());
		}
		newStreamCfgOffset_ = res.value() * 4;

		res = device_.readReg(DICE_EAP_BASE + DICE_EAP_NEW_STREAM_CFG_SPACE_SZ);
		if (!res)
		{
			spdlog::error("Could not read new stream cfg size");
			return std::unexpected(res.error());
		}
		newStreamCfgSize_ = res.value() * 4;

		res = device_.readReg(DICE_EAP_BASE + DICE_EAP_CURR_CFG_SPACE_OFF);
		if (!res)
		{
			spdlog::error("Could not read current cfg offset");
			return std::unexpected(res.error());
		}
		currCfgOffset_ = res.value() * 4;

		res = device_.readReg(DICE_EAP_BASE + DICE_EAP_CURR_CFG_SPACE_SZ);
		if (!res)
		{
			spdlog::error("Could not read current cfg size");
			return std::unexpected(res.error());
		}
		currCfgSize_ = res.value() * 4;

		res = device_.readReg(DICE_EAP_BASE + DICE_EAP_STAND_ALONE_CFG_SPACE_OFF);
		if (!res)
		{
			spdlog::error("Could not read standalone cfg offset");
			return std::unexpected(res.error());
		}
		standaloneOffset_ = res.value() * 4;

		res = device_.readReg(DICE_EAP_BASE + DICE_EAP_STAND_ALONE_CFG_SPACE_SZ);
		if (!res)
		{
			spdlog::error("Could not read standalone cfg size");
			return std::unexpected(res.error());
		}
		standaloneSize_ = res.value() * 4;

		res = device_.readReg(DICE_EAP_BASE + DICE_EAP_APP_SPACE_OFF);
		if (!res)
		{
			spdlog::error("Could not read app offset");
			return std::unexpected(res.error());
		}
		appOffset_ = res.value() * 4;

		res = device_.readReg(DICE_EAP_BASE + DICE_EAP_APP_SPACE_SZ);
		if (!res)
		{
			spdlog::error("Could not read app size");
			return std::unexpected(res.error());
		}
		appSize_ = res.value() * 4;

		spdlog::debug("EAP Parameter Space info:");
		spdlog::debug(" Capability        : offset=0x{:x} size={}", capabilityOffset_, capabilitySize_);
		spdlog::debug(" Command           : offset=0x{:x} size={}", cmdOffset_, cmdSize_);
		spdlog::debug(" Mixer             : offset=0x{:x} size={}", mixerOffset_, mixerSize_);
		spdlog::debug(" Peak              : offset=0x{:x} size={}", peakOffset_, peakSize_);
		spdlog::debug(" New Routing       : offset=0x{:x} size={}", newRoutingOffset_, newRoutingSize_);
		spdlog::debug(" New Stream Config : offset=0x{:x} size={}", newStreamCfgOffset_, newStreamCfgSize_);
		spdlog::debug(" Current Config    : offset=0x{:x} size={}", currCfgOffset_, currCfgSize_);
		spdlog::debug(" Standalone        : offset=0x{:x} size={}", standaloneOffset_, standaloneSize_);
		spdlog::debug(" Application       : offset=0x{:x} size={}", appOffset_, appSize_);

		// Read capability registers
		std::expected<uint32_t, IOKitError> routerCapRes = device_.readReg(DICE_EAP_BASE + capabilityOffset_ + DICE_EAP_CAPABILITY_ROUTER);
		if (!routerCapRes)
		{
			spdlog::error("Could not read router capabilities");
			return std::unexpected(routerCapRes.error());
		}

		uint32_t routerCap = routerCapRes.value();
		routerExposed_ = (routerCap >> DICE_EAP_CAP_ROUTER_EXPOSED) & 0x01;
		routerReadonly_ = (routerCap >> DICE_EAP_CAP_ROUTER_READONLY) & 0x01;
		routerFlashstored_ = (routerCap >> DICE_EAP_CAP_ROUTER_FLASHSTORED) & 0x01;
		routerNumEntries_ = (routerCap >> DICE_EAP_CAP_ROUTER_MAXROUTES) & 0xFFFF;

		spdlog::debug("Router capabilities: exposed={}, readonly={}, flashstored={}, entries={}",
									routerExposed_, routerReadonly_, routerFlashstored_, routerNumEntries_);

		std::expected<uint32_t, IOKitError> mixerCapRes = device_.readReg(DICE_EAP_BASE + capabilityOffset_ + DICE_EAP_CAPABILITY_MIXER);
		if (!mixerCapRes)
		{
			spdlog::error("Could not read mixer capabilities");
			return std::unexpected(mixerCapRes.error());
		}

		uint32_t mixerCap = mixerCapRes.value();
		mixerExposed_ = (mixerCap >> DICE_EAP_CAP_MIXER_EXPOSED) & 0x01;
		mixerReadonly_ = (mixerCap >> DICE_EAP_CAP_MIXER_READONLY) & 0x01;
		mixerFlashstored_ = (mixerCap >> DICE_EAP_CAP_MIXER_FLASHSTORED) & 0x01;
		mixerTxId_ = (mixerCap >> DICE_EAP_CAP_MIXER_IN_DEV) & 0x0F;
		mixerRxId_ = (mixerCap >> DICE_EAP_CAP_MIXER_OUT_DEV) & 0x0F;
		mixerNumTx_ = (mixerCap >> DICE_EAP_CAP_MIXER_INPUTS) & 0xFF;
		mixerNumRx_ = (mixerCap >> DICE_EAP_CAP_MIXER_OUTPUTS) & 0xFF;

		spdlog::debug("Mixer capabilities: exposed={}, readonly={}, flashstored={}",
									mixerExposed_, mixerReadonly_, mixerFlashstored_);
		spdlog::debug("Mixer I/O: TX ID={}, RX ID={}, Inputs={}, Outputs={}",
									mixerTxId_, mixerRxId_, mixerNumTx_, mixerNumRx_);

		std::expected<uint32_t, IOKitError> generalCapRes = device_.readReg(DICE_EAP_BASE + capabilityOffset_ + DICE_EAP_CAPABILITY_GENERAL);
		if (!generalCapRes)
		{
			spdlog::error("Could not read general capabilities");
			return std::unexpected(generalCapRes.error());
		}

		uint32_t generalCap = generalCapRes.value();
		generalSupportDynstream_ = (generalCap >> DICE_EAP_CAP_GENERAL_STRM_CFG_EN) & 0x01;
		generalSupportFlash_ = (generalCap >> DICE_EAP_CAP_GENERAL_FLASH_EN) & 0x01;
		generalPeakEnabled_ = (generalCap >> DICE_EAP_CAP_GENERAL_PEAK_EN) & 0x01;
		generalMaxTx_ = (generalCap >> DICE_EAP_CAP_GENERAL_MAX_TX_STREAM) & 0x0F;
		generalMaxRx_ = (generalCap >> DICE_EAP_CAP_GENERAL_MAX_RX_STREAM) & 0x0F;
		generalStreamCfgStored_ = (generalCap >> DICE_EAP_CAP_GENERAL_STRM_CFG_FLS) & 0x01;
		generalChip_ = (generalCap >> DICE_EAP_CAP_GENERAL_CHIP) & 0xFFFF;

		spdlog::debug("General capabilities: dynamic stream={}, flash={}, peak={}, stream stored={}",
									generalSupportDynstream_, generalSupportFlash_, generalPeakEnabled_, generalStreamCfgStored_);
		spdlog::debug("Max streams: TX={}, RX={}", generalMaxTx_, generalMaxRx_);

		switch (generalChip_)
		{
		case DICE_EAP_CAP_GENERAL_CHIP_DICEII:
			spdlog::info("Detected DICE II chipset");
			break;
		case DICE_EAP_CAP_GENERAL_CHIP_DICEMINI:
			spdlog::info("Detected DICE Mini chipset (TCD2210)");
			break;
		case DICE_EAP_CAP_GENERAL_CHIP_DICEJR:
			spdlog::info("Detected DICE Jr chipset (TCD2220)");
			break;
		default:
			spdlog::info("Unknown DICE chipset: {}", generalChip_);
			break;
		}

		// Create the configuration objects
		if (routerExposed_)
		{
			currCfgRoutingLow_ = std::make_unique<DiceRouterConfig>(*this);
			currCfgRoutingMid_ = std::make_unique<DiceRouterConfig>(*this);
			currCfgRoutingHigh_ = std::make_unique<DiceRouterConfig>(*this);
		}

		// Create stream config objects
		currCfgStreamLow_ = std::make_unique<DiceStreamConfig>(*this);
		currCfgStreamMid_ = std::make_unique<DiceStreamConfig>(*this);
		currCfgStreamHigh_ = std::make_unique<DiceStreamConfig>(*this);

		// Create peak meter object if supported
		if (generalPeakEnabled_)
		{
			peakSpace_ = std::make_unique<DicePeakSpace>(*this);
		}

		// Create standalone configuration object
		if (standaloneOffset_ > 0 && standaloneSize_ > 0)
		{
			standaloneConfig_ = std::make_unique<DiceStandaloneConfig>(*this);
			// Read the initial configuration
			auto standaloneRes = standaloneConfig_->read();
			if (!standaloneRes)
			{
				spdlog::warn("Failed to read standalone configuration, but continuing");
				// Don't fail initialization for this
			}
		}

		// Create mixer object if supported
		if (mixerExposed_)
		{
			mixer_ = std::make_unique<DiceMixer>(*this);
			auto mixerRes = mixer_->init();
			if (!mixerRes)
			{
				spdlog::warn("Failed to initialize mixer, but continuing");
				// Don't fail initialization for this
			}
			else
			{
				spdlog::info("Successfully initialized mixer with {} inputs and {} outputs",
										 mixerNumTx_, mixerNumRx_);
			}
		}

		return {};
	}

	std::expected<void, IOKitError> DiceEAP::update()
	{
		spdlog::debug("Updating EAP state");

		// First update configuration cache from device
		auto result = updateConfigurationCache();
		if (!result)
		{
			spdlog::error("Failed to update configuration cache");
			return std::unexpected(result.error());
		}

		// Now setup sources and destinations based on current sample rate
		// This ensures router has proper naming for all channels
		DiceRouterConfig *router = getActiveRouterConfig();
		if (router)
		{
			// Make sure we have the latest configuration
			int config = device_.getCurrentConfig();
			switch (static_cast<DICE::DiceConfig>(config))
			{
			case DICE::DiceConfig::Low:
				spdlog::debug("Setting up low-rate sources and destinations");
				break;
			case DICE::DiceConfig::Mid:
				spdlog::debug("Setting up mid-rate sources and destinations");
				break;
			case DICE::DiceConfig::High:
				spdlog::debug("Setting up high-rate sources and destinations");
				break;
			default:
				spdlog::warn("Unknown sample rate configuration: {}", config);
				break;
			}
		}

		// Update peak meters if enabled
		if (generalPeakEnabled_ && peakSpace_)
		{
			auto res = peakSpace_->read(0);
			if (!res)
			{
				spdlog::error("Failed to update peak meters");
				// Don't return error for this - peaks are not critical
			}
		}

		spdlog::debug("EAP state update completed");
		return {};
	}

	std::expected<void, IOKitError> DiceEAP::loadFlashConfig()
	{
		spdlog::info("Loading configuration from flash");
		if (!generalSupportFlash_)
		{
			spdlog::error("Flash configuration not supported");
			return std::unexpected(IOKitError::Unsupported);
		}

		// Simple implementation to start with
		uint32_t cmd = DICE_EAP_CMD_OPCODE_LD_FLASH_CFG | DICE_EAP_CMD_OPCODE_FLAG_LD_EXECUTE;
		return commandHelper(cmd);
	}

	std::expected<void, IOKitError> DiceEAP::storeFlashConfig()
	{
		spdlog::info("Storing configuration to flash");
		if (!generalSupportFlash_)
		{
			spdlog::error("Flash configuration not supported");
			return std::unexpected(IOKitError::Unsupported);
		}

		// Simple implementation to start with
		uint32_t cmd = DICE_EAP_CMD_OPCODE_ST_FLASH_CFG | DICE_EAP_CMD_OPCODE_FLAG_LD_EXECUTE;
		return commandHelper(cmd);
	}

	DiceEAP::CommandStatus DiceEAP::operationBusy()
	{
		// Read command register to check if it's still busy
		std::expected<uint32_t, IOKitError> cmdRes = device_.readReg(DICE_EAP_BASE + cmdOffset_ + DICE_EAP_COMMAND_OPCODE);
		if (!cmdRes)
		{
			spdlog::error("Failed to read command register");
			return CommandStatus::Error;
		}

		uint32_t cmd = cmdRes.value();
		if (cmd & DICE_EAP_CMD_OPCODE_FLAG_LD_EXECUTE)
		{
			return CommandStatus::Busy;
		}

		// Check status
		std::expected<uint32_t, IOKitError> retvalRes = device_.readReg(DICE_EAP_BASE + cmdOffset_ + DICE_EAP_COMMAND_RETVAL);
		if (!retvalRes)
		{
			spdlog::error("Failed to read command return value");
			return CommandStatus::Error;
		}

		if (retvalRes.value() != 0)
		{
			spdlog::error("Command failed with status: {}", retvalRes.value());
			return CommandStatus::Error;
		}

		return CommandStatus::Done;
	}

	DiceEAP::CommandStatus DiceEAP::waitForOperationEnd(int maxWaitTimeMs)
	{
		using namespace std::chrono;
		using Clock = high_resolution_clock;

		auto start = Clock::now();
		auto now = start;
		CommandStatus status;

		do
		{
			status = operationBusy();
			if (status != CommandStatus::Busy)
			{
				return status;
			}

			// Sleep a bit to avoid hammering the device
			std::this_thread::sleep_for(milliseconds(5));
			now = Clock::now();
		} while (duration_cast<milliseconds>(now - start).count() < maxWaitTimeMs);

		return CommandStatus::Timeout;
	}

	std::expected<void, IOKitError> DiceEAP::updateConfigurationCache()
	{
		spdlog::debug("Updating configuration cache");

		// Read router configurations for each sample rate range
		if (currCfgRoutingLow_)
		{
			auto res = currCfgRoutingLow_->read(DICE_EAP_CURRCFG_LOW_ROUTER);
			if (!res)
			{
				spdlog::error("Failed to read low-rate router configuration");
				return std::unexpected(res.error());
			}
			spdlog::debug("Updated low-rate router configuration");
		}

		if (currCfgRoutingMid_)
		{
			auto res = currCfgRoutingMid_->read(DICE_EAP_CURRCFG_MID_ROUTER);
			if (!res)
			{
				spdlog::error("Failed to read mid-rate router configuration");
				return std::unexpected(res.error());
			}
			spdlog::debug("Updated mid-rate router configuration");
		}

		if (currCfgRoutingHigh_)
		{
			auto res = currCfgRoutingHigh_->read(DICE_EAP_CURRCFG_HIGH_ROUTER);
			if (!res)
			{
				spdlog::error("Failed to read high-rate router configuration");
				return std::unexpected(res.error());
			}
			spdlog::debug("Updated high-rate router configuration");
		}

		// Read stream configurations for each sample rate range
		if (currCfgStreamLow_)
		{
			auto res = currCfgStreamLow_->read(DICE_EAP_CURRCFG_LOW_STREAM);
			if (!res)
			{
				spdlog::error("Failed to read low-rate stream configuration");
				return std::unexpected(res.error());
			}
			spdlog::debug("Updated low-rate stream configuration");
		}

		if (currCfgStreamMid_)
		{
			auto res = currCfgStreamMid_->read(DICE_EAP_CURRCFG_MID_STREAM);
			if (!res)
			{
				spdlog::error("Failed to read mid-rate stream configuration");
				return std::unexpected(res.error());
			}
			spdlog::debug("Updated mid-rate stream configuration");
		}

		if (currCfgStreamHigh_)
		{
			auto res = currCfgStreamHigh_->read(DICE_EAP_CURRCFG_HIGH_STREAM);
			if (!res)
			{
				spdlog::error("Failed to read high-rate stream configuration");
				return std::unexpected(res.error());
			}
			spdlog::debug("Updated high-rate stream configuration");
		}

		// Update peak meters if enabled
		if (generalPeakEnabled_ && peakSpace_)
		{
			auto res = peakSpace_->read(0);
			if (!res)
			{
				spdlog::error("Failed to read peak data");
				return std::unexpected(res.error());
			}
			spdlog::debug("Updated peak meters");
		}

		// Update standalone configuration if available
		if (standaloneConfig_)
		{
			auto res = standaloneConfig_->read();
			if (!res)
			{
				spdlog::warn("Failed to read standalone configuration");
				// Don't fail for this, just log warning
			}
			else
			{
				spdlog::debug("Updated standalone configuration");
			}
		}

		return {};
	}

	DiceRouterConfig *DiceEAP::getActiveRouterConfig()
	{
		// Get router configuration based on current sample rate
		int config = device_.getCurrentConfig();
		switch (static_cast<DICE::DiceConfig>(config))
		{
		case DICE::DiceConfig::Low:
			return currCfgRoutingLow_.get();
		case DICE::DiceConfig::Mid:
			return currCfgRoutingMid_.get();
		case DICE::DiceConfig::High:
			return currCfgRoutingHigh_.get();
		default:
			return nullptr;
		}
	}

	DiceStreamConfig *DiceEAP::getActiveStreamConfig()
	{
		// Get stream configuration based on current sample rate
		int config = device_.getCurrentConfig();
		switch (static_cast<DICE::DiceConfig>(config))
		{
		case DICE::DiceConfig::Low:
			return currCfgStreamLow_.get();
		case DICE::DiceConfig::Mid:
			return currCfgStreamMid_.get();
		case DICE::DiceConfig::High:
			return currCfgStreamHigh_.get();
		default:
			return nullptr;
		}
	}

	std::expected<void, IOKitError> DiceEAP::setupDefaultRouterConfig()
	{
		spdlog::info("Setting up default router configuration");
		// Get current config
		int config = device_.getCurrentConfig();

		switch (static_cast<DICE::DiceConfig>(config))
		{
		case DICE::DiceConfig::Low:
			return setupDefaultRouterConfigLow();
		case DICE::DiceConfig::Mid:
			return setupDefaultRouterConfigMid();
		case DICE::DiceConfig::High:
			return setupDefaultRouterConfigHigh();
		default:
			spdlog::error("Unknown configuration");
			return std::unexpected(IOKitError::Unsupported);
		}
	}

	std::expected<std::vector<std::string>, IOKitError> DiceEAP::getTxNames(unsigned int index)
	{
		DiceStreamConfig *config = getActiveStreamConfig();
		if (!config)
		{
			spdlog::error("No active stream configuration");
			return std::unexpected(IOKitError::NoDevice);
		}

		return config->getTxNames(index);
	}

	std::expected<std::vector<std::string>, IOKitError> DiceEAP::getRxNames(unsigned int index)
	{
		DiceStreamConfig *config = getActiveStreamConfig();
		if (!config)
		{
			spdlog::error("No active stream configuration");
			return std::unexpected(IOKitError::NoDevice);
		}

		return config->getRxNames(index);
	}

	std::expected<void, IOKitError> DiceEAP::readReg(RegBase base, uint32_t offset, uint32_t *value)
	{
		uint64_t addr = offsetGen(base, offset, 4);
		if (addr == 0)
		{
			spdlog::error("Invalid register address");
			return std::unexpected(IOKitError::BadArgument);
		}

		std::expected<uint32_t, IOKitError> res = device_.readReg(addr);
		if (!res)
		{
			return std::unexpected(res.error());
		}

		*value = res.value();
		return {};
	}

	std::expected<void, IOKitError> DiceEAP::writeReg(RegBase base, uint32_t offset, uint32_t value)
	{
		uint64_t addr = offsetGen(base, offset, 4);
		if (addr == 0)
		{
			spdlog::error("Invalid register address");
			return std::unexpected(IOKitError::BadArgument);
		}

		return device_.writeReg(addr, value);
	}

	std::expected<void, IOKitError> DiceEAP::readRegBlock(RegBase base, uint32_t offset, uint32_t *data, size_t size)
	{
		uint64_t addr = offsetGen(base, offset, size);
		if (addr == 0)
		{
			spdlog::error("Invalid register address");
			return std::unexpected(IOKitError::BadArgument);
		}

		std::expected<std::vector<uint32_t>, IOKitError> res = device_.readRegBlock(addr, size);
		if (!res)
		{
			return std::unexpected(res.error());
		}

		// Copy data to buffer
		std::vector<uint32_t> result = res.value();
		std::memcpy(data, result.data(), size);
		return {};
	}

	std::expected<void, IOKitError> DiceEAP::writeRegBlock(RegBase base, uint32_t offset, const uint32_t *data, size_t size)
	{
		uint64_t addr = offsetGen(base, offset, size);
		if (addr == 0)
		{
			spdlog::error("Invalid register address");
			return std::unexpected(IOKitError::BadArgument);
		}

		return device_.writeRegBlock(addr, data, size);
	}

	std::expected<void, IOKitError> DiceEAP::commandHelper(uint32_t cmd)
	{
		// Write the command to the command register
		std::expected<void, IOKitError> res = writeReg(RegBase::Command, DICE_EAP_COMMAND_OPCODE, cmd);
		if (!res)
		{
			spdlog::error("Failed to write command");
			return std::unexpected(res.error());
		}

		// Wait for completion if the execute flag is set
		if (cmd & DICE_EAP_CMD_OPCODE_FLAG_LD_EXECUTE)
		{
			CommandStatus status = waitForOperationEnd();
			if (status != CommandStatus::Done)
			{
				spdlog::error("Command failed with status: {}", static_cast<int>(status));
				return std::unexpected(IOKitError::Timeout);
			}
		}

		return {};
	}

	int DiceEAP::getCurrentConfig() const
	{
		return device_.getCurrentConfig();
	}

	std::expected<std::vector<uint32_t>, IOKitError> DiceEAP::readApplicationSpace(uint32_t offset, size_t size)
	{
		spdlog::debug("Reading application space at offset 0x{:x}, size {}", offset, size);

		// Check if application space is available
		if (appOffset_ == 0 || appSize_ == 0)
		{
			spdlog::error("Application space not available");
			return std::unexpected(IOKitError::Unsupported);
		}

		// Check bounds
		if (offset + size > appSize_)
		{
			spdlog::error("Application space read out of bounds: offset=0x{:x}, size={}, max=0x{:x}",
										offset, size, appSize_);
			return std::unexpected(IOKitError::BadArgument);
		}

		// Calculate number of 32-bit words to read
		size_t numWords = (size + 3) / 4; // Round up to nearest word
		std::vector<uint32_t> data(numWords, 0);

		// Read the data from the application space
		auto result = device_.readRegBlock(DICE_EAP_BASE + appOffset_ + offset, numWords * 4);
		if (!result)
		{
			spdlog::error("Failed to read application space data");
			return std::unexpected(result.error());
		}

		return result.value();
	}

	uint64_t DiceEAP::offsetGen(RegBase base, uint32_t offset, size_t size)
	{
		uint32_t baseOffset = 0;
		uint32_t maxSize = 0;

		switch (base)
		{
		case RegBase::Base:
			return DICE_EAP_BASE + offset;

		case RegBase::Capability:
			baseOffset = capabilityOffset_;
			maxSize = capabilitySize_;
			break;

		case RegBase::Command:
			baseOffset = cmdOffset_;
			maxSize = cmdSize_;
			break;

		case RegBase::Mixer:
			baseOffset = mixerOffset_;
			maxSize = mixerSize_;
			break;

		case RegBase::Peak:
			baseOffset = peakOffset_;
			maxSize = peakSize_;
			break;

		case RegBase::NewRouting:
			baseOffset = newRoutingOffset_;
			maxSize = newRoutingSize_;
			break;

		case RegBase::NewStreamCfg:
			baseOffset = newStreamCfgOffset_;
			maxSize = newStreamCfgSize_;
			break;

		case RegBase::CurrentCfg:
			baseOffset = currCfgOffset_;
			maxSize = currCfgSize_;
			break;

		case RegBase::Standalone:
			baseOffset = standaloneOffset_;
			maxSize = standaloneSize_;
			break;

		case RegBase::Application:
			baseOffset = appOffset_;
			maxSize = appSize_;
			break;

		default:
			return 0;
		}

		// Check bounds
		if (offset + size > maxSize)
		{
			spdlog::error("Register access out of bounds: offset=0x{:x}, size={}, maxSize=0x{:x}",
										offset, size, maxSize);
			return 0;
		}

		return DICE_EAP_BASE + baseOffset + offset;
	}

	std::expected<void, IOKitError> DiceEAP::addRoute(
			DICE::RouteSource srcId, unsigned int srcBase,
			DICE::RouteDestination dstId, unsigned int dstBase)
	{
		// Simple implementation for now
		DiceRouterConfig *router = getActiveRouterConfig();
		if (!router)
		{
			spdlog::error("No active router configuration");
			return std::unexpected(IOKitError::NoDevice);
		}

		return router->createRoute(
				static_cast<unsigned char>(srcId) + srcBase,
				static_cast<unsigned char>(dstId) + dstBase);
	}

	std::expected<void, IOKitError> DiceEAP::setupDefaultRouterConfigLow()
	{
		spdlog::debug("Setting up default router config for low sample rates");
		if (!currCfgRoutingLow_)
		{
			spdlog::error("No low-rate router configuration");
			return std::unexpected(IOKitError::NoDevice);
		}

		// Clear all routes first
		auto res = currCfgRoutingLow_->clearRoutes();
		if (!res)
		{
			return std::unexpected(res.error());
		}

		// Setup default routes based on chip type
		switch (generalChip_)
		{
		case DICE_EAP_CAP_GENERAL_CHIP_DICEII:
			// router/EAP not fully supported for DICE II
			break;
		case DICE_EAP_CAP_GENERAL_CHIP_DICEJR:
			// Second audio port (unique to junior)
			for (unsigned int i = 0; i < 8; i++)
			{
				addRoute(DICE::RouteSource::ARX0, i + 8, DICE::RouteDestination::InS1, i);
			}
			// fallthrough intentional - Junior shares most functionality with Mini
		case DICE_EAP_CAP_GENERAL_CHIP_DICEMINI:
			// The 1394 stream receivers
			for (unsigned int i = 0; i < 8; i++)
			{
				addRoute(DICE::RouteSource::InS0, i, DICE::RouteDestination::ATX0, i);
			}
			// On the Junior, route second audio port to the second half of ATX0
			if (generalChip_ == DICE_EAP_CAP_GENERAL_CHIP_DICEJR)
			{
				for (unsigned int i = 0; i < 8; i++)
				{
					addRoute(DICE::RouteSource::InS1, i, DICE::RouteDestination::ATX0, i + 8);
				}
			}
			for (unsigned int i = 0; i < 8; i++)
			{
				addRoute(DICE::RouteSource::ADAT, i, DICE::RouteDestination::ATX1, i);
			}
			for (unsigned int i = 0; i < 8; i++)
			{
				addRoute(DICE::RouteSource::AES, i, DICE::RouteDestination::ATX1, i + 8);
			}
			// The audio ports - ensure they are not muted
			for (unsigned int i = 0; i < 8; i++)
			{
				addRoute(DICE::RouteSource::ARX0, i, DICE::RouteDestination::InS0, i);
			}
			// The AES receiver - muted by default
			for (unsigned int i = 0; i < 8; i++)
			{
				addRoute(DICE::RouteSource::Muted, 0, DICE::RouteDestination::AES, i);
			}
			// The ADAT receiver - muted by default
			for (unsigned int i = 0; i < 8; i++)
			{
				addRoute(DICE::RouteSource::Muted, 0, DICE::RouteDestination::ADAT, i);
			}
			// The Mixer inputs
			for (unsigned int i = 0; i < 8; i++)
			{
				addRoute(DICE::RouteSource::InS0, i, DICE::RouteDestination::Mixer0, i);
			}
			for (unsigned int i = 0; i < 8; i++)
			{
				addRoute(DICE::RouteSource::ADAT, i, DICE::RouteDestination::Mixer0, i + 8);
			}
			for (unsigned int i = 0; i < 2; i++)
			{
				addRoute(DICE::RouteSource::Muted, 0, DICE::RouteDestination::Mixer1, i);
			}
			// The ARM audio port - muted by default
			for (unsigned int i = 0; i < 8; i++)
			{
				addRoute(DICE::RouteSource::Muted, 0, DICE::RouteDestination::ARM, i);
			}
			// Mute destination - must be connected to mute source
			addRoute(DICE::RouteSource::Muted, 0, DICE::RouteDestination::Muted, 0);
			break;
		default:
			// Unsupported chip - just clear routes
			break;
		}

		return {};
	}

	std::expected<void, IOKitError> DiceEAP::setupDefaultRouterConfigMid()
	{
		spdlog::debug("Setting up default router config for mid sample rates");
		if (!currCfgRoutingMid_)
		{
			spdlog::error("No mid-rate router configuration");
			return std::unexpected(IOKitError::NoDevice);
		}

		// For most devices, mid-rate config is the same as low-rate
		auto res = currCfgRoutingMid_->clearRoutes();
		if (!res)
		{
			return std::unexpected(res.error());
		}

		// Use the same routing as low-rate
		switch (generalChip_)
		{
		case DICE_EAP_CAP_GENERAL_CHIP_DICEII:
			// router/EAP not fully supported for DICE II
			break;
		case DICE_EAP_CAP_GENERAL_CHIP_DICEJR:
			// Second audio port (unique to junior)
			for (unsigned int i = 0; i < 8; i++)
			{
				addRoute(DICE::RouteSource::ARX0, i + 8, DICE::RouteDestination::InS1, i);
			}
			// fallthrough intentional
		case DICE_EAP_CAP_GENERAL_CHIP_DICEMINI:
			// The 1394 stream receivers
			for (unsigned int i = 0; i < 8; i++)
			{
				addRoute(DICE::RouteSource::InS0, i, DICE::RouteDestination::ATX0, i);
			}
			if (generalChip_ == DICE_EAP_CAP_GENERAL_CHIP_DICEJR)
			{
				for (unsigned int i = 0; i < 8; i++)
				{
					addRoute(DICE::RouteSource::InS1, i, DICE::RouteDestination::ATX0, i + 8);
				}
			}
			for (unsigned int i = 0; i < 8; i++)
			{
				addRoute(DICE::RouteSource::ADAT, i, DICE::RouteDestination::ATX1, i);
			}
			for (unsigned int i = 0; i < 8; i++)
			{
				addRoute(DICE::RouteSource::AES, i, DICE::RouteDestination::ATX1, i + 8);
			}
			// The audio ports
			for (unsigned int i = 0; i < 8; i++)
			{
				addRoute(DICE::RouteSource::ARX0, i, DICE::RouteDestination::InS0, i);
			}
			// The AES receiver
			for (unsigned int i = 0; i < 8; i++)
			{
				addRoute(DICE::RouteSource::Muted, 0, DICE::RouteDestination::AES, i);
			}
			// The ADAT receiver
			for (unsigned int i = 0; i < 8; i++)
			{
				addRoute(DICE::RouteSource::Muted, 0, DICE::RouteDestination::ADAT, i);
			}
			// The Mixer inputs
			for (unsigned int i = 0; i < 8; i++)
			{
				addRoute(DICE::RouteSource::InS0, i, DICE::RouteDestination::Mixer0, i);
			}
			for (unsigned int i = 0; i < 8; i++)
			{
				addRoute(DICE::RouteSource::ADAT, i, DICE::RouteDestination::Mixer0, i + 8);
			}
			for (unsigned int i = 0; i < 2; i++)
			{
				addRoute(DICE::RouteSource::Muted, 0, DICE::RouteDestination::Mixer1, i);
			}
			// The ARM audio port
			for (unsigned int i = 0; i < 8; i++)
			{
				addRoute(DICE::RouteSource::Muted, 0, DICE::RouteDestination::ARM, i);
			}
			// Mute
			addRoute(DICE::RouteSource::Muted, 0, DICE::RouteDestination::Muted, 0);
			break;
		default:
			// Unsupported chip
			break;
		}

		return {};
	}

	std::expected<void, IOKitError> DiceEAP::setupDefaultRouterConfigHigh()
	{
		spdlog::debug("Setting up default router config for high sample rates");
		if (!currCfgRoutingHigh_)
		{
			spdlog::error("No high-rate router configuration");
			return std::unexpected(IOKitError::NoDevice);
		}

		// For most devices, high-rate config is the same as low-rate with some channel count differences
		auto res = currCfgRoutingHigh_->clearRoutes();
		if (!res)
		{
			return std::unexpected(res.error());
		}

		// Just use the same routing as mid-rate
		setupDefaultRouterConfigMid();

		return {};
	}

	// Helper methods for matrix mixer implementation

	std::string DiceEAP::getSourceName(DICE::RouteSource srcId, unsigned int channel)
	{
		char buffer[32];
		const char *srcName = "UNKNOWN";

		switch (srcId)
		{
		case DICE::RouteSource::AES:
			srcName = "AES";
			break;
		case DICE::RouteSource::ADAT:
			srcName = "ADAT";
			break;
		case DICE::RouteSource::Mixer:
			srcName = "MXR";
			break;
		case DICE::RouteSource::InS0:
			srcName = "INS0";
			break;
		case DICE::RouteSource::InS1:
			srcName = "INS1";
			break;
		case DICE::RouteSource::ARM:
			srcName = "ARM";
			break;
		case DICE::RouteSource::ARX0:
			srcName = "ARX0";
			break;
		case DICE::RouteSource::ARX1:
			srcName = "ARX1";
			break;
		case DICE::RouteSource::Muted:
			srcName = "MUTE";
			break;
		default:
			break;
		}

		snprintf(buffer, sizeof(buffer), "%s:%02d", srcName, channel);
		return std::string(buffer);
	}

	std::string DiceEAP::getDestinationName(DICE::RouteDestination dstId, unsigned int channel)
	{
		char buffer[32];
		const char *dstName = "UNKNOWN";

		switch (dstId)
		{
		case DICE::RouteDestination::AES:
			dstName = "AES";
			break;
		case DICE::RouteDestination::ADAT:
			dstName = "ADAT";
			break;
		case DICE::RouteDestination::Mixer0:
			dstName = "MXR0";
			break;
		case DICE::RouteDestination::Mixer1:
			dstName = "MXR1";
			break;
		case DICE::RouteDestination::InS0:
			dstName = "INS0";
			break;
		case DICE::RouteDestination::InS1:
			dstName = "INS1";
			break;
		case DICE::RouteDestination::ARM:
			dstName = "ARM";
			break;
		case DICE::RouteDestination::ATX0:
			dstName = "ATX0";
			break;
		case DICE::RouteDestination::ATX1:
			dstName = "ATX1";
			break;
		case DICE::RouteDestination::Muted:
			dstName = "MUTE";
			break;
		default:
			break;
		}

		snprintf(buffer, sizeof(buffer), "%s:%02d", dstName, channel);
		return std::string(buffer);
	}

} // namespace FWA
