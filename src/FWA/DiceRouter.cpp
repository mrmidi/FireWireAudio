// src/FWA/DiceRouter.cpp
#include "FWA/DiceRouter.hpp"
#include "FWA/DiceEAP.hpp"
#include "FWA/DiceDefines.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <sstream> // For stringstream
#include <iomanip> // For setw

namespace FWA
{

	DiceRouter::DiceRouter(DiceEAP &eap)
			: eap_(eap), pointsCached_(false)
	{
		spdlog::debug("DiceRouter constructor");
	}

	DiceRouter::~DiceRouter()
	{
		spdlog::debug("DiceRouter destructor");
	}

	std::expected<void, IOKitError> DiceRouter::update()
	{
		spdlog::debug("Updating router state from device");

		// Clear cache
		sources_.clear();
		destinations_.clear();
		pointsCached_ = false;

		// Ensure EAP state is updated
		auto result = eap_.update();
		if (!result)
		{
			spdlog::error("Failed to update EAP state: {}", static_cast<int>(result.error()));
			return std::unexpected(result.error());
		}

		return {};
	}

	std::vector<DiceRouter::Route> DiceRouter::getRoutes() const
	{
		spdlog::debug("Getting all routes");
		std::vector<Route> routes;

		// Get the active router config
		DiceRouterConfig *router = eap_.getActiveRouterConfig();
		if (!router)
		{
			spdlog::error("No active router configuration");
			return routes;
		}

		// Get destinations
		std::vector<RoutePoint> dests = getDestinations();

		// For each destination, get its source
		for (const auto &dest : dests)
		{
			auto sourceResult = getSource(dest);
			if (sourceResult)
			{
				routes.push_back(Route(sourceResult.value(), dest));
			}
		}

		return routes;
	}

	std::vector<DiceRouter::RoutePoint> DiceRouter::getSources() const
	{
		// If we have a cached list, return it
		if (pointsCached_ && !sources_.empty())
		{
			return sources_;
		}

		// Otherwise, build the list
		std::vector<RoutePoint> sources;

		// Add all standard sources
		for (int srcId = 0; srcId < static_cast<int>(DICE::RouteSource::Invalid); srcId++)
		{
			DICE::RouteSource source = static_cast<DICE::RouteSource>(srcId);

			// Skip muted source
			if (source == DICE::RouteSource::Muted)
				continue;

			// Determine channel count for this source
			int numChannels = 0;
			switch (source)
			{
			case DICE::RouteSource::AES:
				numChannels = 8; // AES typically has 8 channels
				break;
			case DICE::RouteSource::ADAT:
				numChannels = 8; // ADAT has 8 channels
				break;
			case DICE::RouteSource::Mixer:
				numChannels = 16; // Mixer typically has 16 outputs
				break;
			case DICE::RouteSource::InS0:
			case DICE::RouteSource::InS1:
				numChannels = 16; // 1394 streams typically have 16 channels
				break;
			case DICE::RouteSource::ARM:
				numChannels = 2; // ARM typically has 2 channels
				break;
			case DICE::RouteSource::ARX0:
			case DICE::RouteSource::ARX1:
				numChannels = 16; // Audio receive typically has 16 channels
				break;
			default:
				numChannels = 1; // Default for unknown sources
			}

			// Add channels for this source
			for (int ch = 0; ch < numChannels; ch++)
			{
				sources.push_back(createSourcePoint(srcId, ch));
			}
		}

		// Cache the results
		sources_ = sources;
		pointsCached_ = true;

		return sources;
	}

