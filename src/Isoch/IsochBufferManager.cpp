#include "FWA/Isoch/IsochBufferManager.hpp"
#include <mach/mach.h>
#include <strings.h>  // for bzero

namespace FWA {
namespace Isoch {

IsochBufferManager::IsochBufferManager(
    std::shared_ptr<spdlog::logger> logger,
    uint32_t totalCycles,
    uint32_t clientBufferSize)
    : logger_(std::move(logger))
    , totalCycles_(totalCycles)
    , clientBufferSize_(clientBufferSize) {
    
    if (logger_) {
        logger_->info("IsochBufferManager created: totalCycles={}, clientBufferSize={}",
                     totalCycles_, clientBufferSize_);
    }
}

IsochBufferManager::~IsochBufferManager() {
    if (pTransmitBuffer_) {
        vm_deallocate(mach_task_self(), 
                     reinterpret_cast<vm_address_t>(pTransmitBuffer_),
                     totalBufferSize_);
        
        if (logger_) {
            logger_->debug("Deallocated buffer: size={}, address=0x{:x}",
                         totalBufferSize_,
                         reinterpret_cast<uintptr_t>(pTransmitBuffer_));
        }
    }
}

std::expected<void, IOKitError> IsochBufferManager::allocateBuffers() {
    using namespace detail;
    
    // Calculate required sizes for each region
    uint32_t cipHeadersSize = totalCycles_ * CIP_HEADER_SIZE;
    uint32_t isochHeadersSize = totalCycles_ * ISOCH_HEADER_SIZE;
    uint32_t timeStampsSize = totalCycles_ * TIMESTAMP_SIZE;
    
    // Calculate page-aligned sizes
    uint32_t alignedClientSize = alignToPage(clientBufferSize_);
    uint32_t alignedCIPSize = alignToPage(cipHeadersSize);
    uint32_t alignedIsochSize = alignToPage(isochHeadersSize);
    uint32_t alignedTimestampSize = alignToPage(timeStampsSize);
    
    // Calculate total buffer size needed
    totalBufferSize_ = alignedClientSize + alignedCIPSize + 
                      alignedIsochSize + alignedTimestampSize;
    
    // Allocate VM memory
    uint8_t* pBuffer = nullptr;
    kern_return_t result = vm_allocate(
        mach_task_self(),
        reinterpret_cast<vm_address_t*>(&pBuffer),
        totalBufferSize_,
        VM_FLAGS_ANYWHERE
    );
    
    if (result != KERN_SUCCESS) {
        logger_->error("Failed to allocate buffer: size={}, error={}", 
                      totalBufferSize_, result);
        return std::unexpected(IOKitError(kIOReturnNoMemory));
    }
    
    // Zero the memory
    bzero(pBuffer, totalBufferSize_);
    
    // Setup buffer pointers with proper offsets
    pTransmitBuffer_ = pBuffer;
    pClientBuffer_ = pBuffer;  // Client buffer at start
    
    // Set up other buffer regions after client buffer
    pCIPHeaders_ = reinterpret_cast<uint32_t*>(pBuffer + alignedClientSize);
    pIsochHeaders_ = reinterpret_cast<uint32_t*>(pBuffer + alignedClientSize + alignedCIPSize);
    pTimeStamps_ = reinterpret_cast<uint32_t*>(pBuffer + alignedClientSize + 
                                               alignedCIPSize + alignedIsochSize);
    
    // Configure buffer range for FireWire
    bufferRange_.address = reinterpret_cast<IOVirtualAddress>(pBuffer);
    bufferRange_.length = totalBufferSize_;
    
    if (logger_) {
        logger_->info("Buffer allocated and initialized:");
        logger_->info("  Total size: {} bytes", totalBufferSize_);
        logger_->info("  Base address: 0x{:x}", reinterpret_cast<uintptr_t>(pBuffer));
        logger_->info("  Client buffer: 0x{:x}", reinterpret_cast<uintptr_t>(pClientBuffer_));
        logger_->info("  CIP headers: 0x{:x}", reinterpret_cast<uintptr_t>(pCIPHeaders_));
        logger_->info("  Isoch headers: 0x{:x}", reinterpret_cast<uintptr_t>(pIsochHeaders_));
        logger_->info("  Timestamps: 0x{:x}", reinterpret_cast<uintptr_t>(pTimeStamps_));
    }
    
    return {};
}

IOVirtualRange IsochBufferManager::getBufferRange() const {
    return bufferRange_;
}

uint8_t* IsochBufferManager::getClientBuffer() const noexcept {
    return pClientBuffer_;
}

uint32_t* IsochBufferManager::getCIPHeaders() const noexcept {
    return pCIPHeaders_;
}

uint32_t* IsochBufferManager::getIsochHeaders() const noexcept {
    return pIsochHeaders_;
}

uint32_t* IsochBufferManager::getTimeStamps() const noexcept {
    return pTimeStamps_;
}

uint32_t IsochBufferManager::getClientBufferSize() const noexcept {
    return clientBufferSize_;
}

uint32_t IsochBufferManager::getTotalBufferSize() const noexcept {
    return totalBufferSize_;
}

bool IsochBufferManager::isAddressInClientBuffer(const void* address) const noexcept {
    auto addr = reinterpret_cast<const uint8_t*>(address);
    return (addr >= pClientBuffer_ && 
            addr < (pClientBuffer_ + clientBufferSize_));
}

} // namespace Isoch
} // namespace FWA