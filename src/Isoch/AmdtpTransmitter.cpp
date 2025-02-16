#include "FWA/Isoch/AmdtpTransmitter.hpp"
#include "FWA/Isoch/AmdtpHelpers.hpp"  // Use our shared helpers for time management, logging, etc.
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <CoreFoundation/CoreFoundation.h>
#include <strings.h>

namespace FWA {
namespace Isoch {

    // --- Time Helpers have been factored out to AmdtpHelpers.
    // In handleDCLCallback, we now use:
    //   AbsoluteTime currentUpTime = AmdtpHelpers::GetUpTime();
    //   Nanoseconds currentUpTimeInNano = AmdtpHelpers::AbsoluteTimeToNanoseconds(currentUpTime);
    // and remove the local getUpTime/absoluteTimeToNanoseconds functions.

    // --- Remote Port Helpers (static member functions) ---
    IOReturn AmdtpTransmitter::remotePortGetSupportedHelper(IOFireWireLibIsochPortRef interface,
                                                            IOFWSpeed* outMaxSpeed,
                                                            UInt64* outChanSupported)
    {
        AmdtpTransmitter* transmitter = static_cast<AmdtpTransmitter*>((*interface)->GetRefCon(interface));
        return transmitter->remotePortGetSupported(interface, outMaxSpeed, outChanSupported);
    }

    IOReturn AmdtpTransmitter::remotePortAllocatePortHelper(IOFireWireLibIsochPortRef interface,
                                                            IOFWSpeed maxSpeed,
                                                            UInt32 channel)
    {
        AmdtpTransmitter* transmitter = static_cast<AmdtpTransmitter*>((*interface)->GetRefCon(interface));
        return transmitter->remotePortAllocatePort(interface, maxSpeed, channel);
    }

    IOReturn AmdtpTransmitter::remotePortReleasePortHelper(IOFireWireLibIsochPortRef interface)
    {
        AmdtpTransmitter* transmitter = static_cast<AmdtpTransmitter*>((*interface)->GetRefCon(interface));
        return transmitter->remotePortReleasePort(interface);
    }

    IOReturn AmdtpTransmitter::remotePortStartHelper(IOFireWireLibIsochPortRef interface)
    {
        AmdtpTransmitter* transmitter = static_cast<AmdtpTransmitter*>((*interface)->GetRefCon(interface));
        return transmitter->remotePortStart(interface);
    }

    IOReturn AmdtpTransmitter::remotePortStopHelper(IOFireWireLibIsochPortRef interface)
    {
        AmdtpTransmitter* transmitter = static_cast<AmdtpTransmitter*>((*interface)->GetRefCon(interface));
        return transmitter->remotePortStop(interface);
    }

    // --- C-style Callback Wrappers ---
    IOReturn AmdtpTransmitter::finalizeCallbackWrapper(void* refcon) {
        AmdtpTransmitter* transmitter = static_cast<AmdtpTransmitter*>(refcon);
        transmitter->handleFinalize();
        return kIOReturnSuccess;
    }

    void AmdtpTransmitter::dclCallbackWrapper(void* refcon, NuDCLRef dcl) {
        AmdtpTransmitter* transmitter = static_cast<AmdtpTransmitter*>(refcon);
        transmitter->handleDCLCallback();
    }

    void AmdtpTransmitter::dclOverrunCallbackWrapper(void* refcon, NuDCLRef dcl) {
        AmdtpTransmitter* transmitter = static_cast<AmdtpTransmitter*>(refcon);
        transmitter->handleOverrunCallback();
    }

