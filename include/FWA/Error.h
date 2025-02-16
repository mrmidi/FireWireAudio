// include/FWA/Error.h
// Synopsis: Wrapper for IOReturn error codes.

#pragma once

#include <system_error>
#include <IOKit/IOReturn.h>
#include <string>
#include <format>

namespace FWA {

enum class IOKitError {
    Success = 0,                    // Operation completed successfully
    Error = 0x2bc,                 // General error
    NoMemory = 0x2bd,             // Memory allocation failed
    NoResources = 0x2be,          // Resource shortage
    IPCError = 0x2bf,             // Error during IPC
    NoDevice = 0x2c0,             // No such device
    NotPrivileged = 0x2c1,        // Privilege violation
    BadArgument = 0x2c2,          // Invalid argument
    LockedRead = 0x2c3,           // Device read locked
    LockedWrite = 0x2c4,          // Device write locked
    ExclusiveAccess = 0x2c5,      // Exclusive access and device already open
    BadMessageID = 0x2c6,         // Sent/received messages had different msg_id
    Unsupported = 0x2c7,          // Unsupported function
    VMError = 0x2c8,              // Virtual memory error
    InternalError = 0x2c9,        // Internal error
    IOError = 0x2ca,              // I/O error
    CannotLock = 0x2cb,           // Cannot acquire lock
    NotOpen = 0x2cc,              // Device not open
    NotReadable = 0x2cd,          // Read not supported
    NotWritable = 0x2ce,          // Write not supported
    NotAligned = 0x2cf,           // Alignment error
    BadMedia = 0x2d0,             // Media error
    StillOpen = 0x2d1,            // Device(s) still open
    DMAError = 0x2d2,             // DMA error
    Busy = 0x2d3,                 // Device busy
    Timeout = 0x2d4,              // Operation timed out
    Offline = 0x2d5,              // Device offline
    NotReady = 0x2d6,             // Device not ready
    NotAttached = 0x2d7,          // Device not attached
    NoChannels = 0x2d8,           // No DMA channels available
    NoSpace = 0x2d9,              // No space available
    PortExists = 0x2da,           // Port already exists
    CannotWire = 0x2db,           // Cannot wire memory
    NoInterrupt = 0x2dc,          // No interrupt attached
    NoFrames = 0x2dd,             // No DMA frames available
    MessageTooLarge = 0x2de,      // Message too large
    NotPermitted = 0x2df,         // Operation not permitted
    NoPower = 0x2e3,              // No power to device
    NoMedia = 0x2e4,              // Media not present
    UnformattedMedia = 0x2e5,     // Media not formatted
    UnsupportedMode = 0x2e6,      // No such mode
    Underrun = 0x2e7,             // Data underrun
    Overrun = 0x2e8,              // Data overrun
    DeviceError = 0x2e9,          // Device malfunction
    NoCompletion = 0x2ea,         // No completion routine
    Aborted = 0x2eb,              // Operation aborted
    NoBandwidth = 0x2ec,          // Insufficient bandwidth
    NotResponding = 0x2ed,        // Device not responding
    IsoTooOld = 0x2ee,            // Isochronous request too old
    IsoTooNew = 0x2ef,            // Isochronous request too new
    NotFound = 0x2f0,             // Data was not found
    Invalid = 0x1                  // Invalid state or operation
};

namespace detail {
    struct IOKitErrorCategory : public std::error_category {
        const char* name() const noexcept override {
            return "IOKit";
        }
        
        std::string message(int ev) const override {
            using enum IOKitError;  // C++23 using enum
            switch (static_cast<IOKitError>(ev)) {
                case Success: return "Success";
                case Error: return "General error";
                case NoMemory: return "Memory allocation failed";
                case NoResources: return "Resource shortage";
                case IPCError: return "IPC error";
                case NoDevice: return "No such device";
                case NotPrivileged: return "Privilege violation";
                case BadArgument: return "Invalid argument";
                case LockedRead: return "Device read locked";
                case LockedWrite: return "Device write locked";
                case ExclusiveAccess: return "Device already open (exclusive access)";
                case BadMessageID: return "Message ID mismatch";
                case Unsupported: return "Unsupported function";
                case VMError: return "Virtual memory error";
                case InternalError: return "Internal error";
                case IOError: return "I/O error";
                case CannotLock: return "Cannot acquire lock";
                case NotOpen: return "Device not open";
                case NotReadable: return "Read not supported";
                case NotWritable: return "Write not supported";
                case NotAligned: return "Alignment error";
                case BadMedia: return "Media error";
                case StillOpen: return "Device(s) still open";
                case DMAError: return "DMA error";
                case Busy: return "Device busy";
                case Timeout: return "Operation timed out";
                case Offline: return "Device offline";
                case NotReady: return "Device not ready";
                case NotAttached: return "Device not attached";
                case NoChannels: return "No DMA channels available";
                case NoSpace: return "No space available";
                case PortExists: return "Port already exists";
                case CannotWire: return "Cannot wire memory";
                case NoInterrupt: return "No interrupt attached";
                case MessageTooLarge: return "Message too large";
                case NotPermitted: return "Operation not permitted";
                case NoPower: return "No power to device";
                case NoMedia: return "Media not present";
                case UnformattedMedia: return "Media not formatted";
                case UnsupportedMode: return "No such mode";
                case Underrun: return "Data underrun";
                case Overrun: return "Data overrun";
                case DeviceError: return "Device malfunction";
                case NoCompletion: return "No completion routine";
                case Aborted: return "Operation aborted";
                case NoBandwidth: return "Insufficient bandwidth";
                case NotResponding: return "Device not responding";
                case IsoTooOld: return "Isochronous request too old";
                case IsoTooNew: return "Isochronous request too new";
                case NotFound: return "Data was not found";
                case Invalid: return "Invalid state or operation";
                default: return std::format("Unknown error: 0x{:x}", std::to_underlying(static_cast<IOKitError>(ev)));
            }
        }
    };
}

inline const std::error_category& iokit_error_category() noexcept {
    static detail::IOKitErrorCategory category;
    return category;
}

inline std::error_code make_error_code(IOKitError e) noexcept {
    return {static_cast<int>(e), iokit_error_category()};
}

class IOKitException : public std::system_error {
public:
    explicit IOKitException(IOReturn error)
        : std::system_error(make_error_code(static_cast<IOKitError>(error))) {}
    
    explicit IOKitException(IOKitError error)
        : std::system_error(make_error_code(error)) {}
    
    IOReturn iokit_return() const noexcept {
        return static_cast<IOReturn>(code().value());
    }
};

} // namespace FWA

namespace std {
    template<>
    struct is_error_code_enum<FWA::IOKitError> : true_type {};
}
