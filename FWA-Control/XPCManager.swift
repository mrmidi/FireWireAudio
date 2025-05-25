//
//  XPCManager.swift
//  FWA-Control
//

import Foundation
import FWADaemonXPC
import Logging

// MARK: - Public model types

public struct DeviceStatus: Sendable {
    public let guid: UInt64
    public let isConnected: Bool
    public let isInitialized: Bool
    public let name: String?
    public let vendor: String?
}

/// Immutable, thread-safe wrapper for heterogeneous dictionaries returned by the daemon.
public struct InfoDictionary: @unchecked Sendable {
    public let raw: [AnyHashable: Any]
    public init(_ raw: [AnyHashable: Any]) { self.raw = raw }
}

public struct DeviceConfig: @unchecked Sendable {
    public let guid: UInt64
    public let info: InfoDictionary
}

// MARK: - Helper for streams

private struct Continuations: @unchecked Sendable {
    let deviceStatus: AsyncStream<DeviceStatus>.Continuation
    let deviceConfig: AsyncStream<DeviceConfig>.Continuation
    let driverStatus: AsyncStream<Bool>.Continuation
}

// MARK: - Wrapper to make XPC reply closures Sendable

private struct ReplyWrapper: @unchecked Sendable {
    let closure: (Bool) -> Void
    init(_ closure: @escaping (Bool) -> Void) { self.closure = closure }
    func call(_ v: Bool) { closure(v) }
}

// MARK: - XPCManager actor

