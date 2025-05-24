// DaemonCore.cpp (Minimal for SHM Creation)
#include "DaemonCore.hpp"


namespace FWA {

DaemonCore::DaemonCore(std::shared_ptr<spdlog::logger> logger)
    : m_logger(std::move(logger)) {
    if (!m_logger) {
        auto sink = std::make_shared<spdlog::sinks::stderr_sink_mt>();
        m_logger = std::make_shared<spdlog::logger>("DaemonCore_Min_Fallback", sink);
    }
    m_logger->info("Minimal DaemonCore constructing...");
}

DaemonCore::~DaemonCore() {
    m_logger->info("Minimal DaemonCore destructing...");
    cleanupSharedMemory(); // Ensure cleanup on destruction
}

std::expected<void, MinimalDaemonCoreError> DaemonCore::initializeSharedMemory() {
    if (m_shmInitialized) {
        m_logger->warn("DaemonCore: Shared memory already initialized.");
        return std::unexpected(MinimalDaemonCoreError::AlreadyInitialized);
    }

    m_logger->info("DaemonCore: Initializing shared memory segment '{}'...", m_sharedMemoryName);

    // Optional: Unlink stale segment first.
    // For this minimal test, it's good practice to ensure a clean state if the daemon restarts.
    shm_unlink(m_sharedMemoryName.c_str());
    // We don't check the return of unlink here, as it might not exist, which is fine.

    // Create the shared memory segment exclusively.
    // The daemon IS the creator.
    m_shmFd = shm_open(m_sharedMemoryName.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);

    if (m_shmFd == -1) {
        if (errno == EEXIST) {
             m_logger->error("DaemonCore: SHM segment '{}' already exists (O_EXCL used). This might indicate a previous unclean shutdown or another instance. Try unlinking first.", m_sharedMemoryName);
             // Attempt to open it if it exists, assuming it was properly initialized before.
             // However, for a minimal *creator* test, failure on EEXIST is clearer.
             // Let's stick to exclusive creation for this minimal step for clarity.
             return std::unexpected(MinimalDaemonCoreError::SharedMemoryCreationFailure);
        }
        m_logger->critical("DaemonCore: shm_open (O_CREAT | O_EXCL) failed for '{}': {} - {}", m_sharedMemoryName, errno, strerror(errno));
        return std::unexpected(MinimalDaemonCoreError::SharedMemoryCreationFailure);
    }

    m_logger->info("DaemonCore: SHM segment '{}' created successfully with fd {}.", m_sharedMemoryName, m_shmFd);

    // Set the size of the shared memory segment.
    off_t requiredSize = sizeof(RTShmRing::SharedRingBuffer_POD);
    if (ftruncate(m_shmFd, requiredSize) == -1) {
        m_logger->critical("DaemonCore: ftruncate failed for '{}' (fd {}): {} - {}", m_sharedMemoryName, m_shmFd, errno, strerror(errno));
        close(m_shmFd); // Close the fd
        m_shmFd = -1;
        shm_unlink(m_sharedMemoryName.c_str()); // Clean up the created segment
        return std::unexpected(MinimalDaemonCoreError::SharedMemoryTruncateFailure);
    }

    m_logger->info("DaemonCore: SHM segment '{}' truncated to {} bytes.", m_sharedMemoryName, requiredSize);

    // In this minimal step, we are NOT mapping it with RingBufferManager yet.
    // We'll just close the FD for now, as shm_open created a persistent kernel object.
    // The ASPL will open it by name. RingBufferManager in DaemonCore will map it later if needed.
    // However, to *initialize* the ControlBlock_POD, the creator should map, write, and unmap.
    // Let's add that minimal initialization.

    void* shm_ptr = mmap(nullptr, requiredSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_shmFd, 0);
    if (shm_ptr == MAP_FAILED) {
        m_logger->error("DaemonCore: mmap failed for initial setup: {} - {}", errno, strerror(errno));
        close(m_shmFd);
        m_shmFd = -1;
        shm_unlink(m_sharedMemoryName.c_str());
        return std::unexpected(MinimalDaemonCoreError::SharedMemoryCreationFailure); // Re-use error
    }

    // Initialize ControlBlock_POD
    RTShmRing::SharedRingBuffer_POD* sharedBuffer = static_cast<RTShmRing::SharedRingBuffer_POD*>(shm_ptr);
    std::memset(sharedBuffer, 0, requiredSize); // Zero out the entire buffer
    sharedBuffer->control.abiVersion = kShmVersion;
    sharedBuffer->control.capacity = kRingCapacityPow2;
    // writeIndex and readIndex are already 0 due to memset.

    // Using atomic proxies for initialization (optional here as we have exclusive access during init)
    // RTShmRing::WriteIndexProxy(sharedBuffer->control).store(0, std::memory_order_relaxed);
    // RTShmRing::ReadIndexProxy(sharedBuffer->control).store(0, std::memory_order_relaxed);

    m_logger->info("DaemonCore: SHM ControlBlock initialized (ABI: {}, Capacity: {}).",
                   sharedBuffer->control.abiVersion, sharedBuffer->control.capacity);

    if (munmap(shm_ptr, requiredSize) == -1) {
        m_logger->warn("DaemonCore: munmap failed during initial setup: {} - {}", errno, strerror(errno));
        // Not necessarily fatal for the SHM object itself, but indicates an issue.
    }

    // We can close the fd now. The SHM object persists in the kernel until unlinked.
    // RingBufferManager will re-open by name when it needs to map.
    // Or, keep m_shmFd open if RingBufferManager will use it directly.
    // For this minimal test, closing is fine.
    close(m_shmFd);
    m_shmFd = -1; // Indicate FD is closed

    m_shmInitialized = true;
    m_logger->info("DaemonCore: Shared memory segment '{}' fully initialized by creator.", m_sharedMemoryName);
    return {};
}

void DaemonCore::cleanupSharedMemory() {
    if (m_shmInitialized) {
        m_logger->info("DaemonCore: Cleaning up shared memory segment '{}'...", m_sharedMemoryName);
        // If fd was kept open, close it: if (m_shmFd != -1) { close(m_shmFd); m_shmFd = -1; }

        // Unlink the shared memory segment. This allows the kernel to reclaim it
        // once all processes have unmapped and closed it.
        if (shm_unlink(m_sharedMemoryName.c_str()) == 0) {
            m_logger->info("DaemonCore: SHM segment '{}' unlinked.", m_sharedMemoryName);
        } else {
            if (errno != ENOENT) { // ENOENT is fine, means it was already gone
                m_logger->warn("DaemonCore: shm_unlink for '{}' failed: {} - {}", m_sharedMemoryName, errno, strerror(errno));
            }
        }
        m_shmInitialized = false;
    }
}

std::string DaemonCore::getSharedMemoryName() const {
    // It's okay to return the name even if not yet "initialized" successfully,
    // as the ASPL might try to connect before the daemon fully starts its core.
    // However, the XPC method should ideally only be callable after successful daemon core init.
    if (!m_shmInitialized) {
        m_logger->warn("DaemonCore: getSharedMemoryName() called, but SHM not yet successfully initialized. Returning configured name.");
    }
    return m_sharedMemoryName;
}

bool DaemonCore::isSharedMemoryInitialized() const {
    return m_shmInitialized;
}

} // namespace FWA