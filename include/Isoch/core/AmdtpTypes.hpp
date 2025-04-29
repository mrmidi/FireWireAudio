#pragma once

namespace FWA::Isoch {

enum AmdtpMessageType {
    kAmdtpReceiverAllocateIsochPort = 0x1000,
    kAmdtpReceiverReleaseIsochPort,
    kAmdtpReceiverStarted,
    kAmdtpReceiverStopped,
    kAmdtpReceiverError
};

} // namespace FWA::Isoch