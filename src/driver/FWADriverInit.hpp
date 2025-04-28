#ifndef FWADRIVERINIT_HPP
#define FWADRIVERINIT_HPP

#include <aspl/Driver.hpp>
#include <aspl/DriverRequestHandler.hpp>
#include <memory>
#include <os/log.h>
#include "FWADriverHandler.hpp" // Include the handler header

class FWADriverInit : public aspl::DriverRequestHandler {
public:
    // Constructor now accepts handler
    FWADriverInit(std::shared_ptr<FWADriverHandler> ioHandler);
    OSStatus OnInitialize() override;
    // void OnFinalize() override;
private:
    std::shared_ptr<FWADriverHandler> ioHandler_;
};

#endif // FWADRIVERINIT_HPP