	std::vector<DiceRouter::RoutePoint> DiceRouter::getDestinations() const
	{
		// If we have a cached list, return it
		if (pointsCached_ && !destinations_.empty())
		{
			return destinations_;
		}

		// Otherwise, build the list
		std::vector<RoutePoint> destinations;

		// Add all standard destinations
		for (int dstId = 0; dstId < static_cast<int>(DICE::RouteDestination::Invalid); dstId++)
		{
			DICE::RouteDestination dest = static_cast<DICE::RouteDestination>(dstId);

			// Skip muted destination
			if (dest == DICE::RouteDestination::Muted)
				continue;

			// Determine channel count for this destination
			int numChannels = 0;
			switch (dest)
			{
			case DICE::RouteDestination::AES:
				numChannels = 8; // AES typically has 8 channels
				break;
			case DICE::RouteDestination::ADAT:
				numChannels = 8; // ADAT has 8 channels
				break;
			case DICE::RouteDestination::Mixer0:
				numChannels = 16; // Mixer0 typically has 16 inputs
				break;
			case DICE::RouteDestination::Mixer1:
				numChannels = 2; // Mixer1 typically has 2 inputs
				break;
			case DICE::RouteDestination::InS0:
			case DICE::RouteDestination::InS1:
				numChannels = 16; // 1394 streams typically have 16 channels
				break;
			case DICE::RouteDestination::ARM:
				numChannels = 2; // ARM typically has 2 channels
				break;
			case DICE::RouteDestination::ATX0:
			case DICE::RouteDestination::ATX1:
				numChannels = 16; // Audio transmit typically has 16 channels
				break;
			default:
				numChannels = 1; // Default for unknown destinations
			}

			// Add channels for this destination
			for (int ch = 0; ch < numChannels; ch++)
			{
				destinations.push_back(createDestinationPoint(dstId, ch));
			}
		}

		// Cache the results
		destinations_ = destinations;
		pointsCached_ = true;

		return destinations;
	}

	std::expected<void, IOKitError> DiceRouter::connect(uint8_t sourceId, uint8_t sourceChannel,
																											uint8_t destId, uint8_t destChannel)
	{
		spdlog::debug("Connecting source {}:{} to destination {}:{}",
									static_cast<int>(sourceId), static_cast<int>(sourceChannel),
									static_cast<int>(destId), static_cast<int>(destChannel));

		// Get the active router config
		DiceRouterConfig *router = eap_.getActiveRouterConfig();
		if (!router)
		{
			spdlog::error("No active router configuration");
			return std::unexpected(IOKitError::NoDevice);
		}

		// Create combined IDs
		uint8_t src = sourceToId(sourceId, sourceChannel);
		uint8_t dst = destinationToId(destId, destChannel);

		// Create the route
		auto result = router->setupRoute(src, dst);
		if (!result)
		{
			spdlog::error("Failed to setup route: {}", static_cast<int>(result.error()));
			return std::unexpected(result.error());
		}

		return {};
	}

	std::expected<void, IOKitError> DiceRouter::connect(const RoutePoint &source, const RoutePoint &destination)
	{
		return connect(source.id, source.channel, destination.id, destination.channel);
	}

	std::expected<void, IOKitError> DiceRouter::disconnect(uint8_t destId, uint8_t destChannel)
	{
		spdlog::debug("Disconnecting destination {}:{}",
									static_cast<int>(destId), static_cast<int>(destChannel));

		// Get the active router config
		DiceRouterConfig *router = eap_.getActiveRouterConfig();
		if (!router)
		{
			spdlog::error("No active router configuration");
			return std::unexpected(IOKitError::NoDevice);
		}

		// Create combined ID
		uint8_t dst = destinationToId(destId, destChannel);

		// Remove the route
		auto result = router->removeRoute(dst);
		if (!result)
		{
			spdlog::error("Failed to remove route: {}", static_cast<int>(result.error()));
			return std::unexpected(result.error());
		}

		return {};
	}

	std::expected<void, IOKitError> DiceRouter::disconnect(const RoutePoint &destination)
	{
		return disconnect(destination.id, destination.channel);
	}

	std::expected<void, IOKitError> DiceRouter::mute(uint8_t destId, uint8_t destChannel)
	{
		spdlog::debug("Muting destination {}:{}",
									static_cast<int>(destId), static_cast<int>(destChannel));

		// Get the active router config
		DiceRouterConfig *router = eap_.getActiveRouterConfig();
		if (!router)
		{
			spdlog::error("No active router configuration");
			return std::unexpected(IOKitError::NoDevice);
		}

		// Create combined ID
		uint8_t dst = destinationToId(destId, destChannel);

		// Mute the route
		auto result = router->muteRoute(dst);
		if (!result)
		{
			spdlog::error("Failed to mute route: {}", static_cast<int>(result.error()));
			return std::unexpected(result.error());
		}

		return {};
	}

	std::expected<void, IOKitError> DiceRouter::mute(const RoutePoint &destination)
	{
		return mute(destination.id, destination.channel);
	}

