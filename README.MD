# FireWire Audio Daemon

A modern C++ application (potentially evolving into a daemon) for interfacing with FireWire (IEEE 1394) audio devices on macOS, providing low-level device discovery, control, and configuration capabilities.

[![Discord](https://img.shields.io/badge/Join%20Discord-5865F2?logo=discord&logoColor=white)](https://discord.gg/c82rmSEEPY)

## Overview

This project implements a FireWire audio device daemon that handles:

- Device discovery and hot-plugging
- AV/C (Audio/Video Control) command interface
- Music and Audio subunit control

## Features

- **Device Discovery**: Automatic detection and monitoring of FireWire audio devices
- **Command Interface**: Implementation of AV/C command protocol

### Plug Management (TODO)

- Isochronous input/output plugs
- External input/output plugs
- Music subunit plugs
- Audio subunit plugs
- Stream format discovery

### Modern C++ Design

- RAII principles
- Smart pointers
- STL containers
- Error handling with `std::expected`
- Thread-safe operations

## Requirements

- XCode 15 or later
- CMake 3.21 or later
- Modern C++ compiler with C++23 support

## Dependencies

- **spdlog** (v1.13.0) - Modern C++ logging
- **Catch2** (v3.5.2) - Testing framework
- **IOKit framework**
- **CoreFoundation framework**

## Building

I recommend create a XCode project using cmake:
```sh
mkdir build && cd build
cmake -G Xcode ..
open FireWireAudioDaemon.xcodeproj
```

## Architecture

The application is built around several key components:

### Device Discovery (IOKitFireWireDeviceDiscovery)

- Handles device arrival/removal
- Manages device lifecycle
- Provides device enumeration

### Device Controller (DeviceController)

- High-level device management
- Command routing
- State management

### Command Interface (CommandInterface)

- AV/C command implementation
- Low-level device communication
- Error handling

### Device Parser (DeviceParser) (TODO)

- Capability discovery
- Format parsing
- Plug configuration

## Future plans

The primary goal is to restore  FireWire audio functionality on modern macOS versions by implementing isochronous audio stream handling. Since Apple removed `AppleFWAudio.kext` from the latest versions of their operating systems, and older devices rely on a specific set of standards, restoring FireWire audio functionality is a feasible task. Currently, I am implementing device handling and planning to add isochronous stream support. I have successfully sent audio to and from a device as a proof of concept, but before advancing further, I need a proper tool to handle device controls effectively.

## Contributing

Contributions are highly encouraged! If you're interested, please submit issues and pull requests to help improve and develop the project further.

### Code Formatting

This project uses clang-format for consistent code formatting. A `.clang-format` file is provided in the root directory that defines the project's coding style.

#### Required Version

This project requires clang-format version 16.0.0 or later. A script is provided to install the correct version:

```sh
# Check if the correct version is installed
./tools/install-clang-format.sh --check-only

# Install the correct version if needed
./tools/install-clang-format.sh
```

The script will detect your operating system and install clang-format using the appropriate package manager (Homebrew for macOS, apt/dnf/pacman for Linux).

#### CMake Integration

The project includes CMake targets for code formatting:

```sh
# Format all source files
cmake --build build --target format

# Check if all source files are formatted correctly
cmake --build build --target format-check

# Install the required clang-format version
cmake --build build --target install-clang-format
```

These targets are available after configuring the project with CMake.

#### Editor Integration

**VSCode**
1. The project's `.vscode/settings.json` is already configured to use clang-format
2. Use the extension of your choice (c/c++ extension or clang-format extension) to format your code on save
**Xcode**
1. Install [ClangFormat-Xcode](https://github.com/mapbox/XcodeClangFormat)
2. Configure it to use the project's `.clang-format` file

#### Pre-commit Hook

A Git pre-commit hook is included that automatically formats your code before committing. The hook:
- Runs clang-format on all staged C/C++/Objective-C files
- Re-stages the formatted files
- Allows you to bypass the hook with `git commit --no-verify` if needed

To enable the hook, make sure it's executable:
```sh
chmod +x .git/hooks/pre-commit
```

## License

This project is licensed under the MIT License.

## Acknowledgments

- Based on Apple's IOKit FireWire Family framework architecture
- Inspired by AVCVideoServices sample code (found in the FireWire SDK)
