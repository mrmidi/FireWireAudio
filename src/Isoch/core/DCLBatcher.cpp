#include "Isoch/core/DCLBatcher.hpp"

namespace FWA::Isoch {

DCLBatcher::DCLBatcher(size_t maxBatchSize) {
    batchToNotify_.reserve(maxBatchSize);
}

void DCLBatcher::queueForNotification(NuDCLRef dcl) {
    if (dcl && currentIndex_ < batchToNotify_.capacity()) {
        batchToNotify_.push_back(static_cast<void*>(dcl));
        currentIndex_++;
    }
}

void DCLBatcher::flush(IOFireWireLibLocalIsochPortRef localPort) {
    if (!localPort || currentIndex_ == 0) {
        return;
    }

    // Batch notify all queued DCL updates in single kernel call
    (*localPort)->Notify(localPort,
                         kFWNuDCLModifyNotification,
                         batchToNotify_.data(),
                         currentIndex_);

    // Reset the batch for the next round
    batchToNotify_.clear();
    currentIndex_ = 0;
}

} // namespace FWA::Isoch