	std::expected<void, IOKitError> DiceRouter::clearAllRoutes()
	{
		spdlog::debug("Clearing all routes");

		// Get the active router config
		DiceRouterConfig *router = eap_.getActiveRouterConfig();
		if (!router)
		{
			spdlog::error("No active router configuration");
			return std::unexpected(IOKitError::NoDevice);
		}

		// Clear all routes
		auto result = router->clearRoutes();
		if (!result)
		{
			spdlog::error("Failed to clear routes: {}", static_cast<int>(result.error()));
			return std::unexpected(result.error());
		}

		return {};
	}

	std::expected<void, IOKitError> DiceRouter::setupDefaultRoutes()
	{
		spdlog::debug("Setting up default routes");

		// Use the existing EAP implementation
		return eap_.setupDefaultRouterConfig();
	}

	std::expected<DiceRouter::RoutePoint, IOKitError> DiceRouter::getSource(uint8_t destId, uint8_t destChannel) const
	{
		spdlog::debug("Getting source for destination {}:{}",
									static_cast<int>(destId), static_cast<int>(destChannel));

		// Get the active router config
		DiceRouterConfig *router = eap_.getActiveRouterConfig();
		if (!router)
		{
			spdlog::error("No active router configuration");
			return std::unexpected(IOKitError::NoDevice);
		}

		// Create combined ID
		uint8_t dst = destinationToId(destId, destChannel);

		// Get the source
		auto sourceResult = router->getSourceForDestination(dst);
		if (!sourceResult)
		{
			spdlog::debug("No source found for destination {}:{}",
										static_cast<int>(destId), static_cast<int>(destChannel));
			return std::unexpected(sourceResult.error());
		}

		// Convert back to source ID and channel
		uint8_t src = sourceResult.value();
		auto [srcId, srcChannel] = idToSource(src);

		// Create and return the source point
		return createSourcePoint(srcId, srcChannel);
	}

	std::expected<DiceRouter::RoutePoint, IOKitError> DiceRouter::getSource(const RoutePoint &destination) const
	{
		return getSource(destination.id, destination.channel);
	}

	std::expected<std::vector<DiceRouter::RoutePoint>, IOKitError> DiceRouter::getDestinations(
			uint8_t sourceId, uint8_t sourceChannel) const
	{
		spdlog::debug("Getting destinations for source {}:{}",
									static_cast<int>(sourceId), static_cast<int>(sourceChannel));

		// Get the active router config
		DiceRouterConfig *router = eap_.getActiveRouterConfig();
		if (!router)
		{
			spdlog::error("No active router configuration");
			return std::unexpected(IOKitError::NoDevice);
		}

		// Create combined ID
		uint8_t src = sourceToId(sourceId, sourceChannel);

		// Get the destinations
		auto destsResult = router->getDestinationsForSource(src);
		if (!destsResult)
		{
			spdlog::debug("No destinations found for source {}:{}",
										static_cast<int>(sourceId), static_cast<int>(sourceChannel));
			return std::unexpected(destsResult.error());
		}

		// Convert each destination
		std::vector<RoutePoint> destinations;
		for (uint8_t dst : destsResult.value())
		{
			auto [dstId, dstChannel] = idToDestination(dst);
			destinations.push_back(createDestinationPoint(dstId, dstChannel));
		}

		return destinations;
	}

	std::expected<std::vector<DiceRouter::RoutePoint>, IOKitError> DiceRouter::getDestinations(const RoutePoint &source) const
	{
		return getDestinations(source.id, source.channel);
	}

	std::vector<std::vector<bool>> DiceRouter::getRoutingMatrix() const
	{
		spdlog::debug("Getting routing matrix");

		// Get all sources and destinations
		std::vector<RoutePoint> sources = getSources();
		std::vector<RoutePoint> destinations = getDestinations();

		// Create the matrix
		std::vector<std::vector<bool>> matrix(sources.size(), std::vector<bool>(destinations.size(), false));

		// Get all routes
		std::vector<Route> routes = getRoutes();

		// Fill the matrix
		for (const auto &route : routes)
		{
			// Find source index
			auto srcIt = std::find_if(sources.begin(), sources.end(),
																[&route](const RoutePoint &p)
																{
																	return p.id == route.source.id && p.channel == route.source.channel;
																});

			// Find destination index
			auto dstIt = std::find_if(destinations.begin(), destinations.end(),
																[&route](const RoutePoint &p)
																{
																	return p.id == route.destination.id && p.channel == route.destination.channel;
																});

			// If both found, set matrix element
			if (srcIt != sources.end() && dstIt != destinations.end())
			{
				size_t srcIndex = std::distance(sources.begin(), srcIt);
				size_t dstIndex = std::distance(destinations.begin(), dstIt);

				matrix[srcIndex][dstIndex] = true;
			}
		}

		return matrix;
	}

