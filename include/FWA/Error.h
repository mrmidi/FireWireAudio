// include/FWA/Error.h
// Synopsis: Wrapper for IOReturn error codes.

#pragma once

#include <system_error>
#include <IOKit/IOReturn.h>

namespace FWA {

enum class IOKitError {
    Success = 0,                    // Operation completed successfully
    Error = 0x2bc,                 // General error
    NoMemory = 0x2bd,             // Memory allocation failed
    NotInitialized = 0x2bc + 0x100, // Not initialized
    ReadOnly = 0x2bc + 0x101,       // Read only
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
    VMError = 0x2c8,              // Misc. VM failure
    InternalError = 0x2c9,        // Internal error
    IOError = 0x2ca,              // General I/O error
    CannotLock = 0x2cc,           // Can't acquire lock
    NotOpen = 0x2cd,              // Device not open
    NotReadable = 0x2ce,          // Read not supported
    NotWritable = 0x2cf,          // Write not supported
    NotAligned = 0x2d0,           // Alignment error
    BadMedia = 0x2d1,             // Media Error
    StillOpen = 0x2d2,            // Device(s) still open
    RLDError = 0x2d3,             // RLD failure
    DMAError = 0x2d4,             // DMA failure
    Busy = 0x2d5,                 // Device Busy
    Timeout = 0x2d6,              // I/O Timeout
    Offline = 0x2d7,              // Device offline
    NotReady = 0x2d8,             // Not ready
    NotAttached = 0x2d9,          // Device not attached
    NoChannels = 0x2da,           // No DMA channels left
    NoSpace = 0x2db,              // No space for data
    PortExists = 0x2dd,           // Port already exists
    CannotWire = 0x2de,           // Can't wire down physical memory
    NoInterrupt = 0x2df,          // No interrupt attached
    NoFrames = 0x2e0,             // No DMA frames enqueued
    MessageTooLarge = 0x2e1,      // Oversized msg received on interrupt port
    NotPermitted = 0x2e2,         // Not permitted
    NoPower = 0x2e3,              // No power to device
    NoMedia = 0x2e4,              // Media not present
    UnformattedMedia = 0x2e5,     // Media not formatted
    UnsupportedMode = 0x2e6,      // No such mode
    Underrun = 0x2e7,             // Data underrun
    Overrun = 0x2e8,              // Data overrun
    DeviceError = 0x2e9,          // Device not working properly
    NoCompletion = 0x2ea,         // A completion routine is required
    Aborted = 0x2eb,              // Operation aborted
    NoBandwidth = 0x2ec,          // Bus bandwidth would be exceeded
    NotResponding = 0x2ed,        // Device not responding
    IsoTooOld = 0x2ee,            // Isochronous I/O request for distant past
    IsoTooNew = 0x2ef,            // Isochronous I/O request for distant future
    NotFound = 0x2f0,             // Data was not found
    InvalidState = 0x2f1         // Invalid state
};

// Make IOKitError work with std::error_code
namespace detail {
    struct IOKitErrorCategory : std::error_category {
        const char* name() const noexcept override { return "IOKit"; }
        std::string message(int ev) const override {
            switch (static_cast<IOKitError>(ev)) {
                case IOKitError::Success: return "Success";
                case IOKitError::Error: return "General error";
                case IOKitError::NoMemory: return "Memory allocation failed";
                case IOKitError::NoResources: return "Resource shortage";
                case IOKitError::IPCError: return "IPC error";
                case IOKitError::NoDevice: return "No such device";
                case IOKitError::NotPrivileged: return "Privilege violation";
                case IOKitError::BadArgument: return "Invalid argument";
                case IOKitError::LockedRead: return "Device read locked";
                case IOKitError::LockedWrite: return "Device write locked";
                case IOKitError::ExclusiveAccess: return "Device already open (exclusive access)";
                case IOKitError::BadMessageID: return "Message ID mismatch";
                case IOKitError::Unsupported: return "Unsupported function";
                case IOKitError::VMError: return "Virtual memory error";
                case IOKitError::InternalError: return "Internal error";
                case IOKitError::IOError: return "I/O error";
                case IOKitError::CannotLock: return "Cannot acquire lock";
                case IOKitError::NotOpen: return "Device not open";
                case IOKitError::NotReadable: return "Read not supported";
                case IOKitError::NotWritable: return "Write not supported";
                case IOKitError::NotAligned: return "Alignment error";
                case IOKitError::BadMedia: return "Media error";
                case IOKitError::StillOpen: return "Device(s) still open";
                case IOKitError::DMAError: return "DMA error";
                case IOKitError::Busy: return "Device busy";
                case IOKitError::Timeout: return "Operation timed out";
                case IOKitError::Offline: return "Device offline";
                case IOKitError::NotReady: return "Device not ready";
                case IOKitError::NotAttached: return "Device not attached";
                case IOKitError::NoChannels: return "No DMA channels available";
                case IOKitError::NoSpace: return "No space available";
                case IOKitError::PortExists: return "Port already exists";
                case IOKitError::CannotWire: return "Cannot wire memory";
                case IOKitError::NoInterrupt: return "No interrupt attached";
                case IOKitError::NoFrames: return "No DMA frames available";
                case IOKitError::MessageTooLarge: return "Message too large";
                case IOKitError::NotPermitted: return "Operation not permitted";
                case IOKitError::NoPower: return "No power to device";
                case IOKitError::NoMedia: return "No media present";
                case IOKitError::UnformattedMedia: return "Unformatted media";
                case IOKitError::UnsupportedMode: return "Unsupported mode";
                case IOKitError::Underrun: return "Data underrun";
                case IOKitError::Overrun: return "Data overrun";
                case IOKitError::DeviceError: return "Device malfunction";
                case IOKitError::NoCompletion: return "No completion routine";
                case IOKitError::Aborted: return "Operation aborted";
                case IOKitError::NoBandwidth: return "Insufficient bandwidth";
                case IOKitError::NotResponding: return "Device not responding";
                case IOKitError::IsoTooOld: return "Isochronous request too old";
                case IOKitError::IsoTooNew: return "Isochronous request too new";
                case IOKitError::NotFound: return "Not found";
                case IOKitError::InvalidState: return "Invalid state";
                default: return "Unknown error";
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

// Enable automatic conversion to std::error_code
} // namespace FWA

namespace std {
    template<>
    struct is_error_code_enum<FWA::IOKitError> : true_type {};
}
