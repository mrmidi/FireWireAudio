// include/FWA/Error.h
// Synopsis: Wrapper for IOReturn error codes.

#pragma once

#include <system_error>
#include <IOKit/IOReturn.h>

namespace FWA {

struct IOKitError : public std::error_code {
    IOKitError(IOReturn ret) :
    std::error_code(ret, iokit_category()) {}
    
    IOReturn iokit_return() const { return static_cast<IOReturn>(value()); }
    
private:
    static const std::error_category& iokit_category() {
        static struct IOKitErrorCategory : public std::error_category {
            const char* name() const noexcept override {
                return "IOKit";
            }
            
            std::string message(int ev) const override {
                //  Provide a more descriptive message if possible, or at least
                //  return the IOReturn value as a hex string.
                return "IOKit error: 0x" + std::to_string(static_cast<unsigned int>(ev));
            }
            
        } category;
        return category;
    }
};

} // namespace FWA
