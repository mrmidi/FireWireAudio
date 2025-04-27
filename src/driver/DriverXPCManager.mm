#import "DriverXPCManager.hpp"
#import <Foundation/Foundation.h>
#import <os/log.h>
#import "shared/xpc/FWADaemonControlProtocol.h"

constexpr const char* LogPrefix = "FWADriverASPL: ";

DriverXPCManager& DriverXPCManager::instance() {
    static DriverXPCManager singletonInstance;
    return singletonInstance;
}

DriverXPCManager::DriverXPCManager() {
    os_log_info(OS_LOG_DEFAULT, "%sDriverXPCManager: Singleton created.", LogPrefix);
}

DriverXPCManager::~DriverXPCManager() {
    os_log_info(OS_LOG_DEFAULT, "%sDriverXPCManager: Singleton destroyed.", LogPrefix);
    disconnect();
}

bool DriverXPCManager::connect() {
    if (isConnected_.load()) {
        os_log_info(OS_LOG_DEFAULT, "%sDriverXPCManager: Already connected.", LogPrefix);
        return true;
    }
    if (xpcConnection_ != nullptr) {
         os_log_info(OS_LOG_DEFAULT, "%sDriverXPCManager: Connection attempt already in progress or failed.", LogPrefix);
         return false;
    }
    os_log_info(OS_LOG_DEFAULT, "%sDriverXPCManager: Attempting to connect to daemon '%s'...", LogPrefix, daemonServiceName_.c_str());
    @try {
        xpcConnection_ = [[NSXPCConnection alloc] initWithMachServiceName:@(daemonServiceName_.c_str())
                                                                  options:NSXPCConnectionPrivileged];
        if (!xpcConnection_) {
             os_log_error(OS_LOG_DEFAULT, "%sDriverXPCManager: Failed to create NSXPCConnection object.", LogPrefix);
             return false;
        }
        xpcConnection_.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(FWADaemonControlProtocol)];
        __weak DriverXPCManager* weakSelf = this;
        xpcConnection_.interruptionHandler = ^{
            os_log_error(OS_LOG_DEFAULT, "%sDriverXPCManager: Daemon connection interrupted.", LogPrefix);
            if (weakSelf) {
                weakSelf->handleDaemonDisconnect("interrupted");
            }
        };
        xpcConnection_.invalidationHandler = ^{
            os_log_error(OS_LOG_DEFAULT, "%sDriverXPCManager: Daemon connection invalidated.", LogPrefix);
             if (weakSelf) {
                weakSelf->handleDaemonDisconnect("invalidated");
            }
        };
        [xpcConnection_ resume];
        daemonProxy_ = [xpcConnection_ remoteObjectProxyWithErrorHandler:^(NSError * _Nonnull error) {
            os_log_error(OS_LOG_DEFAULT, "%sDriverXPCManager: XPC proxy error: %{public}@", LogPrefix, error);
             if (weakSelf) {
                weakSelf->handleDaemonDisconnect("proxy error");
            }
        }];
        if (!daemonProxy_) {
            os_log_error(OS_LOG_DEFAULT, "%sDriverXPCManager: Failed to get remote object proxy.", LogPrefix);
            [xpcConnection_ invalidate];
            xpcConnection_ = nullptr;
            return false;
        }
        isConnected_.store(true);
        os_log_info(OS_LOG_DEFAULT, "%sDriverXPCManager: XPC connection established and proxy obtained.", LogPrefix);
        return true;
    } @catch (NSException *exception) {
        os_log_error(OS_LOG_DEFAULT, "%sDriverXPCManager: Exception during XPC connect: %{public}@ - %{public}@", LogPrefix, exception.name, exception.reason);
        if (xpcConnection_) {
            [xpcConnection_ invalidate];
            xpcConnection_ = nullptr;
        }
        daemonProxy_ = nullptr;
        isConnected_.store(false);
        return false;
    }
}

void DriverXPCManager::disconnect() {
    if (!isConnected_.load() && xpcConnection_ == nullptr) {
        os_log_info(OS_LOG_DEFAULT, "%sDriverXPCManager: Already disconnected.", LogPrefix);
        return;
    }
    os_log_info(OS_LOG_DEFAULT, "%sDriverXPCManager: Disconnecting from daemon...", LogPrefix);
    handleDaemonDisconnect("manual disconnect");
}

void DriverXPCManager::handleDaemonDisconnect(const char* reason) {
    os_log_info(OS_LOG_DEFAULT, "%sDriverXPCManager: Handling disconnect (%s).", LogPrefix, reason);
    isConnected_.store(false);
    daemonProxy_ = nullptr;
    if (xpcConnection_ != nullptr) {
        xpcConnection_.interruptionHandler = nil;
        xpcConnection_.invalidationHandler = nil;
        [xpcConnection_ invalidate];
        xpcConnection_ = nullptr;
    }
}

bool DriverXPCManager::isConnected() const {
    return isConnected_.load() && (xpcConnection_ != nullptr);
}

void DriverXPCManager::setPresenceStatus(bool isPresent) {
    if (!isConnected()) {
        os_log_error(OS_LOG_DEFAULT, "%sDriverXPCManager: Cannot set presence status, not connected to daemon.", LogPrefix);
        return;
    }
    if (!daemonProxy_) {
         os_log_error(OS_LOG_DEFAULT, "%sDriverXPCManager: Cannot set presence status, daemon proxy is nil.", LogPrefix);
         handleDaemonDisconnect("proxy nil on setPresenceStatus");
         return;
    }
    os_log_info(OS_LOG_DEFAULT, "%sDriverXPCManager: Setting driver presence status to %d via XPC.", LogPrefix, isPresent);
    @try {
        id<FWADaemonControlProtocol> proxy = (id<FWADaemonControlProtocol>)daemonProxy_;
        [proxy setDriverPresenceStatus:isPresent];
    } @catch (NSException *exception) {
        os_log_error(OS_LOG_DEFAULT, "%sDriverXPCManager: Exception calling setDriverPresenceStatus: %{public}@ - %{public}@", LogPrefix, exception.name, exception.reason);
        handleDaemonDisconnect("exception on setPresenceStatus");
    }
}