	DiceRouter::LabeledMatrix DiceRouter::getLabeledRoutingMatrix() const
	{
		spdlog::debug("Getting labeled routing matrix");

		// Create the result structure
		LabeledMatrix result;

		// Get all sources and destinations
		std::vector<RoutePoint> sources = getSources();
		std::vector<RoutePoint> destinations = getDestinations();

		// Extract row and column labels
		for (const auto &src : sources)
		{
			result.rowLabels.push_back(src.name + " [" + std::to_string(static_cast<int>(src.id)) +
																 ":" + std::to_string(static_cast<int>(src.channel)) + "]");
		}

		for (const auto &dst : destinations)
		{
			result.columnLabels.push_back(dst.name + " [" + std::to_string(static_cast<int>(dst.id)) +
																		":" + std::to_string(static_cast<int>(dst.channel)) + "]");
		}

		// Get the connection matrix
		result.connections = getRoutingMatrix();

		return result;
	}

	std::string DiceRouter::formatRoutingGrid() const
	{
		spdlog::debug("Formatting routing grid as text");

		LabeledMatrix matrix = getLabeledRoutingMatrix();
		std::stringstream ss;

		// Find the max width needed for row labels
		size_t maxRowLabelWidth = 0;
		for (const auto &label : matrix.rowLabels)
		{
			maxRowLabelWidth = std::max(maxRowLabelWidth, label.length());
		}

		// Add 2 for better readability
		maxRowLabelWidth += 2;

		// Calculate column widths (minimum 3 characters per column)
		std::vector<size_t> colWidths;
		for (const auto &label : matrix.columnLabels)
		{
			colWidths.push_back(std::max(label.length(), size_t(3)));
		}

		// Print header row with column labels
		ss << std::string(maxRowLabelWidth, ' ') << " | ";
		for (size_t c = 0; c < matrix.columnLabels.size(); ++c)
		{
			ss << std::setw(colWidths[c]) << std::left << matrix.columnLabels[c] << " | ";
		}
		ss << std::endl;

		// Print separator row
		ss << std::string(maxRowLabelWidth, '-') << "-+-";
		for (size_t c = 0; c < colWidths.size(); ++c)
		{
			ss << std::string(colWidths[c], '-') << "-+-";
		}
		ss << std::endl;

		// Print data rows
		for (size_t r = 0; r < matrix.rowLabels.size(); ++r)
		{
			ss << std::setw(maxRowLabelWidth) << std::left << matrix.rowLabels[r] << " | ";

			for (size_t c = 0; c < matrix.columnLabels.size(); ++c)
			{
				std::string cell = matrix.connections[r][c] ? " X " : "   ";
				ss << std::setw(colWidths[c]) << std::left << cell << " | ";
			}
			ss << std::endl;
		}

		return ss.str();
	}

	std::expected<DiceRouter::RoutePoint, IOKitError> DiceRouter::getMidiInput(unsigned int port) const
	{
		spdlog::debug("Getting MIDI input for port {}", port);

		// MIDI inputs are typically routed to specific destinations based on port
		if (port >= 4)
		{
			spdlog::error("MIDI port {} out of range (max 4)", port);
			return std::unexpected(IOKitError::BadArgument);
		}

		// Create a destination point for the MIDI input
		// In DICE, MIDI inputs are typically mapped to specific mixer inputs
		// This is a simplification - actual implementation depends on device
		return createDestinationPoint(static_cast<uint8_t>(DICE::RouteDestination::Mixer1), port);
	}

	std::expected<DiceRouter::RoutePoint, IOKitError> DiceRouter::getMidiOutput(unsigned int port) const
	{
		spdlog::debug("Getting MIDI output for port {}", port);

		// MIDI outputs are typically routed from specific sources based on port
		if (port >= 4)
		{
			spdlog::error("MIDI port {} out of range (max 4)", port);
			return std::unexpected(IOKitError::BadArgument);
		}

		// Create a source point for the MIDI output
		// In DICE, MIDI outputs are typically mapped to specific mixer outputs
		// This is a simplification - actual implementation depends on device
		return createSourcePoint(static_cast<uint8_t>(DICE::RouteSource::Mixer), port + 16);
	}

