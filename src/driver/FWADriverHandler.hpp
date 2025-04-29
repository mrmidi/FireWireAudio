#ifndef FWADRIVERHANDLER_HPP
#define FWADRIVERHANDLER_HPP

#include <aspl/Driver.hpp>
#include <aspl/Stream.hpp>
#include <aspl/ControlRequestHandler.hpp>
#include <aspl/IORequestHandler.hpp>
#include <memory>
#include <shared/SharedMemoryStructures.hpp> // Include the new header
#include <vector>

class FWADriverHandler : public aspl::ControlRequestHandler, public aspl::IORequestHandler {
public:
    FWADriverHandler(); // Constructor
    ~FWADriverHandler(); // Destructor for cleanup

    OSStatus OnStartIO() override;
    void OnStopIO() override;

    // Remove OnWriteMixedOutput (will move logic to Device)
    // Add shared memory setup/teardown
    bool SetupSharedMemory(const std::string& shmName);
    void TeardownSharedMemory();

    // Helper for device to check SHM state
    bool IsSharedMemoryReady() const { return controlBlock_ && ringBuffer_; }
    // Helper for device to push audio data
    bool PushToSharedMemory(const AudioBufferList* src, const AudioTimeStamp& ts, uint32_t frames, uint32_t bytesPerFrame);

private:
    // Shared Memory state
    void* shmPtr_ = nullptr; // Raw pointer to the mapped memory
    int shmFd_ = -1;         // File descriptor for POSIX shared memory
    size_t shmSize_ = 0;     // Total size of the mapped region
    RTShmRing::ControlBlock_POD* controlBlock_ = nullptr; // Pointer into shmPtr_
    RTShmRing::AudioChunk_POD*   ringBuffer_ = nullptr;   // Pointer into shmPtr_

    // Local non-atomic counter for RT thread (see recommendation 2.8)
    uint32_t localOverrunCounter_ = 0;
    // Add timer mechanism later to periodically update shared atomic counter
};

#endif // FWADRIVERHANDLER_HPP
