// include/FWA/DiceRouter.hpp
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
	class DiceEAP;
	class DiceRouterConfig;

	/**
	 * @brief High-level router interface for DICE devices
	 *
	 * This class provides a user-friendly interface for configuring routing on DICE devices.
	 * It abstracts the low-level details and provides a matrix-style visualization of routes.
	 */
	class DiceRouter
	{
	public:
		/**
		 * @brief Source/destination description for a routing point
		 */
		struct RoutePoint
		{
			uint8_t id;				///< Source/destination ID
			uint8_t channel;	///< Channel number
			std::string name; ///< Human-readable name

			RoutePoint() : id(0), channel(0), name("Unknown") {}
			RoutePoint(uint8_t _id, uint8_t _ch, const std::string &_name)
					: id(_id), channel(_ch), name(_name) {}
		};

		/**
		 * @brief Route connection between source and destination
		 */
		struct Route
		{
			RoutePoint source;			///< Source point
			RoutePoint destination; ///< Destination point

			Route() = default;
			Route(const RoutePoint &src, const RoutePoint &dst)
					: source(src), destination(dst) {}
		};

	public:
		/**
		 * @brief Construct a new DiceRouter
		 * @param eap Reference to EAP interface
		 */
		DiceRouter(DiceEAP &eap);

		/**
		 * @brief Destroy the DiceRouter
		 */
		~DiceRouter();

		/**
		 * @brief Update the router state from the device
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> update();

		/**
		 * @brief Get all current routes
		 * @return Vector of routes
		 */
		std::vector<Route> getRoutes() const;

		/**
		 * @brief Get all available source points
		 * @return Vector of source points
		 */
		std::vector<RoutePoint> getSources() const;

		/**
		 * @brief Get all available destination points
		 * @return Vector of destination points
		 */
		std::vector<RoutePoint> getDestinations() const;

		/**
		 * @brief Connect a source to a destination
		 * @param sourceId Source ID
		 * @param sourceChannel Source channel
		 * @param destId Destination ID
		 * @param destChannel Destination channel
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> connect(uint8_t sourceId, uint8_t sourceChannel,
																						uint8_t destId, uint8_t destChannel);

		/**
		 * @brief Connect a source to a destination using RoutePoints
		 * @param source Source point
		 * @param destination Destination point
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> connect(const RoutePoint &source, const RoutePoint &destination);

		/**
		 * @brief Disconnect a destination
		 * @param destId Destination ID
		 * @param destChannel Destination channel
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> disconnect(uint8_t destId, uint8_t destChannel);

		/**
		 * @brief Disconnect a destination using a RoutePoint
		 * @param destination Destination point
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> disconnect(const RoutePoint &destination);

		/**
		 * @brief Mute a destination (set source to Muted)
		 * @param destId Destination ID
		 * @param destChannel Destination channel
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> mute(uint8_t destId, uint8_t destChannel);

		/**
		 * @brief Mute a destination using a RoutePoint
		 * @param destination Destination point
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> mute(const RoutePoint &destination);

		/**
		 * @brief Clear all routes
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> clearAllRoutes();

		/**
		 * @brief Set up default routes based on device type
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> setupDefaultRoutes();

		/**
		 * @brief Get the source for a destination
		 * @param destId Destination ID
		 * @param destChannel Destination channel
		 * @return Source point or error
		 */
		std::expected<RoutePoint, IOKitError> getSource(uint8_t destId, uint8_t destChannel) const;

		/**
		 * @brief Get the source for a destination using a RoutePoint
		 * @param destination Destination point
		 * @return Source point or error
		 */
		std::expected<RoutePoint, IOKitError> getSource(const RoutePoint &destination) const;

		/**
		 * @brief Get all destinations for a source
		 * @param sourceId Source ID
		 * @param sourceChannel Source channel
		 * @return Vector of destination points or error
		 */
		std::expected<std::vector<RoutePoint>, IOKitError> getDestinations(uint8_t sourceId, uint8_t sourceChannel) const;

		/**
		 * @brief Get all destinations for a source using a RoutePoint
		 * @param source Source point
		 * @return Vector of destination points or error
		 */
		std::expected<std::vector<RoutePoint>, IOKitError> getDestinations(const RoutePoint &source) const;

		/**
		 * @brief Get a full routing matrix
		 *
		 * Returns a matrix where each element indicates if a connection exists
		 * between a source (row) and destination (column).
		 *
		 * @return 2D vector (sources × destinations) of booleans
		 */
		std::vector<std::vector<bool>> getRoutingMatrix() const;

		/**
		 * @brief Get a labeled routing matrix
		 *
		 * Returns a matrix with port names and connection status, suitable for UI display.
		 * The matrix includes row headers (source names) and column headers (destination names).
		 *
		 * @return Matrix data structure with names and connection status
		 */
		struct LabeledMatrix
		{
			std::vector<std::string> rowLabels;					///< Source names
			std::vector<std::string> columnLabels;			///< Destination names
			std::vector<std::vector<bool>> connections; ///< Connection status
		};
		LabeledMatrix getLabeledRoutingMatrix() const;

		/**
		 * @brief Format routing matrix as a text grid
		 *
		 * @return String containing a formatted text representation of the routing matrix
		 */
		std::string formatRoutingGrid() const;

		/**
		 * @brief Get a route for the MIDI input
		 * @param port MIDI port number
		 * @return Source point or error
		 */
		std::expected<RoutePoint, IOKitError> getMidiInput(unsigned int port) const;

		/**
		 * @brief Get a route for the MIDI output
		 * @param port MIDI port number
		 * @return Destination point or error
		 */
		std::expected<RoutePoint, IOKitError> getMidiOutput(unsigned int port) const;

		/**
		 * @brief Set up a route for MIDI input
		 * @param port MIDI port number
		 * @param dest Destination point
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> setMidiInput(unsigned int port, const RoutePoint &dest);

		/**
		 * @brief Set up a route for MIDI output
		 * @param port MIDI port number
		 * @param src Source point
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> setMidiOutput(unsigned int port, const RoutePoint &src);

		/**
		 * @brief Check if a route point is a MIDI port
		 * @param point Route point to check
		 * @return True if this is a MIDI port, false otherwise
		 */
		bool isMidiPort(const RoutePoint &point) const;

		/**
		 * @brief Get all MIDI inputs
		 * @return Vector of MIDI input route points
		 */
		std::vector<RoutePoint> getAllMidiInputs() const;

		/**
		 * @brief Get all MIDI outputs
		 * @return Vector of MIDI output route points
		 */
		std::vector<RoutePoint> getAllMidiOutputs() const;

		/**
		 * @brief Connect all available MIDI ports in a default configuration
		 *
		 * This method sets up common MIDI routing patterns based on the device type.
		 *
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> setupDefaultMidiRoutes();

		/**
		 * @brief Get the number of transmit channels available
		 * @return Number of transmit channels
		 */
		int getTransmitChannelCount() const;

		/**
		 * @brief Get the number of receive channels available
		 * @return Number of receive channels
		 */
		int getReceiveChannelCount() const;

		/**
		 * @brief Set the number of transmit channels to use
		 * @param count Number of channels
		 * @return Success or error status
		 *
		 * @note This may affect the device's streaming capabilities
		 */
		std::expected<void, IOKitError> setTransmitChannelCount(int count);

		/**
		 * @brief Set the number of receive channels to use
		 * @param count Number of channels
		 * @return Success or error status
		 *
		 * @note This may affect the device's streaming capabilities
		 */
		std::expected<void, IOKitError> setReceiveChannelCount(int count);

		/**
		 * @brief Get available channel format options for the current sample rate
		 * @return Vector of possible channel count configurations
		 */
		std::vector<std::pair<int, int>> getAvailableChannelFormats() const;

		/**
		 * @brief Apply a predefined channel format
		 * @param txChannels Number of transmit channels
		 * @param rxChannels Number of receive channels
		 * @return Success or error status
		 */
		std::expected<void, IOKitError> applyChannelFormat(int txChannels, int rxChannels);

	private:
		/**
		 * @brief Convert a source ID and channel to a combined ID
		 * @param srcId Source ID
		 * @param channel Channel number
		 * @return Combined ID
		 */
		uint8_t sourceToId(uint8_t srcId, uint8_t channel) const;

		/**
		 * @brief Convert a destination ID and channel to a combined ID
		 * @param destId Destination ID
		 * @param channel Channel number
		 * @return Combined ID
		 */
		uint8_t destinationToId(uint8_t destId, uint8_t channel) const;

		/**
		 * @brief Extract source ID and channel from a combined ID
		 * @param id Combined ID
		 * @return Pair of source ID and channel
		 */
		std::pair<uint8_t, uint8_t> idToSource(uint8_t id) const;

		/**
		 * @brief Extract destination ID and channel from a combined ID
		 * @param id Combined ID
		 * @return Pair of destination ID and channel
		 */
		std::pair<uint8_t, uint8_t> idToDestination(uint8_t id) const;

		/**
		 * @brief Create a source point from ID and channel
		 * @param srcId Source ID
		 * @param channel Channel number
		 * @return Source point
		 */
		RoutePoint createSourcePoint(uint8_t srcId, uint8_t channel) const;

		/**
		 * @brief Create a destination point from ID and channel
		 * @param destId Destination ID
		 * @param channel Channel number
		 * @return Destination point
		 */
		RoutePoint createDestinationPoint(uint8_t destId, uint8_t channel) const;

	private:
		DiceEAP &eap_;
		mutable std::vector<RoutePoint> sources_;
		mutable std::vector<RoutePoint> destinations_;
		mutable bool pointsCached_;
	};

} // namespace FWA