	std::expected<void, IOKitError> DiceRouter::setMidiInput(unsigned int port, const RoutePoint &src)
	{
		spdlog::debug("Setting MIDI input for port {}", port);

		// Get the destination for this MIDI port
		auto destResult = getMidiInput(port);
		if (!destResult)
		{
			return std::unexpected(destResult.error());
		}

		// Connect the source to the MIDI input destination
		return connect(src, destResult.value());
	}

	std::expected<void, IOKitError> DiceRouter::setMidiOutput(unsigned int port, const RoutePoint &dest)
	{
		spdlog::debug("Setting MIDI output for port {}", port);

		// Get the source for this MIDI port
		auto srcResult = getMidiOutput(port);
		if (!srcResult)
		{
			return std::unexpected(srcResult.error());
		}

		// Connect the MIDI output source to the destination
		return connect(srcResult.value(), dest);
	}

	bool DiceRouter::isMidiPort(const RoutePoint &point) const
	{
		// MIDI ports in DICE devices are typically on these sources/destinations

		// Check if it's a MIDI input (destination)
		if (point.id == static_cast<uint8_t>(DICE::RouteDestination::Mixer1) && point.channel < 4)
		{
			return true;
		}

		// Check if it's a MIDI output (source)
		if (point.id == static_cast<uint8_t>(DICE::RouteSource::Mixer) && point.channel >= 16 && point.channel < 20)
		{
			return true;
		}

		// Check for MIDI over FireWire
		if ((point.id == static_cast<uint8_t>(DICE::RouteDestination::InS0) ||
				 point.id == static_cast<uint8_t>(DICE::RouteDestination::InS1)) &&
				point.channel >= 8 && point.channel < 12)
		{
			return true;
		}

		if ((point.id == static_cast<uint8_t>(DICE::RouteSource::InS0) ||
				 point.id == static_cast<uint8_t>(DICE::RouteSource::InS1)) &&
				point.channel >= 8 && point.channel < 12)
		{
			return true;
		}

		return false;
	}

	std::vector<DiceRouter::RoutePoint> DiceRouter::getAllMidiInputs() const
	{
		spdlog::debug("Getting all MIDI inputs");

		std::vector<RoutePoint> midiInputs;

		// Add standard MIDI inputs (typically 4 ports)
		for (unsigned int i = 0; i < 4; i++)
		{
			auto inputResult = getMidiInput(i);
			if (inputResult)
			{
				// Add a MIDI-specific label
				RoutePoint point = inputResult.value();
				point.name = "MIDI Input " + std::to_string(i + 1) + " (" + point.name + ")";
				midiInputs.push_back(point);
			}
		}

		// Add FireWire MIDI ports
		for (int ch = 8; ch < 12; ch++)
		{
			RoutePoint point = createDestinationPoint(
					static_cast<uint8_t>(DICE::RouteDestination::InS0), ch);
			point.name = "FW MIDI Input " + std::to_string(ch - 7) + " (" + point.name + ")";
			midiInputs.push_back(point);
		}

		return midiInputs;
	}

	std::vector<DiceRouter::RoutePoint> DiceRouter::getAllMidiOutputs() const
	{
		spdlog::debug("Getting all MIDI outputs");

		std::vector<RoutePoint> midiOutputs;

		// Add standard MIDI outputs (typically 4 ports)
		for (unsigned int i = 0; i < 4; i++)
		{
			auto outputResult = getMidiOutput(i);
			if (outputResult)
			{
				// Add a MIDI-specific label
				RoutePoint point = outputResult.value();
				point.name = "MIDI Output " + std::to_string(i + 1) + " (" + point.name + ")";
				midiOutputs.push_back(point);
			}
		}

		// Add FireWire MIDI ports
		for (int ch = 8; ch < 12; ch++)
		{
			RoutePoint point = createSourcePoint(
					static_cast<uint8_t>(DICE::RouteSource::InS0), ch);
			point.name = "FW MIDI Output " + std::to_string(ch - 7) + " (" + point.name + ")";
			midiOutputs.push_back(point);
		}

		return midiOutputs;
	}

