# FireWire Audio Daemon - Dev Guide

- ripgrep is avalable
- request any additional packages/bundles/libraries that is required

## Project Structure
- **FWA**: Core library for FireWire audio device management
  - Device discovery (IOKitFireWireDeviceDiscovery)
  - Command Interface (AV/C commands)
  - Device Controller
  - Device Parser
- **FWAIsoch**: Library for isochronous streaming
  - AudioDeviceStream
  - IsoStreamHandler
  - Components (AmdtpDCLManager, AmdtpPortManager, etc.)
  - Core (AmdtpReceiver, AmdtpTransmitter, etc.)
- **FireWireAudioDaemon**: Main executable
- **FWADriver**: macOS audio driver (ASPL-based)
- **FWADaemon.xpc**: XPC service for audio processing

## External Dependencies
- **spdlog**: Modern C++ logging library (v1.13.0)
- **Catch2**: Testing framework (v3.5.2)
- **libASPL**: Apple System Programming Layer library

## Build Commands
- Configure: `cmake --preset xcode-debug` or `cmake --preset default`
- Build: `cmake --build --preset xcode-debug`
- Build specific target: `cmake --build --preset xcode-debug --target FireWireAudioDaemon`
- Build driver: `cmake --build --preset xcode-debug --target FWADriver`
- Generate docs: `cmake --build --preset xcode-debug --target docs`

## Testing
- Run all tests: `ctest --preset xcode-debug`
- Run single test: `ctest --preset xcode-debug -R "IOKitFireWireDeviceDiscovery - Initial Tests"`
- Run with output: `ctest --preset xcode-debug --output-on-failure`

## Code Style
- C++23 standard with modern features (smart pointers, STL containers)
- Use `std::expected<T, Error>` for error handling (NOT exceptions)
- Prefix member variables with `m_` (public) or `_` (private)
- Classes start with uppercase, methods with lowercase camelCase
- Include guards use filename_h format (NO pragma once)
- Always initialize member variables in constructor initializer lists
- Prefer composition over inheritance
- Thread safety: ensure proper resource cleanup and RAII principles
- Namespace everything under `FWA::`
- Indent with 4 spaces

## Project Purpose
This project aims to restore FireWire audio functionality on modern macOS by implementing:
1. Device discovery and control (via AV/C commands)
2. Isochronous streaming for audio input/output
3. Core Audio driver integration

The current focus is on implementing isochronous streaming support (AMDTP protocol) on the "streams" branch.