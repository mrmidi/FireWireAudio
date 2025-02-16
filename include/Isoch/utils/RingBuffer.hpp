// Copyright 2007-2012 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RAUL_RINGBUFFER_HPP
#define RAUL_RINGBUFFER_HPP

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <spdlog/spdlog.h>

// Include ARM NEON headers
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

namespace raul {

/**
   A lock-free RingBuffer.
   Thread-safe with a single reader and single writer, and real-time safe
   on both ends.
   @ingroup raul
*/
class RingBuffer
{
public:
    /**
       Create a new RingBuffer.
       @param size Size in bytes (note this may be rounded up).
    */
    explicit RingBuffer(uint32_t size, std::shared_ptr<spdlog::logger> logger = nullptr)
        : _size(next_power_of_two(size))
        , _size_mask(_size - 1)
        , _buf(new char[_size])
        , _logger(logger)
    {
        if (_logger) {
            _logger->debug("[RingBuffer] Initialized with size: {} bytes", _size);
            _logger->debug("[RingBuffer] Size in samples: {}", _size / sizeof(int32_t));
        }
        assert(read_space() == 0);
        assert(write_space() == _size - 1);
    }

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;
    ~RingBuffer() = default;

    /**
       Reset (empty) the RingBuffer.
       This method is NOT thread-safe, it may only be called when there are no
       readers or writers.
    */
    void reset()
    {
        _write_head = 0;
        _read_head = 0;
    }

    /// Return the number of bytes of space available for reading
    [[nodiscard]] uint32_t read_space() const
    {
        return read_space_internal(_read_head, _write_head);
    }

    /// Return the number of bytes of space available for writing
    [[nodiscard]] uint32_t write_space() const
    {
        return write_space_internal(_read_head, _write_head);
    }

    /// Return the capacity (i.e. total write space when empty)
    [[nodiscard]] uint32_t capacity() const { return _size - 1; }

    /// Read from the RingBuffer without advancing the read head
    uint32_t peek(uint32_t size, void* dst)
    {
        return peek_internal(_read_head, _write_head, size, dst);
    }

    /// Read from the RingBuffer and advance the read head
    uint32_t read(uint32_t size, void* dst)
    {
        const uint32_t r = _read_head;
        const uint32_t w = _write_head;
        if (peek_internal(r, w, size, dst)) {
            std::atomic_thread_fence(std::memory_order_acquire);
            _read_head = (r + size) & _size_mask;
            return size;
        }
        return 0;
    }

    /// Skip data in the RingBuffer (advance read head without reading)
    uint32_t skip(uint32_t size)
    {
        const uint32_t r = _read_head;
        const uint32_t w = _write_head;
        if (read_space_internal(r, w) < size) {
            return 0;
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        _read_head = (r + size) & _size_mask;
        return size;
    }

    /// Write data to the RingBuffer
    uint32_t write(uint32_t size, const void* src)
    {
        const uint32_t r = _read_head;
        const uint32_t w = _write_head;
        if (write_space_internal(r, w) < size) {
            return 0;
        }
        
        if (w + size <= _size) {
            // Contiguous write
            neon_memcpy(&_buf[w], src, size);
            std::atomic_thread_fence(std::memory_order_release);
            _write_head = (w + size) & _size_mask;
        } else {
            // Split write across boundary
            const uint32_t this_size = _size - w;
            assert(this_size < size);
            assert(w + this_size <= _size);
            
            // Use optimized copy for both parts
            neon_memcpy(&_buf[w], src, this_size);
            neon_memcpy(&_buf[0], 
                       static_cast<const uint8_t*>(src) + this_size, 
                       size - this_size);
                       
            std::atomic_thread_fence(std::memory_order_release);
            _write_head = size - this_size;
        }
        
        // Prefetch next potential write location
        prefetch_next_write();
        
        return size;
    }

private:
    static uint32_t next_power_of_two(uint32_t size)
    {
        // http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
        size--;
        size |= size >> 1U;
        size |= size >> 2U;
        size |= size >> 4U;
        size |= size >> 8U;
        size |= size >> 16U;
        size++;
        return size;
    }

    [[nodiscard]] uint32_t write_space_internal(uint32_t r, uint32_t w) const
    {
        if (r == w) {
            return _size - 1;
        }
        if (r < w) {
            return ((r - w + _size) & _size_mask) - 1;
        }
        return (r - w) - 1;
    }

    [[nodiscard]] uint32_t read_space_internal(uint32_t r, uint32_t w) const
    {
        if (r < w) {
            return w - r;
        }
        return (w - r + _size) & _size_mask;
    }

    uint32_t peek_internal(uint32_t r, uint32_t w, uint32_t size, void* dst) const
    {
        if (read_space_internal(r, w) < size) {
            return 0;
        }
        
        if (r + size <= _size) {
            // Contiguous read - use optimized copy
            neon_memcpy(dst, &_buf[r], size);
        } else {
            // Split read across boundary
            const uint32_t first_size = _size - r;
            
            // Use optimized copy for both parts
            neon_memcpy(dst, &_buf[r], first_size);
            neon_memcpy(static_cast<uint8_t*>(dst) + first_size, 
                       &_buf[0], 
                       size - first_size);
        }
        
        return size;
    }
    
    // Prefetch the next likely write location
    void prefetch_next_write() const {
#ifdef __ARM_NEON
        const uint32_t next_write = (_write_head + 64) & _size_mask;
        __builtin_prefetch(&_buf[next_write], 1, 0);
#endif
    }
    
    // Prefetch the next likely read location
    void prefetch_next_read() const {
#ifdef __ARM_NEON
        const uint32_t next_read = (_read_head + 64) & _size_mask;
        __builtin_prefetch(&_buf[next_read], 0, 0);
#endif
    }

    // NEON-optimized memcpy
    static void neon_memcpy(void* dst, const void* src, size_t size) {
#ifdef __ARM_NEON
        uint8_t* d = static_cast<uint8_t*>(dst);
        const uint8_t* s = static_cast<const uint8_t*>(src);
        
        // For small copies, use standard memcpy
        if (size < 64) {
            std::memcpy(d, s, size);
            return;
        }
        
        // Process in chunks of 16 bytes (128 bits)
        size_t i = 0;
        size_t simd_end = size - (size % 16);
        
        for (; i < simd_end; i += 16) {
            uint8x16_t data = vld1q_u8(s + i);
            vst1q_u8(d + i, data);
        }
        
        // Handle remaining bytes
        for (; i < size; ++i) {
            d[i] = s[i];
        }
#else
        // Fall back to standard memcpy if NEON not available
        std::memcpy(dst, src, size);
#endif
    }

    std::atomic<uint32_t> _write_head{0}; ///< Write index into _buf
    std::atomic<uint32_t> _read_head{0};  ///< Read index into _buf
    uint32_t _size;                       ///< Size (capacity) in bytes
    uint32_t _size_mask;                  ///< Mask for fast modulo
    std::unique_ptr<char[]> _buf;         ///< Contents
    std::shared_ptr<spdlog::logger> _logger;
};

} // namespace raul

#endif // RAUL_RINGBUFFER_HPP