	std::expected<void, IOKitError> DiceRouter::setupDefaultMidiRoutes()
	{
		spdlog::debug("Setting up default MIDI routes");

		// Get all MIDI inputs and outputs
		auto midiInputs = getAllMidiInputs();
		auto midiOutputs = getAllMidiOutputs();

		// Set up standard routes - example: connect physical MIDI to FireWire MIDI
		for (size_t i = 0; i < std::min(midiInputs.size(), midiOutputs.size()) && i < 4; i++)
		{
			// Connect physical MIDI input to FireWire MIDI output
			auto result = connect(midiInputs[i], midiOutputs[i]);
			if (!result)
			{
				spdlog::error("Failed to connect MIDI input {} to output {}: {}",
											i, i, static_cast<int>(result.error()));
				return std::unexpected(result.error());
			}

			// Connect FireWire MIDI input to physical MIDI output
			if (i + 4 < midiInputs.size() && i + 4 < midiOutputs.size())
			{
				result = connect(midiInputs[i + 4], midiOutputs[i + 4]);
				if (!result)
				{
					spdlog::error("Failed to connect FW MIDI input {} to output {}: {}",
												i, i, static_cast<int>(result.error()));
					return std::unexpected(result.error());
				}
			}
		}

		return {};
	}

	uint8_t DiceRouter::sourceToId(uint8_t srcId, uint8_t channel) const
	{
		// In DICE, the combined ID is (srcId << 4) | channel
		return ((srcId & 0x0F) << 4) | (channel & 0x0F);
	}

	uint8_t DiceRouter::destinationToId(uint8_t destId, uint8_t channel) const
	{
		// In DICE, the combined ID is (destId << 4) | channel
		return ((destId & 0x0F) << 4) | (channel & 0x0F);
	}

	std::pair<uint8_t, uint8_t> DiceRouter::idToSource(uint8_t id) const
	{
		// Extract source ID and channel
		uint8_t srcId = (id >> 4) & 0x0F;
		uint8_t channel = id & 0x0F;

		return {srcId, channel};
	}

	std::pair<uint8_t, uint8_t> DiceRouter::idToDestination(uint8_t id) const
	{
		// Extract destination ID and channel
		uint8_t destId = (id >> 4) & 0x0F;
		uint8_t channel = id & 0x0F;

		return {destId, channel};
	}

	DiceRouter::RoutePoint DiceRouter::createSourcePoint(uint8_t srcId, uint8_t channel) const
	{
		std::string name;

		// Get a human-readable name for this source
		if (srcId < static_cast<uint8_t>(DICE::RouteSource::Invalid))
		{
			DICE::RouteSource source = static_cast<DICE::RouteSource>(srcId);
			name = eap_.getSourceName(source, channel);
		}
		else
		{
			name = "Unknown Source";
		}

		return RoutePoint(srcId, channel, name);
	}

	DiceRouter::RoutePoint DiceRouter::createDestinationPoint(uint8_t destId, uint8_t channel) const
	{
		std::string name;

		// Get a human-readable name for this destination
		if (destId < static_cast<uint8_t>(DICE::RouteDestination::Invalid))
		{
			DICE::RouteDestination dest = static_cast<DICE::RouteDestination>(destId);
			name = eap_.getDestinationName(dest, channel);
		}
		else
		{
			name = "Unknown Destination";
		}

		return RoutePoint(destId, channel, name);
	}

	int DiceRouter::getTransmitChannelCount() const
	{
		spdlog::debug("Getting transmit channel count");

		// Get the stream configuration
		DiceStreamConfig *streamCfg = eap_.getActiveStreamConfig();
		if (!streamCfg)
		{
			spdlog::error("Could not get active stream configuration");
			return 0;
		}

		// Access TX names for first stream to determine count
		auto txNamesResult = streamCfg->getTxNames(0);
		if (!txNamesResult)
		{
			spdlog::error("Could not get TX names");
			return 0;
		}

		// Return the number of TX channels
		return static_cast<int>(txNamesResult.value().size());
	}

	int DiceRouter::getReceiveChannelCount() const
	{
		spdlog::debug("Getting receive channel count");

		// Get the stream configuration
		DiceStreamConfig *streamCfg = eap_.getActiveStreamConfig();
		if (!streamCfg)
		{
			spdlog::error("Could not get active stream configuration");
			return 0;
		}

		// Access RX names for first stream to determine count
		auto rxNamesResult = streamCfg->getRxNames(0);
		if (!rxNamesResult)
		{
			spdlog::error("Could not get RX names");
			return 0;
		}

		// Return the number of RX channels
		return static_cast<int>(rxNamesResult.value().size());
	}

