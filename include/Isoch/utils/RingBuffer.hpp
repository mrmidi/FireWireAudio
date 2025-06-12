// TODO: Fix receiver to get rid of this buffer: should be removed possibly.

// Copyright 2007-2012 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RAUL_RINGBUFFER_HPP
#define RAUL_RINGBUFFER_HPP

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <algorithm>
#include <spdlog/spdlog.h>

// Include ARM NEON headers
#ifdef __ARM_NEON
#  include <arm_neon.h>
#endif

namespace raul {

/**
   A lock-free RingBuffer with partial-write support.
   Thread-safe for single producer / single consumer.
   Real-time safe on both ends.
*/
class RingBuffer
{
public:
    /**
       Create a new RingBuffer.
       @param size Size in bytes (rounded up to next power of two).
    */
    explicit RingBuffer(uint32_t size,
                        std::shared_ptr<spdlog::logger> logger = nullptr)
      : _size(next_power_of_two(size))
      , _size_mask(_size - 1)
      , _buf(static_cast<char*>(std::aligned_alloc(64, _size)), &std::free)
      , _logger(std::move(logger))
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

    /// Reset to empty. Not thread-safe.
    void reset()
    {
        _write_head.store(0, std::memory_order_relaxed);
        _read_head.store(0,  std::memory_order_relaxed);
    }

    /// Bytes available for reading.
    [[nodiscard]] uint32_t read_space() const noexcept
    {
        const uint32_t r = _read_head.load(std::memory_order_relaxed);
        const uint32_t w = _write_head.load(std::memory_order_acquire);
        return read_space_internal(r, w);
    }

    /// Bytes available for writing.
    [[nodiscard]] uint32_t write_space() const noexcept
    {
        const uint32_t r = _read_head.load(std::memory_order_acquire);
        const uint32_t w = _write_head.load(std::memory_order_relaxed);
        return write_space_internal(r, w);
    }

    /// Total capacity (write space when empty).
    [[nodiscard]] uint32_t capacity() const noexcept { return _size - 1; }

    /// Peek up to `size` bytes without advancing read head.
    uint32_t peek(uint32_t size, void* dst) const noexcept
    {
        return peek_internal(_read_head.load(std::memory_order_relaxed),
                             _write_head.load(std::memory_order_acquire),
                             size, dst);
    }

    /// Read `size` bytes and advance read head.
    uint32_t read(uint32_t size, void* dst) noexcept
    {
        const uint32_t r = _read_head.load(std::memory_order_relaxed);
        const uint32_t w = _write_head.load(std::memory_order_acquire);
        if (!peek_internal(r, w, size, dst)) {
            return 0;
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        _read_head.store((r + size) & _size_mask, std::memory_order_relaxed);
        return size;
    }

    /// Write up to `size` bytes, returns actually written.
    uint32_t write(uint32_t size, const void* src) noexcept
    {
        const uint8_t* s = static_cast<const uint8_t*>(src);
        uint32_t to_write = size;
        uint32_t total_written = 0;

        while (to_write) {
            // Load fresh indices
            uint32_t r = _read_head.load(std::memory_order_acquire);
            uint32_t w = _write_head.load(std::memory_order_relaxed);
            uint32_t space = write_space_internal(r, w);
            if (!space) break;

            // Amount we can copy this iteration
            uint32_t chunk = std::min(space, to_write);
            // But cap to contiguous region
            uint32_t cont = std::min(chunk, _size - w);

            // Copy
            neon_memcpy(&_buf.get()[w], s, cont);
            std::atomic_thread_fence(std::memory_order_release);

            // Advance write head
            uint32_t new_w = (w + cont) & _size_mask;
            _write_head.store(new_w, std::memory_order_relaxed);

            prefetch_next_write();

            // Advance pointers/counters
            s             += cont;
            to_write      -= cont;
            total_written += cont;
        }

        return total_written;
    }

private:
    static uint32_t next_power_of_two(uint32_t s)
    {
#if __cplusplus >= 202002L
        return std::bit_ceil(s);
#else
        --s; s |= s >> 1; s |= s >> 2; s |= s >> 4;
        s |= s >> 8; s |= s >> 16; return ++s;
#endif
    }

    uint32_t write_space_internal(uint32_t r, uint32_t w) const noexcept
    {
        if (r == w) {
            return _size - 1;
        }
        if (r < w) {
            return ((r + _size) - w) & _size_mask;
        }
        return (r - w) - 1;
    }

    uint32_t read_space_internal(uint32_t r, uint32_t w) const noexcept
    {
        if (r <= w) {
            return w - r;
        }
        return (w + _size - r) & _size_mask;
    }

    uint32_t peek_internal(uint32_t r, uint32_t w,
                           uint32_t size, void* dst) const noexcept
    {
        uint32_t available = read_space_internal(r, w);
        if (available < size) return 0;

        char* d = static_cast<char*>(dst);
        if (r + size <= _size) {
            neon_memcpy(d, &_buf.get()[r], size);
        } else {
            uint32_t first = _size - r;
            neon_memcpy(d, &_buf.get()[r], first);
            neon_memcpy(d + first, &_buf.get()[0], size - first);
        }
        return size;
    }

    void prefetch_next_write() const noexcept {
#ifdef __ARM_NEON
        uint32_t w = _write_head.load(std::memory_order_relaxed);
        uint32_t nxt = (w + 64) & _size_mask;
        __builtin_prefetch(&_buf.get()[nxt], 1, 0);
#endif
    }

    static void neon_memcpy(void* dst, const void* src, size_t size) noexcept {
#ifdef __ARM_NEON
        auto* d = static_cast<uint8_t*>(dst);
        auto* s = static_cast<const uint8_t*>(src);
        if (size < 128) {
            std::memcpy(d, s, size);
            return;
        }
        size_t i = 0;
        for (; i + 64 <= size; i += 64) {
            uint8x16x4_t v = vld1q_u8_x4(s + i);
            vst1q_u8_x4(d + i, v);
        }
        size_t end16 = size - ((size - i) % 16);
        for (; i < end16; i += 16) {
            vst1q_u8(d + i, vld1q_u8(s + i));
        }
        for (; i < size; ++i) {
            d[i] = s[i];
        }
#else
        std::memcpy(dst, src, size);
#endif
    }

    std::atomic<uint32_t>           _write_head{0};
    std::atomic<uint32_t>           _read_head{0};
    const uint32_t                  _size;
    const uint32_t                  _size_mask;
    std::unique_ptr<char, void(*)(void*)> _buf;
    std::shared_ptr<spdlog::logger> _logger;
};

} // namespace raul

#endif // RAUL_RINGBUFFER_HPP