    // --- Constructor and Destructor ---
    AmdtpTransmitter::AmdtpTransmitter(std::shared_ptr<spdlog::logger> logger,
                                       IOFireWireLibNubRef nubInterface,
                                       uint32_t cyclesPerSegment,
                                       uint32_t numSegments,
                                       uint32_t clientBufferSize,
                                       uint32_t sampleRate,
                                       uint32_t numChannels,
                                       bool doIRMAlloc,
                                       uint32_t irmPacketSize,
                                       uint32_t cycleMatchBits)
        : logger_(std::move(logger))
        , cyclesPerSegment_(cyclesPerSegment)
        , numSegments_(numSegments)
        , clientBufferSize_(clientBufferSize)
        , sampleRate_(sampleRate)
        , numChannels_(numChannels)
        , doIRM_(doIRMAlloc)
        , irmPacketSize_(irmPacketSize)
        , cycleMatchBits_(std::min(cycleMatchBits, 20u))
    {
        for (unsigned int i = 0; i < cycleMatchBits_; i++) {
            startupCycleMatchMask_ |= (1U << i);
        }
        uint32_t totalCycles = cyclesPerSegment_ * numSegments_;
        bufferManager_ = std::make_unique<IsochBufferManager>(logger_, totalCycles, clientBufferSize_);
        cipHandler_ = std::make_unique<CIPHeaderHandler>(logger_);
        if (logger_) {
            logger_->info("AmdtpTransmitter created:");
            logger_->info("  Cycles per segment: {}", cyclesPerSegment_);
            logger_->info("  Number of segments: {}", numSegments_);
            logger_->info("  Client buffer size: {}", clientBufferSize_);
            logger_->info("  Sample rate: {}", sampleRate_);
            logger_->info("  Channels: {}", numChannels_);
            logger_->info("  IRM allocation: {}", doIRM_);
            logger_->info("  IRM packet size: {}", irmPacketSize_);
            logger_->info("  Cycle match mask: 0x{:08X}", startupCycleMatchMask_);
        }
    }

    AmdtpTransmitter::~AmdtpTransmitter() {
        if (transportPlaying_) {
            stopTransmit();
        }
        cleanup();
    }

    // --- Resource Cleanup ---
    void AmdtpTransmitter::cleanup() noexcept {
        if (nodeNubInterface_ && runLoopRef_) {
            (*nodeNubInterface_)->RemoveIsochCallbackDispatcherFromRunLoop(nodeNubInterface_);
            (*nodeNubInterface_)->RemoveCallbackDispatcherFromRunLoop(nodeNubInterface_);
        }
        if (isochChannel_) {
            (*isochChannel_)->Release(isochChannel_);
            isochChannel_ = nullptr;
        }
        if (localIsocPort_) {
            (*localIsocPort_)->Release(localIsocPort_);
            localIsocPort_ = nullptr;
        }
        if (remoteIsocPort_) {
            (*remoteIsocPort_)->Release(remoteIsocPort_);
            remoteIsocPort_ = nullptr;
        }
        if (nuDCLPool_) {
            (*nuDCLPool_)->Release(nuDCLPool_);
            nuDCLPool_ = nullptr;
        }
        if (nodeNubInterface_) {
            (*nodeNubInterface_)->Release(nodeNubInterface_);
            nodeNubInterface_ = nullptr;
        }
        if (dclProgram_) {
            for (auto& bag : dclProgram_->segmentUpdateBags) {
                if (bag) {
                    CFRelease(bag);
                }
            }
            dclProgram_.reset();
        }
    }

    // --- Core Operations ---
    std::expected<void, FWA::IOKitError> AmdtpTransmitter::setupTransmitter() {
        if (auto result = initializeFireWireInterface(); !result) {
            return result;
        }
        if (auto result = bufferManager_->allocateBuffers(); !result) {
            return result;
        }
        if (auto result = createNuDCLPool(); !result) {
            return result;
        }
        if (auto result = createRemoteIsochPort(); !result) {
            return result;
        }
        if (auto result = createDCLProgram(); !result) {
            return result;
        }
        if (auto result = createLocalIsochPort(); !result) {
            return result;
        }
        if (auto result = createIsochChannel(); !result) {
            return result;
        }
        if (auto result = setupChannelConnections(); !result) {
            return result;
        }
        return {};
    }

