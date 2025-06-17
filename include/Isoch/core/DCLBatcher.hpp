#pragma once
#include <vector>
#include <IOKit/firewire/IOFireWireLibIsoch.h>
#include "Isoch/core/IsochTransmitDCLManager.hpp"

namespace FWA::Isoch {

/**
 * @brief Batches DCL notification updates to reduce kernel transitions
 * 
 * This class collects pointers to modified DCL references and flushes them
 * to the hardware in a single batch call, reducing overhead from ~8000 
 * individual calls per second to ~125 batched calls per second.
 */
class DCLBatcher {
public:
    /**
     * @brief Construct DCLBatcher with pre-allocated capacity
     * @param maxBatchSize Maximum number of DCL updates per batch
     */
    explicit DCLBatcher(size_t maxBatchSize);

    /**
     * @brief Add a DCL reference to the batch for later notification
     * @param dcl The DCL reference to queue for notification
     */
    void queueForNotification(NuDCLRef dcl);

    /**
     * @brief Flush all queued DCLs to hardware via single Notify call
     * @param localPort The local isochronous port for notification
     */
    void flush(IOFireWireLibLocalIsochPortRef localPort);

    /**
     * @brief Get current number of queued DCL updates
     * @return Number of DCL updates currently queued
     */
    size_t size() const { return currentIndex_; }

    /**
     * @brief Check if batch is empty
     * @return true if no DCL updates are queued
     */
    bool empty() const { return currentIndex_ == 0; }

private:
    std::vector<void*> batchToNotify_; ///< Use void* as required by IOKit's Notify
    size_t currentIndex_{0};           ///< Current number of queued updates
};

} // namespace FWA::Isoch