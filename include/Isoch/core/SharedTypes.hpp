#pragma once
#include <cstdint>
#include <functional>

namespace FWA {
namespace Isoch {

// Common callback types shared between transmitter and receiver
// Maybe refactor back to C-style callbacks?
using PacketCallbackConst = std::function<void(const uint8_t* data, size_t size)>;
using PacketCallbackMutable = std::function<void(uint8_t* data, size_t size)>;
using MessageCallback = std::function<void(uint32_t msg, uint32_t param1, uint32_t param2)>;

} // namespace Isoch
} // namespace FWA