    std::expected<void, FWA::IOKitError> AmdtpTransmitter::startTransmit() {
        std::lock_guard<std::mutex> lock(transportMutex_);
        if (transportPlaying_) {
            return std::unexpected(FWA::IOKitError::ExclusiveAccess);
        }
        currentSegment_ = 0;
        firstDCLCallbackOccurred_ = false;
        finalizeCallbackCalled_ = false;

        UInt32 busTime = 0, cycleTime = 0;
        (*nodeNubInterface_)->GetBusCycleTime(nodeNubInterface_, &busTime, &cycleTime);
        UInt32 cycleTimeVal = cycleTime;

        if (auto result = cipHandler_->initialize(cycleTimeVal); !result) {
            return result;
        }
        if (messageCallback_) {
            notifyCallback(AmdtpMessageType::DataPull);
        }
        IOReturn result = (*isochChannel_)->AllocateChannel(isochChannel_);
        if (result != kIOReturnSuccess) {
            return std::unexpected(static_cast<FWA::IOKitError>(result));
        }
        result = (*isochChannel_)->Start(isochChannel_);
        if (result != kIOReturnSuccess) {
            (*isochChannel_)->ReleaseChannel(isochChannel_);
            return std::unexpected(static_cast<FWA::IOKitError>(result));
        }
        transportPlaying_ = true;
        return {};
    }

    std::expected<void, FWA::IOKitError> AmdtpTransmitter::stopTransmit() {
        std::lock_guard<std::mutex> lock(transportMutex_);
        if (!transportPlaying_) {
            return {};
        }
        IOReturn result = (*isochChannel_)->Stop(isochChannel_);
        if (result == kIOReturnSuccess) {
            while (!finalizeCallbackCalled_) {
                CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
            }
            result = (*isochChannel_)->ReleaseChannel(isochChannel_);
        }
        transportPlaying_ = false;
        if (result != kIOReturnSuccess) {
            return std::unexpected(static_cast<FWA::IOKitError>(result));
        }
        return {};
    }

    void AmdtpTransmitter::notifyCallback(AmdtpMessageType type, uint32_t param1, uint32_t param2) const {
        if (messageCallback_) {
            messageCallback_(type, param1, param2);
        }
    }

    uint8_t* AmdtpTransmitter::getClientBuffer() const noexcept {
        return bufferManager_ ? bufferManager_->getClientBuffer() : nullptr;
    }

    uint32_t AmdtpTransmitter::getClientBufferSize() const noexcept {
        return bufferManager_ ? bufferManager_->getClientBufferSize() : 0;
    }

    std::expected<void, FWA::IOKitError> AmdtpTransmitter::setTransmitSpeed(IOFWSpeed speed) {
        if (transportPlaying_) {
            return std::unexpected(FWA::IOKitError::Busy);
        }
        transmitSpeed_ = speed;
        return {};
    }

    std::expected<void, FWA::IOKitError> AmdtpTransmitter::setTransmitChannel(unsigned int channel) {
        if (transportPlaying_) {
            return std::unexpected(FWA::IOKitError::Busy);
        }
        transmitChannel_ = channel;
        return {};
    }

    // --- Setup Helpers ---
    std::expected<void, FWA::IOKitError> AmdtpTransmitter::initializeFireWireInterface() {
        IOReturn result;
        IOFireWireLibNubRef newNubInterface;
        if (!nodeNubInterface_) {
            result = GetFireWireLocalNodeInterface(&nodeNubInterface_);
        } else {
            result = GetFireWireDeviceInterfaceFromExistingInterface(nodeNubInterface_, &newNubInterface);
            if (result == kIOReturnSuccess) {
                nodeNubInterface_ = newNubInterface;
            }
        }
        if (result != kIOReturnSuccess) {
            logger_->error("Failed to initialize FireWire interface: {}", result);
            return std::unexpected(static_cast<FWA::IOKitError>(result));
        }
        runLoopRef_ = CFRunLoopGetCurrent();
        result = (*nodeNubInterface_)->AddCallbackDispatcherToRunLoop(nodeNubInterface_, runLoopRef_);
        if (result == kIOReturnSuccess) {
            result = (*nodeNubInterface_)->AddIsochCallbackDispatcherToRunLoop(nodeNubInterface_, runLoopRef_);
        }
        if (result != kIOReturnSuccess) {
            logger_->error("Failed to setup run loop dispatchers: {}", result);
            return std::unexpected(static_cast<FWA::IOKitError>(result));
        }
        return {};
    }

