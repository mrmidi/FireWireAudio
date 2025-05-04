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

    public func connect() async -> Bool {
        guard xpcConnection == nil else { return true }

        // 1) listener for callbacks
        let handler = XPCNotificationHandler(manager: self)
        let listener = NSXPCListener.anonymous()
        listener.delegate = handler
        listener.resume()
        self.listener = listener

        // 2) open XPC connection
        let conn = NSXPCConnection(machServiceName: daemonServiceName, options: [])
        conn.remoteObjectInterface = NSXPCInterface(with: FWADaemonControlProtocol.self)

        // @Sendable interruption/invalidation handlers
        conn.interruptionHandler = ({
            [weak self] in
            Task { await self?.handleDisconnect(reason: "interrupted") }
        } as @Sendable () -> Void)

        conn.invalidationHandler = ({
            [weak self] in
            Task { await self?.handleDisconnect(reason: "invalidated") }
        } as @Sendable () -> Void)

        conn.resume()
        xpcConnection = conn

        // 3) register client
        let success = await withCheckedContinuation { (cc: CheckedContinuation<Bool, Never>) in
            let proxyError: @Sendable (Error) -> Void = { [weak self] err in
                Task { await self?.handleDisconnect(reason: "proxy error: \(err)") }
                cc.resume(returning: false) // Ensure continuation is always resumed
            }

            guard let proxy = conn
                .remoteObjectProxyWithErrorHandler(proxyError)
                    as? FWADaemonControlProtocol else {
                cc.resume(returning: false)
                return
            }

            proxy.registerClient(clientID,
                                 clientNotificationEndpoint: listener.endpoint) {
                success, _ in cc.resume(returning: success)
            }
        }
        // Seed initial driver status stream if registration succeeded
        if success {
            Task { [self] in
                if let isUp = await getDriverConnected() {
                    cont.driverStatus.yield(isUp)
                }
            }
        }
        return success
    }

    public func disconnect() async {
        await handleDisconnect(reason: "manual disconnect")
    }

    private func handleDisconnect(reason: String) async {
        logger.warning("XPC closed: \(reason)")
        xpcConnection?.invalidate()
        listener?.invalidate()
        xpcConnection = nil
        listener = nil
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
    public func getDriverConnected() async -> Bool? {
        guard let proxy = validProxy() else { return nil }
        return await withCheckedContinuation { (cc: CheckedContinuation<Bool?, Never>) in
            proxy.getIsDriverConnected { isConnected in
                cc.resume(returning: isConnected)
            }
        }
    }
}

// MARK: - XPCNotificationHandler

private final class XPCNotificationHandler: NSObject,
                                           NSXPCListenerDelegate,
                                           FWAClientNotificationProtocol,
                                           @unchecked Sendable
{
    /// Retain callback connections so they aren’t deallocated immediately
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

    func didReceiveLogMessage(from senderID: String,
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