	std::expected<void, IOKitError> DiceRouter::setTransmitChannelCount(int count)
	{
		spdlog::debug("Setting transmit channel count to {}", count);

		// Validate the channel count
		if (count < 0 || count > 32)
		{
			spdlog::error("Invalid transmit channel count: {}", count);
			return std::unexpected(IOKitError::BadArgument);
		}

		// DICE devices don't directly support setting channel counts
		// Instead, we need to select an appropriate configuration

		// Get the current configuration
		int currentConfig = eap_.getCurrentConfig();

		// Update the configuration based on the requested channel count
		// This is a simplified implementation - real implementation would
		// need to store the new count in the stream configuration
		spdlog::warn("Setting transmit channel count directly is not supported. Use applyChannelFormat instead.");
		return std::unexpected(IOKitError::Unsupported);
	}

	std::expected<void, IOKitError> DiceRouter::setReceiveChannelCount(int count)
	{
		spdlog::debug("Setting receive channel count to {}", count);

		// Validate the channel count
		if (count < 0 || count > 32)
		{
			spdlog::error("Invalid receive channel count: {}", count);
			return std::unexpected(IOKitError::BadArgument);
		}

		// DICE devices don't directly support setting channel counts
		// Instead, we need to select an appropriate configuration

		// Get the current configuration
		int currentConfig = eap_.getCurrentConfig();

		// Update the configuration based on the requested channel count
		// This is a simplified implementation - real implementation would
		// need to store the new count in the stream configuration
		spdlog::warn("Setting receive channel count directly is not supported. Use applyChannelFormat instead.");
		return std::unexpected(IOKitError::Unsupported);
	}

	std::vector<std::pair<int, int>> DiceRouter::getAvailableChannelFormats() const
	{
		spdlog::debug("Getting available channel formats");

		std::vector<std::pair<int, int>> formats;

		// Get the current config from EAP
		int currentConfig = eap_.getCurrentConfig();

		// Available formats depend on the current configuration
		// These are typical configurations for DICE devices
		if (currentConfig == 1) // Low config (32-48kHz)
		{
			// Low-rate configurations
			formats.push_back({24, 24}); // 24 in, 24 out
			formats.push_back({16, 16}); // 16 in, 16 out
			formats.push_back({8, 8});	 // 8 in, 8 out
		}
		else if (currentConfig == 2) // Mid config (88.2-96kHz)
		{
			// Mid-rate configurations
			formats.push_back({16, 16}); // 16 in, 16 out
			formats.push_back({8, 8});	 // 8 in, 8 out
			formats.push_back({4, 4});	 // 4 in, 4 out
		}
		else if (currentConfig == 3) // High config (176.4-192kHz)
		{
			// High-rate configurations
			formats.push_back({8, 8}); // 8 in, 8 out
			formats.push_back({4, 4}); // 4 in, 4 out
			formats.push_back({2, 2}); // 2 in, 2 out
		}
		else
		{
			// Unknown config, return reasonable defaults
			formats.push_back({16, 16}); // 16 in, 16 out
			formats.push_back({8, 8});	 // 8 in, 8 out
		}

		return formats;
	}

	std::expected<void, IOKitError> DiceRouter::applyChannelFormat(int txChannels, int rxChannels)
	{
		spdlog::debug("Applying channel format: {} TX, {} RX", txChannels, rxChannels);

		// Check if the format is valid
		auto availableFormats = getAvailableChannelFormats();
		bool validFormat = false;

		for (const auto &format : availableFormats)
		{
			if (format.first == txChannels && format.second == rxChannels)
			{
				validFormat = true;
				break;
			}
		}

		if (!validFormat)
		{
			spdlog::error("Invalid channel format: {} TX, {} RX", txChannels, rxChannels);
			return std::unexpected(IOKitError::BadArgument);
		}

		// Apply the transmit channel count
		auto txResult = setTransmitChannelCount(txChannels);
		if (!txResult)
		{
			spdlog::error("Failed to set transmit channel count: {}", static_cast<int>(txResult.error()));
			return std::unexpected(txResult.error());
		}

		// Apply the receive channel count
		auto rxResult = setReceiveChannelCount(rxChannels);
		if (!rxResult)
		{
			spdlog::error("Failed to set receive channel count: {}", static_cast<int>(rxResult.error()));
			return std::unexpected(rxResult.error());
		}

		return {};
	}

} // namespace FWA