public final actor XPCManager {
    /// Get current driver connection state from daemon
    public func getIsDriverConnected() async -> Bool? {
        guard let proxy = validProxy() else {
            logger.warning("getIsDriverConnected: No valid proxy to daemon.")
            return nil
        }
        return await withCheckedContinuation { (cc: CheckedContinuation<Bool?, Never>) in
            proxy.getIsDriverConnected { isConnected in
                cc.resume(returning: isConnected)
            }
        }
    }

    /// Legacy method name for backward compatibility
    public func getDriverConnected() async -> Bool? {
        return await getIsDriverConnected()
    }
    // Logger
//    private let logger = AppLoggers.xpcManager
    
    // Public streams
    public let deviceStatusStream: AsyncStream<DeviceStatus>
    public let deviceConfigStream: AsyncStream<DeviceConfig>
    public let driverStatusStream: AsyncStream<Bool>

    // Private
    private let logger = AppLoggers.xpcManager
    private let cont: Continuations

    private var xpcConnection: NSXPCConnection?
    private var listener: NSXPCListener?

    private let daemonServiceName = "net.mrmidi.FWADaemon"
    private let clientID = "GUI-\(UUID().uuidString)"
    fileprivate let engineService: EngineService

    // MARK: — Init / Deinit

    internal init(engineService: EngineService) {
        self.engineService = engineService
        var ds: AsyncStream<DeviceStatus>.Continuation!
        deviceStatusStream = AsyncStream { ds = $0 }

        var dc: AsyncStream<DeviceConfig>.Continuation!
        deviceConfigStream = AsyncStream { dc = $0 }

        var dr: AsyncStream<Bool>.Continuation!
        driverStatusStream = AsyncStream { dr = $0 }

        cont = Continuations(deviceStatus: ds,
                             deviceConfig: dc,
                             driverStatus: dr)
    }

    deinit {
        // cleanup is handled by explicit disconnect(); do not touch actor state here
    }

    // MARK: — Connection Management

    /// Primary method for connecting to daemon and starting the engine
    public func connectAndInitializeDaemonEngine() async throws -> Bool {
        // Check if already connected
        if xpcConnection != nil {
            logger.info("connectAndInitializeDaemonEngine: Already connected.")
            return true
        }

        logger.info("connectAndInitializeDaemonEngine: Establishing new connection and starting engine...")

        // 1. Create listener for callbacks FROM the daemon
        let handler = XPCNotificationHandler(manager: self)
        let localListener = NSXPCListener.anonymous()
        localListener.delegate = handler
        localListener.resume()
        self.listener = localListener

        // 2. Create XPC connection TO the daemon
        let daemonConnection = NSXPCConnection(machServiceName: daemonServiceName, options: [])
        daemonConnection.remoteObjectInterface = NSXPCInterface(with: FWADaemonControlProtocol.self)

        daemonConnection.interruptionHandler = ({ [weak self] in
            Task { await self?.handleDisconnect(reason: "Daemon connection interrupted", invalidateManually: false) }
        } as @Sendable () -> Void)
        
        daemonConnection.invalidationHandler = ({ [weak self] in
            Task { await self?.handleDisconnect(reason: "Daemon connection invalidated", invalidateManually: false) }
        } as @Sendable () -> Void)
        daemonConnection.resume()
        self.xpcConnection = daemonConnection

        // 3. Call the consolidated XPC method on the daemon

        return try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Bool, Error>) in
            guard let proxy = daemonConnection.remoteObjectProxyWithErrorHandler({ @Sendable [weak self] error in
                // XPC PROXY ERROR HANDLER (NOT on actor executor)
                Task { @MainActor in
                    await self?.handleProxyErrorForContinuation(error: error, specificContinuation: continuation)
                }
            }) as? FWADaemonControlProtocol else {
                let initError = NSError(domain: "XPCManagerErrorDomain", code: 101, userInfo: [NSLocalizedDescriptionKey: "Failed to get remote proxy for daemon."])
                self.logger.error("\(initError.localizedDescription)")
                continuation.resume(throwing: initError)
                return
            }

            proxy.registerClientAndStartEngine(
                self.clientID,
                clientNotificationEndpoint: localListener.endpoint
            ) { @Sendable [weak self] success, errorFromDaemon in
                // XPC REPLY BLOCK (NOT on actor executor)
                Task { @MainActor in
                    await self?.handleRegisterReplyForContinuation(success: success, error: errorFromDaemon, specificContinuation: continuation)
                }
            }
        }
    }

    /// Legacy connect method - now delegates to consolidated method
    public func connect() async -> Bool {
        do {
            return try await connectAndInitializeDaemonEngine()
        } catch {
            logger.error("Connection failed: \(error)")
            return false
        }
    }

    public func disconnect() async {
        await handleDisconnect(reason: "manual disconnect", invalidateManually: true)
    }

    private func handleDisconnect(reason: String, invalidateManually: Bool = true) async {
        logger.warning("XPC closed: \(reason)")
        if invalidateManually {
            xpcConnection?.invalidate() // Invalidate connection TO daemon
        }
        listener?.invalidate()      // Invalidate OUR listener for daemon callbacks
        xpcConnection = nil
        listener = nil
        // Optionally notify other parts of the GUI that the connection is lost.
        // Example: cont.driverStatus.yield(false) or a specific "disconnected" event.
    }

    // Actor-isolated method to handle proxy errors and resume a specific continuation
    nonisolated private func handleProxyErrorForContinuation(error: Error,
                                                 specificContinuation: CheckedContinuation<Bool, Error>?) {
        Task { @MainActor in
            await self.logger.error("Proxy error during an XPC call: \(error.localizedDescription)")
            await self.handleDisconnect(reason: "Proxy error on XPC call", invalidateManually: false)
            specificContinuation?.resume(throwing: error)
        }
    }

    // Actor-isolated method to handle replies and resume a specific continuation
    nonisolated private func handleRegisterReplyForContinuation(success: Bool,
                                                   error: Error?,
                                                   specificContinuation: CheckedContinuation<Bool, Error>?) {
        guard let continuation = specificContinuation else { return }

        Task { @MainActor in
            if let nsError = error {
                await self.logger.error("Daemon replied with error for registerClientAndStartEngine: \(nsError.localizedDescription)")
                continuation.resume(throwing: nsError)
            } else if success {
                await self.logger.info("Successfully registered with daemon and engine start requested.")
                continuation.resume(returning: true)
                Task {
                    if let isUp = await self.getIsDriverConnected() {
                       await self.dispatchDriverStatus(isUp)
                    }
                }
            } else {
                let genericError = NSError(domain: "XPCManagerErrorDomain", code: 102, userInfo: [NSLocalizedDescriptionKey: "Daemon returned failure for registerClientAndStartEngine without specific error."])
                await self.logger.error("\(genericError.localizedDescription)")
                continuation.resume(throwing: genericError)
            }
        }
    }

    // MARK: — Queries

    public func getConnectedGUIDs() async -> [UInt64]? {
        guard let proxy = validProxy() else { return nil }
        return await withCheckedContinuation { (cc: CheckedContinuation<[UInt64]?, Never>) in
            proxy.getConnectedDeviceGUIDs { @Sendable raw in
                cc.resume(returning: raw?.compactMap { $0.uint64Value })
            }
        }
    }

    public func getDeviceStatus(guid: UInt64) async -> InfoDictionary? {
        guard let proxy = validProxy() else { return nil }
        return await withCheckedContinuation { (cc: CheckedContinuation<InfoDictionary?, Never>) in
            proxy.getDeviceConnectionStatus(guid) { @Sendable info in
                cc.resume(returning: info.map(InfoDictionary.init))
            }
        }
    }

    public func getDeviceConfig(guid: UInt64) async -> InfoDictionary? {
        guard let proxy = validProxy() else { return nil }
        return await withCheckedContinuation { (cc: CheckedContinuation<InfoDictionary?, Never>) in
            proxy.getDeviceConfiguration(guid) { @Sendable info in
                cc.resume(returning: info.map(InfoDictionary.init))
            }
        }
    }

    public func requestSetSampleRate(guid: UInt64, rate: Double) async -> Bool {
        guard let proxy = validProxy() else { return false }
        return await withCheckedContinuation { (cc: CheckedContinuation<Bool, Never>) in
            proxy.requestSetNominalSampleRate(guid, rate: rate) { @Sendable ok in
                cc.resume(returning: ok)
            }
        }
    }

    public func requestStartIO(guid: UInt64) async -> Bool {
        guard let proxy = validProxy() else { return false }
        return await withCheckedContinuation { (cc: CheckedContinuation<Bool, Never>) in
            proxy.requestStartIO(guid) { @Sendable ok in
                cc.resume(returning: ok)
            }
        }
    }

    public func requestStopIO(guid: UInt64) async {
        guard let proxy = validProxy() else { return }
        proxy.requestStopIO(guid)
    }

    private func validProxy() -> FWADaemonControlProtocol? {
        guard let conn = xpcConnection else { return nil }
        let err: @Sendable (Error) -> Void = { [weak self] _ in
            Task { await self?.handleDisconnect(reason: "proxy error") }
        }
        return conn
            .remoteObjectProxyWithErrorHandler(err)
            as? FWADaemonControlProtocol
    }

    // MARK: — Event dispatchers

    fileprivate func dispatchStatus(_ status: DeviceStatus) {
        cont.deviceStatus.yield(status)
    }

    fileprivate func dispatchConfig(_ config: DeviceConfig) {
        cont.deviceConfig.yield(config)
    }

    fileprivate func dispatchDriverStatus(_ ok: Bool) {
        cont.driverStatus.yield(ok)
    }

    /// Manually query the daemon for current driver connection state
    // Removed duplicate getDriverConnected()

    // MARK: — Engine Lifecycle Methods

    /// Disconnect from daemon and stop engine (combines old unregisterClientAndStopEngine + disconnect)
    public func disconnectAndStopEngine() async throws -> Bool {
        logger.info("Disconnecting from daemon and stopping engine...")
        
        guard let proxy = validProxy() else {
            logger.warning("No valid proxy available for disconnectAndStopEngine, performing local cleanup only")
            await handleDisconnect(reason: "manual disconnect - no proxy", invalidateManually: true)
            return true // Consider this success since we're disconnecting anyway
        }
        
        let stopSuccess = try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Bool, Error>) in
            proxy.unregisterClientAndStopEngine(clientID) { success, error in
                if let nsError = error {
                    self.logger.error("unregisterClientAndStopEngine failed: \(nsError.localizedDescription)")
                    continuation.resume(throwing: nsError)
                } else {
                    self.logger.info("Successfully unregistered from daemon and requested engine stop")
                    continuation.resume(returning: success)
                }
            }
        }
        // Always perform local cleanup regardless of daemon response
        await handleDisconnect(reason: "manual disconnect after engine stop", invalidateManually: true)
        return stopSuccess
    }

    // MARK: — Legacy Engine Methods (Deprecated - use connectAndInitializeDaemonEngine instead)

    @available(*, deprecated, message: "Use connectAndInitializeDaemonEngine() instead")
    public func registerClientAndStartEngine() async throws -> Bool {
        logger.warning("Using deprecated registerClientAndStartEngine - consider using connectAndInitializeDaemonEngine")
        guard let proxy = validProxy() else { 
            throw NSError(domain: "XPCManager", code: 1, userInfo: [NSLocalizedDescriptionKey: "No valid XPC proxy"])
        }
        return await withCheckedContinuation { (cc: CheckedContinuation<Bool, Never>) in
            proxy.registerClientAndStartEngine(clientID, 
                                              clientNotificationEndpoint: listener!.endpoint) { @Sendable success, error in
                if let error = error {
                    self.logger.error("registerClientAndStartEngine failed: \(error)")
                }
                cc.resume(returning: success && error == nil)
            }
        }
    }

    @available(*, deprecated, message: "Use disconnectAndStopEngine() instead")
    public func unregisterClientAndStopEngine() async throws -> Bool {
        logger.warning("Using deprecated unregisterClientAndStopEngine - consider using disconnectAndStopEngine")
        guard let proxy = validProxy() else { 
            throw NSError(domain: "XPCManager", code: 1, userInfo: [NSLocalizedDescriptionKey: "No valid XPC proxy"])
        }
        return await withCheckedContinuation { (cc: CheckedContinuation<Bool, Never>) in
            proxy.unregisterClientAndStopEngine(clientID) { @Sendable success, error in
                if let error = error {
                    self.logger.error("unregisterClientAndStopEngine failed: \(error)")
                }
                cc.resume(returning: success && error == nil)
            }
        }
    }

    // MARK: — Device Operations

    public func sendAVCCommand(guid: UInt64, command: Data) async -> Data? {
        guard let proxy = validProxy() else { return nil }
        return await withCheckedContinuation { (cc: CheckedContinuation<Data?, Never>) in
            proxy.sendAVCCommand(toDevice: guid, command: command) { @Sendable responseData, error in
                if let error = error {
                    self.logger.error("sendAVCCommand failed: \(error)")
                }
                cc.resume(returning: responseData)
            }
        }
    }

    public func getDetailedDeviceInfoJSON(guid: UInt64) async -> String? {
        guard let proxy = validProxy() else { return nil }
        return await withCheckedContinuation { (cc: CheckedContinuation<String?, Never>) in
            proxy.getDetailedDeviceInfoJSON(forGUID: guid) { @Sendable jsonString, error in
                if let error = error {
                    self.logger.error("getDetailedDeviceInfoJSON failed: \(error)")
                }
                cc.resume(returning: jsonString)
            }
        }
    }

    public func startAudioStreams(guid: UInt64) async -> Bool {
        guard let proxy = validProxy() else { return false }
        return await withCheckedContinuation { (cc: CheckedContinuation<Bool, Never>) in
            proxy.startAudioStreams(forDevice: guid) { @Sendable success, error in
                if let error = error {
                    self.logger.error("startAudioStreams failed: \(error)")
                }
                cc.resume(returning: success && error == nil)
            }
        }
    }

    public func stopAudioStreams(guid: UInt64) async -> Bool {
        guard let proxy = validProxy() else { return false }
        return await withCheckedContinuation { (cc: CheckedContinuation<Bool, Never>) in
            proxy.stopAudioStreams(forDevice: guid) { @Sendable success, error in
                if let error = error {
                    self.logger.error("stopAudioStreams failed: \(error)")
                }
                cc.resume(returning: success && error == nil)
            }
        }
    }

    // MARK: — Proxy Helper
}

