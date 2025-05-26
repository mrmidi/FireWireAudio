// // ShmIsochBridge.hpp
// #pragma once

// #include <shared/SharedMemoryStructures.hpp>
// #include <Isoch/interfaces/ITransmitPacketProvider.hpp>
// #include <atomic>
// #include <thread>
// #include <array>
// #include <cstddef>

// class ShmIsochBridge {
// public:
//     static ShmIsochBridge& instance();

//     /// Start forwarding SHM chunks into the Isoch provider.
//     void start(FWA::Isoch::ITransmitPacketProvider* provider);
//     /// Stop the background worker.
//     void stop();
//     /// Enqueue one shared‐memory chunk (zero copy).
//     void enqueue(const RTShmRing::AudioChunk_POD& chunk);
//     /// True if worker is running.
//     bool isRunning() const noexcept;

// private:
//     ShmIsochBridge() = default;
//     ~ShmIsochBridge();

//     void worker();

//     struct QueueItem {
//         const std::byte* ptr;
//         uint32_t         bytes;
//     };

//     static constexpr size_t kQCap = 128;  // ring of 128 chunks
//     std::array<QueueItem, kQCap> _q;
//     std::atomic<size_t>          _writeIdx{0}, _readIdx{0};

//     std::atomic<bool>            _running{false};
//     std::thread                  _thread;
//     FWA::Isoch::ITransmitPacketProvider* _provider{nullptr};
// };