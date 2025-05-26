// RingBufferManager.cpp
#include "xpc/FWAXPC/RingBufferManager.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <stdexcept>
#include <os/log.h>

namespace {
static constexpr size_t kSliceSize = 64; // FireWire packet size
}

bool RingBufferManager::map(int shmFd, bool isCreator) {
    if (shm_) {
        os_log(OS_LOG_DEFAULT, "RingBufferManager::map: already mapped, returning true");
        return true; // already mapped
    }

    os_log(OS_LOG_DEFAULT, "RingBufferManager::map: starting mapping process, fd=%d, isCreator=%d", shmFd, isCreator);
    
    shmSize_ = sizeof(RTShmRing::SharedRingBuffer_POD);
    void* ptr = ::mmap(nullptr, shmSize_, PROT_READ | PROT_WRITE, MAP_SHARED, shmFd, 0);
    if (ptr == MAP_FAILED) {
        spdlog::error("RingBufferManager::map: mmap failed: {}", errno);
        os_log(OS_LOG_DEFAULT, "RingBufferManager::map: mmap failed with errno=%d", errno);
        return false;
    }
    os_log(OS_LOG_DEFAULT, "RingBufferManager::map: mmap successful, ptr=%p, size=%zu", ptr, shmSize_);
    if (::mlock(ptr, shmSize_) != 0) {
        spdlog::warn("RingBufferManager::map: mlock failed: {}", errno);
        os_log(OS_LOG_DEFAULT, "RingBufferManager::map: mlock failed with errno=%d", errno);
    } else {
        os_log(OS_LOG_DEFAULT, "RingBufferManager::map: mlock successful");
    }
    shm_ = static_cast<RTShmRing::SharedRingBuffer_POD*>(ptr);

    if (isCreator) {
        os_log(OS_LOG_DEFAULT, "RingBufferManager::map: initializing shared memory as creator");
        std::memset(shm_, 0, shmSize_);
        shm_->control.abiVersion = kShmVersion;
        shm_->control.capacity   = kRingCapacityPow2;
        os_log(OS_LOG_DEFAULT, "RingBufferManager::map: shared memory initialized");
    } else {
        os_log(OS_LOG_DEFAULT, "RingBufferManager::map: validating shared memory header as reader");
        if (shm_->control.abiVersion != kShmVersion ||
            shm_->control.capacity   != kRingCapacityPow2) {
            spdlog::error("RingBufferManager::map: SHM header mismatch");
            os_log(OS_LOG_DEFAULT, "RingBufferManager::map: SHM header mismatch");
            ::munlock(ptr, shmSize_);
            ::munmap(ptr, shmSize_);
            shm_ = nullptr;
            return false;
        }
        os_log(OS_LOG_DEFAULT, "RingBufferManager::map: shared memory header validation successful");
    }

    running_.store(true, std::memory_order_release);
    os_log(OS_LOG_DEFAULT, "RingBufferManager::map: starting reader thread");
    reader_ = std::thread(&RingBufferManager::readerLoop, this);
    os_log(OS_LOG_DEFAULT, "RingBufferManager::map: mapping completed successfully");
    return true;
}

void RingBufferManager::unmap() {
    os_log(OS_LOG_DEFAULT, "RingBufferManager::unmap: starting unmap process");
    
    if (running_.load(std::memory_order_acquire)) {
        os_log(OS_LOG_DEFAULT, "RingBufferManager::unmap: stopping reader thread");
        running_.store(false, std::memory_order_release);
        if (reader_.joinable()) {
            os_log(OS_LOG_DEFAULT, "RingBufferManager::unmap: waiting for reader thread to join");
            reader_.join();
            os_log(OS_LOG_DEFAULT, "RingBufferManager::unmap: reader thread joined successfully");
        }
    }
    
    if (shm_) {
        os_log(OS_LOG_DEFAULT, "RingBufferManager::unmap: unmapping shared memory");
        ::munlock(shm_, shmSize_);
        ::munmap(shm_, shmSize_);
        shm_ = nullptr;
        os_log(OS_LOG_DEFAULT, "RingBufferManager::unmap: shared memory unmapped successfully");
    } else {
        os_log(OS_LOG_DEFAULT, "RingBufferManager::unmap: no shared memory to unmap");
    }
}

void RingBufferManager::readerLoop() {

    auto* prov = packetProvider_.load(std::memory_order_acquire);
    if (!shm_ || !prov) {
        spdlog::error("RingBufferManager::readerLoop: missing shm or provider");
        os_log(OS_LOG_DEFAULT, "RingBufferManager::readerLoop: missing shm=%p or provider=%p", shm_, static_cast<void*>(prov));
        return;
    }

    os_log(OS_LOG_DEFAULT, "RingBufferManager::readerLoop: starting reader loop");
    
    RTShmRing::AudioChunk_POD localChunk;
    size_t totalChunksProcessed = 0;
    size_t totalBytesProcessed = 0;


    while (running_.load(std::memory_order_relaxed)) {
        FWA::Isoch::ITransmitPacketProvider* currentProvider = packetProvider_.load(std::memory_order_acquire);

        // Try to pop one chunk
        if (!RTShmRing::pop(shm_->control, shm_->ring, localChunk)) {
            // No data available, sleep briefly
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            continue;
        }

        // Chunk successfully popped
        if (!currentProvider) {
            os_log(OS_LOG_DEFAULT, "RingBufferManager::readerLoop: chunk popped but no provider, discarding chunk of %u bytes", localChunk.dataBytes);
            continue; // Discard and go to next pop
        }

        totalChunksProcessed++;
        totalBytesProcessed += localChunk.dataBytes;
        os_log(OS_LOG_DEFAULT, "RingBufferManager::readerLoop: popped chunk %zu, size=%u bytes, total processed=%zu bytes", 
               totalChunksProcessed, localChunk.dataBytes, totalBytesProcessed);

        const std::byte* ptr = localChunk.audio;
        const std::byte* end = ptr + localChunk.dataBytes;
        size_t slicesInChunk = 0;

        // Slice into 64-byte packets
        while (ptr < end && running_.load(std::memory_order_relaxed)) {
            size_t remain = static_cast<size_t>(end - ptr);
            size_t slice  = (remain >= kSliceSize ? kSliceSize : remain);

            if (currentProvider->pushAudioData(ptr, slice)) {
                ptr += slice;
                slicesInChunk++;
                os_log(OS_LOG_DEFAULT, "RingBufferManager::readerLoop: data pushed successfully, slice=%zu bytes, slices_in_chunk=%zu", slice, slicesInChunk);
            } else {
                os_log(OS_LOG_DEFAULT, "RingBufferManager::readerLoop: failed to push data, slice=%zu bytes, retrying", slice);
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        }
        
        if (slicesInChunk > 0) {
            os_log(OS_LOG_DEFAULT, "RingBufferManager::readerLoop: completed chunk processing, %zu slices processed", slicesInChunk);
        }
    }
    
    os_log(OS_LOG_DEFAULT, "RingBufferManager::readerLoop: exiting reader loop, total chunks processed=%zu, total bytes=%zu", 
           totalChunksProcessed, totalBytesProcessed);
}
