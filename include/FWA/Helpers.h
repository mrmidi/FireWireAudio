// include/FWA/Helpers.h
#ifndef FWA_HELPERS_H
#define FWA_HELPERS_H

#include <CoreFoundation/CoreFoundation.h> // For CFDictionaryRef, etc.
#include <string> // to construct string for output

namespace FWA {

class Helpers {
public:
    static void printCFDictionary(CFDictionaryRef dict, int indent = 0);
    static std::string cfStringToString(CFStringRef cfString); // Helper function to convert CFString to std::string
    static std::string formatHexBytes(const std::vector<uint8_t>& bytes); // Helper function to format a vector of bytes as hex
};

} // namespace FWA

#endif // FWA_HELPERS_H