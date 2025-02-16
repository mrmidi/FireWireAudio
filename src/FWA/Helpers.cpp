// src/Helpers.cpp
#include "FWA/Helpers.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <memory>

namespace FWA {

    // Helper function: Convert CFString to std::string
    std::string Helpers::cfStringToString(CFStringRef cfString) {
        if (cfString == nullptr) {
            return "";  // Handle null strings gracefully
        }
        std::unique_ptr<char[]> buffer(new char[CFStringGetLength(cfString) * 4 + 1]); // +1 for null terminator, *4 for utf-8
        if (CFStringGetCString(cfString, buffer.get(), CFStringGetLength(cfString) * 4 + 1, kCFStringEncodingUTF8)) {
            return std::string(buffer.get());
        } else {
            return ""; // Handle error. CFStringGetCString failed, return empty or error message.
        }
    }

    void Helpers::printCFDictionary(CFDictionaryRef dict, int indent) {
        if (!dict) {
            std::cout << "(null CFDictionary)" << std::endl;
            return;
        }

        CFIndex count = CFDictionaryGetCount(dict);
        std::cout << "CFDictionary with " << std::dec << count << " entries:" << std::endl;

        if (count == 0) {
            return; // Nothing more to do for an empty dictionary.
        }

        CFTypeRef* keys = new CFTypeRef[count];
        CFTypeRef* values = new CFTypeRef[count];
        CFDictionaryGetKeysAndValues(dict, keys, values);

        std::string indentStr(indent, ' ');

        for (CFIndex i = 0; i < count; ++i) {
            CFStringRef key = (CFStringRef)keys[i];
            CFTypeRef value = values[i];
            std::string keyStr = cfStringToString(key);
            CFTypeID typeID = CFGetTypeID(value);

            std::cout << indentStr;

            if (typeID == CFStringGetTypeID()) {
                std::string valueStr = cfStringToString((CFStringRef)value);
                std::cout << keyStr << ": " << valueStr << " (CFString)" << std::endl;
            } else if (typeID == CFNumberGetTypeID()) {
                CFNumberRef num = (CFNumberRef)value;
                if (CFNumberIsFloatType(num)) {
                    double doubleVal;
                    if (CFNumberGetValue(num, kCFNumberDoubleType, &doubleVal)) {
                        std::cout << keyStr << ": " << doubleVal << " (CFNumber - Double)" << std::endl;
                    } else {
                        std::cout << keyStr << ": (CFNumber - Could not get double value)" << std::endl;
                    }
                } else {
                    long long longVal;
                    if (CFNumberGetValue(num, kCFNumberSInt64Type, &longVal)) {
                        std::cout << keyStr << ": " << longVal << " (0x" << std::hex << longVal << ") (CFNumber - Integer)" << std::endl;
                    } else {
                        std::cout << keyStr << ": (CFNumber - Could not get integer value)" << std::endl;
                    }
                }
            } else if (typeID == CFDataGetTypeID()) {
                CFDataRef data = (CFDataRef)value;
                const UInt8* bytes = CFDataGetBytePtr(data);
                CFIndex dataLength = CFDataGetLength(data);
                std::ostringstream hexStream;
                hexStream << std::hex << std::setfill('0');
                for (CFIndex j = 0; j < dataLength; ++j) {
                    hexStream << std::setw(2) << static_cast<int>(bytes[j]) << " ";
                }
                std::cout << keyStr << ": " << hexStream.str() << "(CFData)" << std::endl;
            } else if (typeID == CFDictionaryGetTypeID()) {
                std::cout << keyStr << ": (nested CFDictionary)" << std::endl;
                printCFDictionary((CFDictionaryRef)value, indent + 4); // Recursive call for nested dictionary
            } else if (typeID == CFArrayGetTypeID()) {
                std::cout << keyStr << ": (CFArray)" << std::endl;
                CFArrayRef array = (CFArrayRef)value;
                CFIndex arrayCount = CFArrayGetCount(array);
                for (CFIndex j = 0; j < arrayCount; j++) {
                    CFTypeRef arrayValue = CFArrayGetValueAtIndex(array, j);
                    std::cout << indentStr << "  [" << j << "]: ";
                    if (CFGetTypeID(arrayValue) == CFDictionaryGetTypeID()) {
                        printCFDictionary((CFDictionaryRef)arrayValue, indent + 4); // Increase indent for nested array elements
                    }
                }
            } else {
                std::cout << keyStr << ": (Unknown CFType - ID " << typeID << ")" << std::endl;
            }
        }

        delete[] keys;
        delete[] values;
    }

    std::string Helpers::formatHexBytes(const std::vector<uint8_t>& bytes) {
        std::ostringstream oss;
        oss << std::hex << std::uppercase << std::setfill('0');
        for (const auto& byte : bytes) {
            oss << "0x" << std::setw(2) << static_cast<int>(byte) << " ";
        }
        return oss.str();
    }

} // namespace FWA