    std::expected<void, FWA::IOKitError> AmdtpTransmitter::createNuDCLPool() {
        nuDCLPool_ = (*nodeNubInterface_)->CreateNuDCLPool(nodeNubInterface_, 0, CFUUIDGetUUIDBytes(kIOFireWireNuDCLPoolInterfaceID));
        if (!nuDCLPool_) {
            logger_->error("Failed to create NuDCL pool");
            return std::unexpected(FWA::IOKitError::NoMemory);
        }
        return {};
    }

    std::expected<void, FWA::IOKitError> AmdtpTransmitter::createDCLProgram() {
        dclProgram_ = std::make_unique<DCLProgram>();
        uint32_t totalObjects = cyclesPerSegment_ * numSegments_;
        dclProgram_->programDCLs.resize(totalObjects);
        dclProgram_->segmentUpdateBags.resize(numSegments_);
        for (uint32_t i = 0; i < numSegments_; i++) {
            dclProgram_->segmentUpdateBags[i] = CFSetCreateMutable(kCFAllocatorDefault, 0, nullptr);
            if (!dclProgram_->segmentUpdateBags[i]) {
                return std::unexpected(FWA::IOKitError::NoMemory);
            }
        }
        cycleInfos_.resize(totalObjects);
        for (uint32_t seg = 0; seg < numSegments_; seg++) {
            for (uint32_t cycle = 0; cycle < cyclesPerSegment_; cycle++) {
                uint32_t index = (seg * cyclesPerSegment_) + cycle;
                IOVirtualRange range;
                range.address = 0;
                range.length = 0;
                auto dcl = (*nuDCLPool_)->AllocateSendPacket(nuDCLPool_, nullptr, 1, &range);
                if (!dcl) {
                    logger_->error("Failed to allocate DCL at index {}", index);
                    return std::unexpected(FWA::IOKitError::NoMemory);
                }
                (*nuDCLPool_)->SetDCLFlags(dcl, kNuDCLDynamic | kNuDCLUpdateBeforeCallback);
                (*nuDCLPool_)->SetDCLRefcon(dcl, reinterpret_cast<void*>(this));
                (*nuDCLPool_)->SetDCLCallback(dcl, dclCallbackWrapper);
                dclProgram_->programDCLs[index] = dcl;
                CFSetAddValue(dclProgram_->segmentUpdateBags[seg], dcl);
            }
        }
        IOVirtualRange overrunRange;
        overrunRange.address = 0;
        overrunRange.length = 1;
        dclProgram_->overrunDCL = (*nuDCLPool_)->AllocateSendPacket(nuDCLPool_, nullptr, 1, &overrunRange);
        if (!dclProgram_->overrunDCL) {
            logger_->error("Failed to allocate overrun DCL");
            return std::unexpected(FWA::IOKitError::NoMemory);
        }
        (*nuDCLPool_)->SetDCLFlags(dclProgram_->overrunDCL, kNuDCLDynamic | kNuDCLUpdateBeforeCallback);
        (*nuDCLPool_)->SetDCLRefcon(dclProgram_->overrunDCL, reinterpret_cast<void*>(this));
        (*nuDCLPool_)->SetDCLCallback(dclProgram_->overrunDCL, dclOverrunCallbackWrapper);
        logger_->debug("DCL program created with {} cycles", totalObjects);
        return {};
    }

