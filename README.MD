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

Contributions are highly encouraged!  If you're interested, please submit issues and pull requests to help improve and develop the project further.

## License

This project is licensed under the MIT License.

## Acknowledgments

- Based on Apple's IOKit FireWire Family framework architecture
- Inspired by AVCVideoServices sample code (found in the FireWire SDK)
