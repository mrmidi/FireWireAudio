// include/FWA/Helpers.h
#ifndef FWA_HELPERS_H
#define FWA_HELPERS_H

#include <CoreFoundation/CoreFoundation.h>
#include <string>
#include <vector>
#include <cstdint>
#include "FWA/Enums.hpp"

namespace FWA {

class Helpers {
public:
    static void printCFDictionary(CFDictionaryRef dict, int indent = 0);
    static std::string cfStringToString(CFStringRef cfString);
    static std::string formatHexBytes(const std::vector<uint8_t>& bytes);
    /**
     * @brief Calculates the AV/C subunit address byte from type and ID.
     * @param type The SubunitType enum value.
     * @param id The subunit instance ID (0-7).
     * @return uint8_t The calculated subunit address byte.
     */
    static uint8_t getSubunitAddress(SubunitType type, uint8_t id);
};

} // namespace FWA

#endif // FWA_HELPERS_H