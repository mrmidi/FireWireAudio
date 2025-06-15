#pragma once
#include <atomic>
#include <cstddef>
#include <cstring>
#include <cassert>

// Single‐Producer Single‐Consumer ring buffer.
// Depth must be power‐of‐two for fast modulo.
template<typename T, std::size_t Depth>
class alignas(64) SpscRing {
  static_assert((Depth & (Depth-1))==0, "Depth must be power of two");
public:
  SpscRing() : head_(0), tail_(0) {}

  // Producer: copy-one T into the ring.  Returns false if full.
  bool push(const T& item) {
    auto h = head_.load(std::memory_order_relaxed);
    auto next = (h + 1) & mask_;
    if (next == tail_.load(std::memory_order_acquire))
      return false;               // ring full
    buf_[h] = item;               // memcpy or trivial copy
    head_.store(next, std::memory_order_release);
    return true;
  }

  // Consumer: pop-one T from ring into 'item'.  Returns false if empty.
  bool pop(T& item) {
    auto t = tail_.load(std::memory_order_relaxed);
    if (t == head_.load(std::memory_order_acquire))
      return false;               // ring empty
    item = buf_[t];
    tail_.store((t + 1) & mask_, std::memory_order_release);
    return true;
  }

  // Utility
  bool empty() const { return tail_.load() == head_.load(); }
  bool full()  const { return ((head_.load()+1)&mask_) == tail_.load(); }
  size_t occupancy() const { return (head_.load() + Depth - tail_.load()) & mask_; }

private:
  static constexpr std::size_t mask_ = Depth - 1;
  T                       buf_[Depth];
  alignas(64) std::atomic<std::size_t> head_;
  alignas(64) std::atomic<std::size_t> tail_;
};