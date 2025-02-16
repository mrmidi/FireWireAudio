#pragma once
#include <cstdint>
#include <memory>
#include <expected>
#include "FWA/Error.h"
#include <IOKit/IOKitLib.h>
#include <spdlog/logger.h>

namespace FWA {
namespace Isoch {

namespace detail {
    static constexpr uint32_t PAGE_SIZE = 4096;
    static constexpr uint32_t CIP_HEADER_SIZE = 8;
    static constexpr uint32_t ISOCH_HEADER_SIZE = 16;
    static constexpr uint32_t TIMESTAMP_SIZE = 4;

    constexpr uint32_t alignToPage(uint32_t size) {
        return ((size + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
    }
}

class IsochBufferManager {
public:
    IsochBufferManager(std::shared_ptr<spdlog::logger> logger,
                      uint32_t totalCycles,
                      uint32_t clientBufferSize);
    ~IsochBufferManager();

    std::expected<void, IOKitError> allocateBuffers();
    IOVirtualRange getBufferRange() const;
    
    uint8_t* getClientBuffer() const noexcept;
    uint32_t* getCIPHeaders() const noexcept;
    uint32_t* getIsochHeaders() const noexcept;
    uint32_t* getTimeStamps() const noexcept;
    
    uint32_t getClientBufferSize() const noexcept;
    uint32_t getTotalBufferSize() const noexcept;
    bool isAddressInClientBuffer(const void* address) const noexcept;

private:
    std::shared_ptr<spdlog::logger> logger_;
    uint32_t totalCycles_;
    uint32_t clientBufferSize_;
    uint8_t* pTransmitBuffer_{nullptr};
    uint32_t* pCIPHeaders_{nullptr};
    uint32_t* pIsochHeaders_{nullptr};
    uint32_t* pTimeStamps_{nullptr};
    uint8_t* pClientBuffer_{nullptr};
    IOVirtualRange bufferRange_{};
    uint32_t totalBufferSize_{0};
};

} // namespace Isoch
} // namespace FWA