// src/driver/DriverXPCManager.hpp
#ifndef DRIVERXPCMANAGER_HPP
#define DRIVERXPCMANAGER_HPP

#include <memory>
#include <string>
#include <atomic>
//#include "shared/xpc/FWADaemonControlProtocol.h"

#ifdef __OBJC__
@class NSXPCConnection;
@protocol FWADaemonControlProtocol;
#else
class NSXPCConnection;
typedef struct objc_object FWADaemonControlProtocol;
#endif

class DriverXPCManager {
public:
    static DriverXPCManager& instance();
    bool connect();
    void disconnect();
    bool isConnected() const;
    void setPresenceStatus(bool isPresent);
private:
    DriverXPCManager();
    ~DriverXPCManager();
    DriverXPCManager(const DriverXPCManager&) = delete;
    DriverXPCManager& operator=(const DriverXPCManager&) = delete;
    NSXPCConnection* xpcConnection_ = nullptr;
    void* daemonProxy_ = nullptr; // Use void* for C++ compatibility
    std::atomic<bool> isConnected_{false};
    const std::string daemonServiceName_ = "net.mrmidi.FWADaemon";
    const std::string clientID_ = "FWADriverASPL";
    void handleDaemonDisconnect(const char* reason);
};

#endif // DRIVERXPCMANAGER_HPP
