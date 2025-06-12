# Optimization Build Configuration

## Overview

The CMakeLists.txt now includes a `ENABLE_MAXIMUM_OPTIMIZATION` option that applies aggressive optimization flags to the daemon targets (`FWADaemon` and `FirewireAudioDaemon`) for maximum performance.

## Usage

### CMake Configuration

```bash
# Configure with maximum optimization
cmake -DENABLE_MAXIMUM_OPTIMIZATION=ON -DCMAKE_BUILD_TYPE=Release .

# Or use the provided script
./build-optimized.sh
```

### Build Targets

The optimization flags are applied to these daemon targets:
- `FWADaemon` (XPC Service)
- `FirewireAudioDaemon` (CLI daemon)

## Optimization Flags Applied

### Compiler Flags

| Flag | Purpose |
|------|---------|
| `-O3` | Maximum optimization level |
| `-DNDEBUG` | Disable debug assertions |
| `-flto` | Link-time optimization |
| `-march=native` | Optimize for current CPU architecture |
| `-mtune=native` | Tune for current CPU |
| `-ffast-math` | Enable fast math optimizations |
| `-funroll-loops` | Unroll loops for performance |
| `-finline-functions` | Inline functions aggressively |
| `-fomit-frame-pointer` | Omit frame pointer for better performance |
| `-fno-stack-protector` | Disable stack protection for max speed |
| `-fno-exceptions` | Disable exception handling overhead |
| `-fno-rtti` | Disable RTTI overhead |
| `-fstrict-aliasing` | Enable strict aliasing optimizations |
| `-fvectorize` | Enable auto-vectorization |
| `-fslp-vectorize` | Enable SLP vectorization |

### Linker Flags

| Flag | Purpose |
|------|---------|
| `-flto` | Link-time optimization |
| `-Wl,-dead_strip` | Remove dead code (macOS specific) |
| `-Wl,-no_eh_frame_hdr` | Remove exception handling overhead |

## Performance Considerations

### Benefits
- **Maximum CPU utilization**: Native architecture optimizations
- **Reduced binary size**: Dead code elimination
- **Faster execution**: Aggressive inlining and loop unrolling
- **Better vectorization**: Auto-vectorization for SIMD operations
- **Reduced overhead**: Disabled debugging and safety features

### Trade-offs
- **Debugging difficulty**: Optimized code is harder to debug
- **Reduced safety**: Stack protection and exception handling disabled
- **Architecture specific**: `-march=native` makes binaries non-portable
- **Longer build times**: LTO increases compilation time
- **Potential instability**: Aggressive optimizations may introduce edge cases

## Compatibility with Other Features

### Sanitizers
The build system will issue a warning if both maximum optimization and sanitizers are enabled simultaneously, as they can interfere with each other.

### Architecture Targeting
Works with both native (arm64) and x86_64 builds when using `BUILD_X86_64_TARGETS`.

## Recommended Usage

### Production Builds
```bash
cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_MAXIMUM_OPTIMIZATION=ON .
```

### Development Builds
```bash
cmake -DCMAKE_BUILD_TYPE=Debug .
# Do not use ENABLE_MAXIMUM_OPTIMIZATION for development
```

### Testing with Sanitizers
```bash
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON .
# Optimization disabled for proper sanitizer operation
```

## Performance Testing

After building with optimizations, consider:

1. **Benchmarking**: Compare performance against non-optimized builds
2. **Profiling**: Use instruments to verify optimization effectiveness
3. **Stress testing**: Ensure stability under high load
4. **Memory usage**: Monitor for any increased memory consumption

## Example Build Commands

```bash
# Quick optimized build
./build-optimized.sh

# Manual optimized build
mkdir build-opt && cd build-opt
cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_MAXIMUM_OPTIMIZATION=ON ..
cmake --build . --target FWADaemon FirewireAudioDaemon --parallel

# Compare with debug build
mkdir build-debug && cd build-debug
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build . --target FWADaemon FirewireAudioDaemon --parallel
```