    std::expected<void, FWA::IOKitError> AmdtpTransmitter::createRemoteIsochPort() {
        remoteIsocPort_ = (*nodeNubInterface_)->CreateRemoteIsochPort(nodeNubInterface_, false, CFUUIDGetUUIDBytes(kIOFireWireRemoteIsochPortInterfaceID));
        if (!remoteIsocPort_) {
            logger_->error("Failed to create remote isoch port");
            return std::unexpected(FWA::IOKitError::NoMemory);
        }
        (*remoteIsocPort_)->SetRefCon((IOFireWireLibIsochPortRef)remoteIsocPort_, this);
        (*remoteIsocPort_)->SetGetSupportedHandler(remoteIsocPort_, remotePortGetSupportedHelper);
        (*remoteIsocPort_)->SetAllocatePortHandler(remoteIsocPort_, remotePortAllocatePortHelper);
        (*remoteIsocPort_)->SetReleasePortHandler(remoteIsocPort_, remotePortReleasePortHelper);
        (*remoteIsocPort_)->SetStartHandler(remoteIsocPort_, remotePortStartHelper);
        (*remoteIsocPort_)->SetStopHandler(remoteIsocPort_, remotePortStopHelper);
        return {};
    }

    std::expected<void, FWA::IOKitError> AmdtpTransmitter::createLocalIsochPort() {
        if (!nuDCLPool_ || !bufferManager_) {
            return std::unexpected(FWA::IOKitError::NotReady);
        }
        auto bufferRange = bufferManager_->getBufferRange();
        localIsocPort_ = (*nodeNubInterface_)->CreateLocalIsochPort(nodeNubInterface_, true,
                                  (*nuDCLPool_)->GetProgram(nuDCLPool_),
                                  (startupCycleMatchMask_ == 0) ? 0 : kFWDCLCycleEvent,
                                  0,
                                  startupCycleMatchMask_,
                                  nullptr,
                                  0,
                                  &bufferRange,
                                  1,
                                  CFUUIDGetUUIDBytes(kIOFireWireLocalIsochPortInterfaceID));
        if (!localIsocPort_) {
            logger_->error("Failed to create local isoch port");
            return std::unexpected(FWA::IOKitError::NoMemory);
        }
        (*localIsocPort_)->SetRefCon((IOFireWireLibIsochPortRef)localIsocPort_, this);
        (*localIsocPort_)->SetFinalizeCallback(localIsocPort_, finalizeCallbackWrapper);
        return {};
    }

    std::expected<void, FWA::IOKitError> AmdtpTransmitter::createIsochChannel() {
        isochChannel_ = (*nodeNubInterface_)->CreateIsochChannel(nodeNubInterface_, doIRM_, irmPacketSize_,
                                                               kFWSpeedMaximum,
                                                               CFUUIDGetUUIDBytes(kIOFireWireIsochChannelInterfaceID));
        if (!isochChannel_) {
            logger_->error("Failed to create isoch channel");
            return std::unexpected(FWA::IOKitError::NoMemory);
        }
        return {};
    }

    std::expected<void, FWA::IOKitError> AmdtpTransmitter::setupChannelConnections() {
        if (!isochChannel_ || !remoteIsocPort_ || !localIsocPort_) {
            return std::unexpected(FWA::IOKitError::NotReady);
        }
        IOReturn result = (*isochChannel_)->AddListener(isochChannel_,
                                  reinterpret_cast<IOFireWireLibIsochPortRef>(remoteIsocPort_));
        if (result == kIOReturnSuccess) {
            result = (*isochChannel_)->SetTalker(isochChannel_,
                                  reinterpret_cast<IOFireWireLibIsochPortRef>(localIsocPort_));
        }
        if (result != kIOReturnSuccess) {
            logger_->error("Failed to setup channel connections: {}", result);
            return std::unexpected(static_cast<FWA::IOKitError>(result));
        }
        return {};
    }

