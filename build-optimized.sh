#!/bin/bash

# Build script for FireWire Audio Daemon with maximum optimization
# Usage: ./build-optimized.sh [build_dir]

set -e

BUILD_DIR="${1:-build-optimized}"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "🚀 Building FireWire Audio Daemon with maximum optimization..."
echo "Build directory: $BUILD_DIR"
echo "Project root: $PROJECT_ROOT"

# Create and enter build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with maximum optimization
cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_MAXIMUM_OPTIMIZATION=ON \
  -DBUILD_X86_64_TARGETS=OFF \
  "$PROJECT_ROOT"

# Build the daemon targets
echo "🔨 Building daemon targets..."
cmake --build . --target FWADaemon --parallel $(nproc 2>/dev/null || sysctl -n hw.ncpu)
cmake --build . --target FirewireAudioDaemon --parallel $(nproc 2>/dev/null || sysctl -n hw.ncpu)

echo "✅ Build complete!"
echo "Optimized binaries:"
echo "  - XPC Service: $PWD/FWADaemon.xpc"
echo "  - CLI Daemon:  $PWD/FirewireAudioDaemon"

# Display binary sizes
if command -v ls &> /dev/null; then
    echo ""
    echo "📊 Binary sizes:"
    ls -lh FirewireAudioDaemon 2>/dev/null || true
    ls -lh FWADaemon.xpc/Contents/MacOS/FWADaemon 2>/dev/null || true
fi