// MARK: - XPCNotificationHandler

private final class XPCNotificationHandler: NSObject,
                                           NSXPCListenerDelegate,
                                           FWAClientNotificationProtocol,
                                           @unchecked Sendable
{
    /// Retain callback connections so they aren't deallocated immediately
    private var callbackConnections = [NSXPCConnection]()
    
    // MARK: NSXPCListenerDelegate
    /// Accept the connection that the daemon establishes back to the GUI and
    /// expose ourselves (`self`) under the FWAClientNotificationProtocol.
    func listener(_ listener: NSXPCListener,
                  shouldAcceptNewConnection newConnection: NSXPCConnection) -> Bool {
        newConnection.exportedInterface =
            NSXPCInterface(with: FWAClientNotificationProtocol.self)
        newConnection.exportedObject = self
        // Keep a strong reference so this connection stays alive
        callbackConnections.append(newConnection)
        // When the connection invalidates, remove it from our list
        newConnection.invalidationHandler = { [weak self, weak newConnection] in
            guard let self = self, let conn = newConnection else { return }
            if let idx = self.callbackConnections.firstIndex(of: conn) {
                self.callbackConnections.remove(at: idx)
            }
        }
        newConnection.resume()
        return true
    }
    private let logger = AppLoggers.xpcManager
    private unowned let manager: XPCManager
    init(manager: XPCManager) {
        self.manager = manager
        super.init()
    }

    func daemonDidUpdateDeviceConnectionStatus(_ guid: UInt64,
                                               isConnected: Bool,
                                               isInitialized: Bool,
                                               deviceName: String?,
                                               vendorName: String?)
    {
        let status = DeviceStatus(
            guid: guid,
            isConnected: isConnected,
            isInitialized: isInitialized,
            name: deviceName,
            vendor: vendorName
        )
        let mgr = manager
        Task.detached { [mgr, status] in
            await mgr.dispatchStatus(status)
        }
    }

    func daemonDidUpdateDeviceConfiguration(_ guid: UInt64,
                                            configInfo: [AnyHashable: Any])
    {
        let config = DeviceConfig(
            guid: guid,
            info: InfoDictionary(configInfo)
        )
        let mgr = manager
        Task.detached { [mgr, config] in
            await mgr.dispatchConfig(config)
        }
    }

    // MARK: - Required FWAClientNotificationProtocol Methods
    
    /// Notifies the client that a new FireWire audio device has been discovered and initialized.
    func deviceAdded(_ deviceSummary: [String : Any]) {
        logger.info("Device added: \(deviceSummary)")
        if let guid = deviceSummary["guid"] as? UInt64 {
            // Notify EngineService
            let mgr = manager
            Task.detached { [mgr] in
                await mgr.engineService.handleDeviceUpdate(guid: guid, added: true)
            }
            
            // Also dispatch to status stream
            let status = DeviceStatus(
                guid: guid,
                isConnected: true,
                isInitialized: true,
                name: deviceSummary["name"] as? String,
                vendor: deviceSummary["vendor"] as? String
            )
            Task.detached { [mgr, status] in
                await mgr.dispatchStatus(status)
            }
        }
    }
    
    /// Notifies the client that a previously discovered device has been removed.
    func deviceRemoved(_ guid: UInt64) {
        logger.info("Device removed: GUID 0x\(String(format: "%llX", guid))")
        
        // Notify EngineService
        let mgr = manager
        Task.detached { [mgr] in
            await mgr.engineService.handleDeviceUpdate(guid: guid, added: false)
        }
        
        // Also dispatch to status stream
        let status = DeviceStatus(
            guid: guid,
            isConnected: false,
            isInitialized: false,
            name: nil,
            vendor: nil
        )
        Task.detached { [mgr, status] in
            await mgr.dispatchStatus(status)
        }
    }
    
    /// Notifies the client that detailed information or status for a device has been updated.
    func deviceInfoUpdated(_ guid: UInt64, newInfoJSON updatedInfoJSON: String) {
        logger.info("Device info updated for GUID 0x\(String(format: "%llX", guid))")
        // TODO: Parse JSON and create DeviceConfig update
        // For now, we'll just log it
    }
    
    /// Notifies the client about a change in the streaming status for a device.
    func streamStatusChanged(forDevice guid: UInt64, isStreaming: Bool, error: Error?) {
        logger.info("Stream status changed for GUID 0x\(String(format: "%llX", guid)): streaming=\(isStreaming), error=\(String(describing: error))")
        // TODO: Handle stream status changes
    }
    
    /// Forwards a log message originating from within the daemon (FWA/Isoch C++ libraries).
    func didReceiveLogMessage(_ senderID: String,
                              level: Int32,
                              message: String)
    {
        logger.critical("****** XPCNotificationHandler::didReceiveLogMessage CALLED! Sender: \(senderID), Level: \(level) ******")

        // Map the integer level to swift-log Level
        let mappedLevel: Logger.Level = Logger.Level.allCases.indices.contains(Int(level))
            ? Logger.Level.allCases[Int(level)]
            : .debug // Default to debug if level is out of range

        // Create a UILogEntry
        // Note: We don't have file/function/line info from the daemon via XPC
        let entry = UILogEntry(
            level: mappedLevel,
            message: .init(stringLiteral: message), // Create Logger.Message
            metadata: nil, // No metadata forwarded via XPC currently
            source: senderID, // Use the senderID from XPC as the source
            file: "#XPC#",     // Placeholder
            function: "#\(senderID)#", // Placeholder indicating source
            line: 0           // Placeholder
        )

        logger.trace("Received log from XPC (\(senderID), \(mappedLevel)): \(message)")

        logger.debug("Broadcasting UILogEntry from XPC: \(entry.id)") // <-- ADD DEBUG LOG
        Task.detached {
            await LogBroadcaster.shared.broadcast(entry: entry)
        }
    }
    
    /// Legacy method: Forwards a log message with FWAXPCLoglevel for daemon internal use.
    func didReceiveLogMessage(from senderID: String, level: FWAXPCLoglevel, message: String) {
        logger.trace("Received legacy log message from \(senderID) at level \(level): \(message)")
        
        // Convert FWAXPCLoglevel to Logger.Level
        let mappedLevel: Logger.Level
        switch level {
        case .trace:    mappedLevel = .trace
        case .debug:    mappedLevel = .debug
        case .info:     mappedLevel = .info
        case .warn:     mappedLevel = .warning
        case .error:    mappedLevel = .error
        case .critical: mappedLevel = .critical
        case .off:      mappedLevel = .critical
        @unknown default: mappedLevel = .debug
        }
        
        // Create a UILogEntry
        let entry = UILogEntry(
            level: mappedLevel,
            message: .init(stringLiteral: message),
            metadata: nil,
            source: senderID,
            file: "#XPC#",
            function: "#\(senderID)#",
            line: 0
        )
        
        Task.detached {
            await LogBroadcaster.shared.broadcast(entry: entry)
        }
    }

    func performSetClockSource(_ guid: UInt64,
                               clockSourceID: UInt32,
                               withReply reply: @escaping (Bool) -> Void)
    {
        let safe = ReplyWrapper(reply)
        let mgr = manager
        Task.detached {
            let ok = await mgr.engineService
                        .setClockSource(guid: guid, sourceID: clockSourceID)
            safe.call(ok)
        }
    }

    func performSetMasterVolumeScalar(_ guid: UInt64,
                                      scope: UInt32,
                                      element: UInt32,
                                      scalarValue: Float,
                                      withReply reply: @escaping (Bool) -> Void)
    {
        let safe = ReplyWrapper(reply)
        let mgr = manager
        Task.detached {
            let ok = await mgr.engineService
                        .setVolume(
                            guid: guid,
                            scope: scope,
                            element: element,
                            value: scalarValue
                        )
            safe.call(ok)
        }
    }

    func performSetMasterMute(_ guid: UInt64,
                              scope: UInt32,
                              element: UInt32,
                              muteState: Bool,
                              withReply reply: @escaping (Bool) -> Void)
    {
        let safe = ReplyWrapper(reply)
        let mgr = manager
        Task.detached {
            let ok = await mgr.engineService
                        .setMute(
                            guid: guid,
                            scope: scope,
                            element: element,
                            state: muteState
                        )
            safe.call(ok)
        }
    }

    func performStartIO(_ guid: UInt64,
                        withReply reply: @escaping (Bool) -> Void)
    {
        let safe = ReplyWrapper(reply)
        let mgr = manager
        Task.detached {
            let ok = await mgr.engineService
                        .startIO(guid: guid)
            safe.call(ok)
        }
    }

    func performStopIO(_ guid: UInt64) {
        let mgr = manager
        Task.detached {
            await mgr.engineService
                .stopIO(guid: guid)
        }
    }

    func performSetNominalSampleRate(_ guid: UInt64,
                                     rate: Double,
                                     withReply reply: @escaping (Bool) -> Void)
    {
        let safe = ReplyWrapper(reply)
        let mgr = manager
        Task.detached {
            let ok = await mgr.engineService
                        .setSampleRate(guid: guid, rate: rate)
            safe.call(ok)
        }
    }

    func driverConnectionStatusDidChange(_ isConnected: Bool) {
        logger.info("Received driverConnectionStatusDidChange: \(isConnected)")
        let mgr = manager // Capture manager reference
        // Call the manager's dispatcher asynchronously
        Task.detached { [mgr] in // Use Task.detached for fire-and-forget into the actor
            await mgr.dispatchDriverStatus(isConnected)
        }
    }
    // MARK: ‑ Hand‑shake ping
    func daemonHandshake(_ reply: @escaping (Bool) -> Void) {
        logger.info("Handshake ping received from daemon ✔︎")
        reply(true)                       // tell daemon that the callback works
    }
}