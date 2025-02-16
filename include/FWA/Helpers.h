// include/FWA/Helpers.h
#ifndef FWA_HELPERS_H
#define FWA_HELPERS_H

#include <CoreFoundation/CoreFoundation.h> // For CFDictionaryRef, etc.
#include <string> // to construct string for output
#include <vector> // to use vector for formatHexBytes

namespace FWA {

/**
 * @brief Utility class providing helper functions for common operations
 */
class Helpers {
public:
    /**
     * @brief Print the contents of a CoreFoundation dictionary
     * @param dict Dictionary to print
     * @param indent Indentation level (default 0)
     */
    static void printCFDictionary(CFDictionaryRef dict, int indent = 0);

    /**
     * @brief Convert a CoreFoundation string to std::string
     * @param cfString CoreFoundation string to convert
     * @return std::string Converted string
     */
    static std::string cfStringToString(CFStringRef cfString);

    /**
     * @brief Format a byte array as hexadecimal string
     * @param bytes Vector of bytes to format
     * @return std::string Formatted hexadecimal string
     */
    static std::string formatHexBytes(const std::vector<uint8_t>& bytes);
};

} // namespace FWA

#endif // FWA_HELPERS_H