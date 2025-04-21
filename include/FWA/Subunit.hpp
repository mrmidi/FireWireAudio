// include/FWA/Subunit.hpp
#pragma once

#include "FWA/Enums.hpp"        // Include Enums (needed for SubunitType)
#include <cstdint>
#include <string>

namespace FWA {

/**
 * @brief Abstract base class for AV/C subunits (Music, Audio, etc.).
 *
 * Provides a common interface and basic properties for different subunit types.
 */
class Subunit {
public: // Make interface methods public
    /**
     * @brief Virtual destructor to ensure proper cleanup in derived classes.
     */
    virtual ~Subunit() = default;

    /**
     * @brief Get the instance ID of this subunit.
     * @return uint8_t The subunit instance ID (0-7).
     */
    uint8_t getId() const { return id_; }

    /**
     * @brief Get the specific type of this subunit.
     * @return SubunitType The enum value representing the subunit type.
     */
    virtual SubunitType getSubunitType() const = 0; // Pure virtual

    /**
     * @brief Get a human-readable name for the subunit type.
     * @return std::string The name of the subunit type (e.g., "Music").
     */
    virtual std::string getSubunitTypeName() const = 0; // Pure virtual

    void setId(uint8_t id) { id_ = id; }

protected:
    /**
     * @brief Protected constructor for base class.
     * @param id The subunit instance ID (0-7).
     */
    explicit Subunit(uint8_t id) : id_(id) {} // Use explicit, remove default value if always set

    // Add common protected members if needed later.

private:
    uint8_t id_;

    // Prevent copying and moving of subunits by default
    Subunit(const Subunit&) = delete;
    Subunit& operator=(const Subunit&) = delete;
    Subunit(Subunit&&) = delete;
    Subunit& operator=(Subunit&&) = delete;
};

// NO DEFINITIONS FOR MusicSubunit or AudioSubunit HERE!

} // namespace FWA