// DaemonCore.hpp (Minimal for SHM Creation)
#pragma once

#include <string>
#include <memory>       // For std::shared_ptr
#include <expected>     // For std::expected (C++23)

#include <spdlog/spdlog.h> // For spdlog::logger
#include <spdlog/sinks/stdout_sinks.h>

// Required for SHM operations and SharedRingBuffer_POD size
#include <sys/mman.h>   // For shm_open, shm_unlink
#include <sys/stat.h>   // For fstat, mode_t
#include <fcntl.h>      // For O_CREAT, O_RDWR, O_EXCL
#include <unistd.h>     // For ftruncate, close
#include <cerrno>       // For errno
#include <cstring>      // For strerror
#include "shared/SharedMemoryStructures.hpp" // For RTShmRing::SharedRingBuffer_POD

namespace FWA {

enum class MinimalDaemonCoreError {
    Success = 0,
    AlreadyInitialized,
    SharedMemoryCreationFailure,
    SharedMemoryTruncateFailure,
    NotInitialized
};

class DaemonCore {
public:
    explicit DaemonCore(std::shared_ptr<spdlog::logger> logger);
    ~DaemonCore();

    // Initializes the core, primarily creating the shared memory segment.
    std::expected<void, MinimalDaemonCoreError> initializeSharedMemory();

    // Cleans up shared memory.
    void cleanupSharedMemory();

    // Gets the name of the shared memory segment.
    std::string getSharedMemoryName() const;

    bool isSharedMemoryInitialized() const;

private:
    std::shared_ptr<spdlog::logger> m_logger;
    const std::string m_sharedMemoryName = "/fwa_daemon_shm_v1_minimal_test"; // Use a distinct name for testing
    bool m_shmInitialized = false;
    int m_shmFd = -1; // Store the fd if we need to map it later in DaemonCore
};

} // namespace FWA