    std::expected<void, FWA::IOKitError> AmdtpTransmitter::fillCycleBuffer(
        NuDCLSendPacketRef dcl, uint16_t nodeID, uint32_t segment, uint32_t cycle) {
        
        auto* cipHeaders = bufferManager_->getCIPHeaders();
        if (!cipHeaders) {
            return std::unexpected(FWA::IOKitError::NotReady);
        }
        auto paramsResult = cipHandler_->calculatePacketParams(segment, cycle);
        if (!paramsResult) {
            return std::unexpected(paramsResult.error());
        }
        auto& params = paramsResult.value();
        uint32_t index = (segment * cyclesPerSegment_) + cycle;
        auto& cycleInfo = cycleInfos_[index];
        auto* header = reinterpret_cast<CIPHeader*>(&cipHeaders[index * 2]);
        cipHandler_->updateCIPHeader(header, nodeID, params);
        cycleInfo.ranges[0].address = reinterpret_cast<IOVirtualAddress>(&cipHeaders[index * 2]);
        cycleInfo.ranges[0].length = 8;
        cycleInfo.numRanges = 1;
        if (!params.isNoData) {
            auto* clientBuffer = bufferManager_->getClientBuffer();
            if (clientBuffer) {
                cycleInfo.ranges[1].address = reinterpret_cast<IOVirtualAddress>(clientBuffer + (index * numChannels_ * 4));
                cycleInfo.ranges[1].length = numChannels_ * 4;
                cycleInfo.numRanges = 2;
            }
        }
        // Note: SetDCLRanges expects the count first, then pointer to the ranges array.
        (*nuDCLPool_)->SetDCLRanges(dcl, cycleInfo.numRanges, cycleInfo.ranges.data());
        return {};
    }

    void AmdtpTransmitter::handleDCLCallback() {
        if (!transportPlaying_) return;

        uint16_t nodeID;
        uint32_t generation;
        do {
            (*nodeNubInterface_)->GetBusGeneration(nodeNubInterface_, &generation);
        } while ((*nodeNubInterface_)->GetLocalNodeIDWithGeneration(nodeNubInterface_, generation, &nodeID) != kIOReturnSuccess);

        UInt32 busTime = 0, cycleTime = 0;
        (*nodeNubInterface_)->GetBusCycleTime(nodeNubInterface_, &busTime, &cycleTime);
        uint32_t actualTimeStampCycle = cycleTime & 0x1FFF;

        if (!firstDCLCallbackOccurred_) {
            firstDCLCallbackOccurred_ = true;
            cipHandler_->setFirstCallbackOccurred(true);
            expectedTimeStampCycle_ = actualTimeStampCycle;
        } else {
            if (actualTimeStampCycle != expectedTimeStampCycle_) {
                int lostCycles = static_cast<int>(actualTimeStampCycle) - static_cast<int>(expectedTimeStampCycle_);
                if (lostCycles < 0)
                    lostCycles += 64000;
                notifyCallback(AmdtpMessageType::TimeStampAdjust, actualTimeStampCycle, expectedTimeStampCycle_);
                expectedTimeStampCycle_ = actualTimeStampCycle;
            }
        }

        // Use the shared helpers for time management.
        AbsoluteTime currentUpTime = AmdtpHelpers::GetUpTime();
        Nanoseconds currentUpTimeInNano = AmdtpHelpers::AbsoluteTimeToNanoseconds(currentUpTime);
        currentUpTimeInNanoSecondsU64_ = ((uint64_t)currentUpTimeInNano.hi << 32) | currentUpTimeInNano.lo;

        uint32_t offset = cycleTime & 0x00000FFF;
        uint64_t offsetNs = static_cast<uint64_t>(offset * 41);
        currentUpTimeInNanoSecondsU64_ -= offsetNs;

        for (uint32_t cycle = 0; cycle < cyclesPerSegment_; cycle++) {
            uint32_t index = (currentSegment_ * cyclesPerSegment_) + cycle;
            if (auto result = fillCycleBuffer(dclProgram_->programDCLs[index], nodeID, currentSegment_, cycle); !result) {
                logger_->error("Failed to fill cycle buffer for segment {} cycle {}: {}",
                               currentSegment_, cycle, static_cast<int>(result.error()));
                return;
            }
            if (cycle < cyclesPerSegment_ - 1) {
                expectedTimeStampCycle_ = (expectedTimeStampCycle_ + 1) % 64000;
            }
        }

        NuDCLSendPacketRef pLastSegEndDCL = nullptr;
        if (currentSegment_ == 0) {
            pLastSegEndDCL = dclProgram_->programDCLs[((numSegments_ - 1) * cyclesPerSegment_) + (cyclesPerSegment_ - 1)];
        } else {
            pLastSegEndDCL = dclProgram_->programDCLs[((currentSegment_ - 1) * cyclesPerSegment_) + (cyclesPerSegment_ - 1)];
        }
        (*nuDCLPool_)->SetDCLBranch(pLastSegEndDCL, dclProgram_->programDCLs[currentSegment_ * cyclesPerSegment_]);

        uint32_t numDCLsNotified = 0;
        uint32_t totalDCLsToNotify = cyclesPerSegment_;
        const uint32_t kMaxNuDCLsPerNotify = 10;
        while (numDCLsNotified < totalDCLsToNotify) {
            uint32_t numDCLsForThisNotify = ((totalDCLsToNotify - numDCLsNotified) > kMaxNuDCLsPerNotify)
                                            ? kMaxNuDCLsPerNotify : (totalDCLsToNotify - numDCLsNotified);
            IOReturn notifyResult = (*localIsocPort_)->Notify(localIsocPort_,
                kFWNuDCLModifyNotification,
                (void**)&dclProgram_->programDCLs[(currentSegment_ * cyclesPerSegment_) + numDCLsNotified],
                numDCLsForThisNotify);
            if (notifyResult != kIOReturnSuccess) {
                logger_->error("Failed to notify DCL modify for segment {}: error {}", currentSegment_, notifyResult);
            }
            numDCLsNotified += numDCLsForThisNotify;
        }
        IOReturn notifyResult = (*localIsocPort_)->Notify(localIsocPort_,
                                                           kFWNuDCLModifyJumpNotification,
                                                           (void**)&pLastSegEndDCL,
                                                           1);
        if (notifyResult != kIOReturnSuccess) {
            logger_->error("Failed to notify DCL jump for segment {}: error {}", currentSegment_, notifyResult);
        }
        currentSegment_ = (currentSegment_ + 1) % numSegments_;
        expectedTimeStampCycle_ = (expectedTimeStampCycle_ + 1) % 64000;
    }

