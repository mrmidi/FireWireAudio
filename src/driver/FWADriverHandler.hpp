#ifndef FWADRIVERHANDLER_HPP
#define FWADRIVERHANDLER_HPP

#include <aspl/Driver.hpp>
#include <aspl/Stream.hpp>
#include <aspl/ControlRequestHandler.hpp>
#include <aspl/IORequestHandler.hpp>
#include <memory>

class FWADriverHandler : public aspl::ControlRequestHandler, public aspl::IORequestHandler {
public:
    OSStatus OnStartIO() override;
    void OnStopIO() override;
    void OnWriteMixedOutput(const std::shared_ptr<aspl::Stream>& stream,
                            double zeroTimestamp,
                            double timestamp,
                            const void* buffer,
                            unsigned int bufferByteSize) override;
private:
    // Placeholder for XPC or other IPC resources
    // int socket_ = -1;
    // xpc_connection_t xpcConnection = nullptr;
};

#endif // FWADRIVERHANDLER_HPP