    void AmdtpTransmitter::handleOverrunCallback() {
        if (!transportPlaying_) return;
        logger_->warn("DCL overrun detected");
        notifyCallback(AmdtpMessageType::DCLOverrunAutoRestartFailed);
    }

    void AmdtpTransmitter::handleFinalize() {
        finalizeCallbackCalled_ = true;
        if (finalizeCallback_) {
            finalizeCallback_();
        }
    }

    // --- Remote Port Handlers (instance methods) ---
    IOReturn AmdtpTransmitter::remotePortGetSupported(IOFireWireLibIsochPortRef interface,
                                                      IOFWSpeed* outMaxSpeed,
                                                      uint64_t* outChanSupported) {
        *outMaxSpeed = transmitSpeed_;
        if (transmitChannel_ == 0xFFFFFFFF) {
            *outChanSupported = 0xFFFFFFFFFFFFFFFFULL;
        } else {
            *outChanSupported = 1ULL << transmitChannel_;
        }
        return kIOReturnSuccess;
    }

    IOReturn AmdtpTransmitter::remotePortAllocatePort(IOFireWireLibIsochPortRef interface,
                                                      IOFWSpeed maxSpeed,
                                                      uint32_t channel) {
        notifyCallback(AmdtpMessageType::AllocateIsochPort, maxSpeed, channel);
        return kIOReturnSuccess;
    }

    IOReturn AmdtpTransmitter::remotePortReleasePort(IOFireWireLibIsochPortRef interface) {
        notifyCallback(AmdtpMessageType::ReleaseIsochPort);
        return kIOReturnSuccess;
    }

    IOReturn AmdtpTransmitter::remotePortStart(IOFireWireLibIsochPortRef interface) {
        return kIOReturnSuccess;
    }

    IOReturn AmdtpTransmitter::remotePortStop(IOFireWireLibIsochPortRef interface) {
        return kIOReturnSuccess;
    }

} // namespace Isoch
} // namespace FWA
