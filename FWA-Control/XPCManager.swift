import Foundation
    import Combine
    import Logging
    import FWADaemonXPC
    import SwiftUI // Needed for @MainActor, ObservableObject

    // Helper for safe array access
    extension Collection {
        subscript(safe index: Index) -> Element? {
            return indices.contains(index) ? self[index] : nil
        }
    }

    @MainActor
    class XPCManager: ObservableObject {
        // --- Published State ---
        @Published var isConnected: Bool = false

        // --- Combine Subjects for Notifications ---
        let deviceStatusUpdate = PassthroughSubject<(guid: UInt64, isConnected: Bool, isInitialized: Bool, name: String?, vendor: String?), Never>()
        let deviceConfigUpdate = PassthroughSubject<(guid: UInt64, configInfo: [AnyHashable: Any]), Never>()
        let logMessageReceived = PassthroughSubject<(sender: String, level: Logger.Level, message: String), Never>()
        let driverConnectionStatus = PassthroughSubject<Bool, Never>() // New subject for driver status

        // --- Private Properties ---
        private var xpcConnection: NSXPCConnection?
        private var notificationListener: NSXPCListener?
        private var notificationDelegate: XPCNotificationHandler?
        private let logger = AppLoggers.xpcManager
        private let daemonServiceName = "net.mrmidi.FWADaemon"
        private let clientID = "GUI-\(UUID().uuidString)"
        weak var deviceManager: DeviceManager? // For hardware callbacks

        // MARK: - Connection Management
        func connect(reason: String? = nil) { // <-- Add optional reason parameter
            guard xpcConnection == nil else {
                logger.info("🔌 XPC connection already exists or pending. Reason for call: \(reason ?? "N/A")")
                return
            }
            let logReason = reason ?? "Manual or initial connection"
            logger.info("🔌 Attempting XPC connection to \(daemonServiceName)... (\(logReason))")

            // 1. Setup listener for callbacks (Daemon -> GUI)
            self.notificationDelegate = XPCNotificationHandler(xpcManager: self)
            let listener = NSXPCListener.anonymous()
            listener.delegate = self.notificationDelegate // Delegate now conforms
            self.notificationListener = listener
            listener.resume()

            let listenerEndpoint = listener.endpoint // Not optional

            // 2. Create connection (GUI -> Daemon)
            let connection = NSXPCConnection(machServiceName: daemonServiceName, options: [])
            connection.remoteObjectInterface = NSXPCInterface(with: FWADaemonControlProtocol.self)
            connection.interruptionHandler = { [weak self] in self?.handleDisconnect("interrupted") }
            connection.invalidationHandler = { [weak self] in self?.handleDisconnect("invalidated") }
            self.xpcConnection = connection
            connection.resume()

            // 3. Register with Daemon
            guard let proxy = getProxy() else {
                logger.error("🚫 Failed to get proxy during connect.")
                handleDisconnect("proxy failure")
                return
            }
            proxy.registerClient(self.clientID, clientNotificationEndpoint: listenerEndpoint) { [weak self] success, daemonInfo in
                guard let self = self else { return }
                if success {
                    self.logger.info("✅ Registered with daemon successfully (\(logReason)). Info: \(daemonInfo ?? [:])") // <-- Include reason in success log
                    Task { @MainActor in self.isConnected = true }
                    // After successful registration, query initial driver status
                    self.getInitialDriverStatus()
                } else {
                    self.logger.error("❌ Failed to register with daemon (\(logReason)).") // <-- Include reason in failure log
                    Task { @MainActor in self.handleDisconnect("registration failed") }
                }
            }
        }

        func disconnect() {
            logger.info("🔌 Disconnecting XPC connection...")
            handleDisconnect("manual disconnect")
        }

        private func handleDisconnect(_ reason: String) {
            logger.warning("🔌 XPC Connection \(reason). Cleaning up.")
            guard isConnected else { return }
            self.isConnected = false
            self.xpcConnection?.invalidate()
            self.xpcConnection = nil
            self.notificationListener?.invalidate()
            self.notificationListener = nil
            self.notificationDelegate = nil
        }

        // Helper to get the proxy safely
        private func getProxy() -> FWADaemonControlProtocol? {
            guard let connection = self.xpcConnection else {
                logger.error("🚫 Attempted to get proxy, but XPC connection is nil.")
                Task { @MainActor in handleDisconnect("proxy request on nil connection") }
                return nil
            }
            return connection.remoteObjectProxyWithErrorHandler { [weak self] error in
                self?.logger.error("🚫 XPC communication error: \(error.localizedDescription)")
                Task { @MainActor in self?.handleDisconnect("proxy error") }
            } as? FWADaemonControlProtocol
        }

        // Fetch initial driver connection status after connecting to daemon
        private func getInitialDriverStatus() {
            guard let proxy = getProxy() else { return }
            logger.info("🔌 Querying initial driver connection status...")
            proxy.getIsDriverConnected { [weak self] isDriverConnected in
                self?.logger.info("🔌 Received initial driver status: \(isDriverConnected)")
                Task { @MainActor in
                    self?.driverConnectionStatus.send(isDriverConnected)
                }
            }
        }

        // MARK: - Methods Called by DeviceManager (Requests to Daemon)
        func getConnectedGUIDs(completion: @escaping ([UInt64]?) -> Void) {
            guard let proxy = getProxy() else { completion(nil); return }
            proxy.getConnectedDeviceGUIDs { guids in
                completion(guids?.compactMap { $0.uint64Value })
            }
        }

        func getDeviceStatus(guid: UInt64, completion: @escaping ([AnyHashable: Any]?) -> Void) {
            guard let proxy = getProxy() else { completion(nil); return }
            proxy.getDeviceConnectionStatus(guid) { statusInfo in
                completion(statusInfo)
            }
        }

        func getDeviceConfig(guid: UInt64, completion: @escaping ([AnyHashable: Any]?) -> Void) {
            guard let proxy = getProxy() else { completion(nil); return }
            proxy.getDeviceConfiguration(guid) { configInfo in
                completion(configInfo)
            }
        }

        func requestSetSampleRate(guid: UInt64, rate: Double, completion: @escaping (Bool) -> Void) {
            guard let proxy = getProxy() else { completion(false); return }
            proxy.requestSetNominalSampleRate(guid, rate: rate) { success in
                completion(success)
            }
        }

        func requestStartIO(guid: UInt64, completion: @escaping (Bool) -> Void) {
            guard let proxy = getProxy() else { completion(false); return }
            proxy.requestStartIO(guid) { success in
                completion(success)
            }
        }

        func requestStopIO(guid: UInt64) {
            guard let proxy = getProxy() else { return }
            proxy.requestStopIO(guid)
        }

        // MARK: - Methods Called by Logging System (Forwarding to Daemon)
        func forwardLogMessageToDaemon(level: Logger.Level, message: String) {
            // Not implemented: GUI logs locally
        }

        // MARK: - Methods Called by XPCNotificationHandler (Callbacks from Daemon)
        func daemonDidUpdateStatus(guid: UInt64, isConnected: Bool, isInitialized: Bool, name: String?, vendor: String?) {
            logger.debug("XPCManager received daemonDidUpdateStatus: GUID 0x\(String(format: "%llX", guid)) Conn:\(isConnected) Init:\(isInitialized)")
            deviceStatusUpdate.send((guid: guid, isConnected: isConnected, isInitialized: isInitialized, name: name, vendor: vendor))
        }

        func daemonDidUpdateConfig(guid: UInt64, configInfo: [AnyHashable : Any]) {
            logger.debug("XPCManager received daemonDidUpdateConfig: GUID 0x\(String(format: "%llX", guid))")
            deviceConfigUpdate.send((guid: guid, configInfo: configInfo))
        }

        func daemonDidSendLog(sender: String, level: Int32, message: String) {
            // Use bounds check and direct access instead of safe subscript + optional chaining
            let mappedLevel: Logger.Level
            if level >= 0 && level < Logger.Level.allCases.count {
                mappedLevel = Logger.Level.allCases[Int(level)]
            } else {
                logger.warning("Received log message with invalid level: \(level)")
                mappedLevel = .debug
            }
            logger.trace("XPCManager received log from \(sender): \(message)")
            logMessageReceived.send((sender: sender, level: mappedLevel, message: message))
        }

        // Called by XPCNotificationHandler when driver status changes
        func daemonDidUpdateDriverConnectionStatus(isConnected: Bool) {
            logger.info("🔌 Driver connection status changed: \(isConnected)")
            driverConnectionStatus.send(isConnected)
        }

        func handleSetSampleRateReply(guid: UInt64, success: Bool) {
            logger.info("Daemon relayed GUI reply for SetSampleRate GUID 0x\(String(format: "%llX", guid)): Success=\(success)")
            // For logging/state tracking if needed
        }
    }

    // MARK: - XPC Notification Handler Delegate
    private class XPCNotificationHandler: NSObject, FWAClientNotificationProtocol, NSXPCListenerDelegate {
        weak var xpcManager: XPCManager?
        private let logger = AppLoggers.xpcHandler

        init(xpcManager: XPCManager) {
            self.xpcManager = xpcManager
            super.init()
        }

        // MARK: - NSXPCListenerDelegate
        func listener(_ listener: NSXPCListener, shouldAcceptNewConnection newConnection: NSXPCConnection) -> Bool {
            logger.info("XPCNotificationHandler listener accepting connection from Daemon...")
            newConnection.remoteObjectInterface = NSXPCInterface(with: FWADaemonControlProtocol.self)
            newConnection.exportedInterface = NSXPCInterface(with: FWAClientNotificationProtocol.self)
            newConnection.exportedObject = self
            newConnection.interruptionHandler = { [weak self] in
                self?.logger.warning("Daemon connection to our listener interrupted.")
            }
            newConnection.invalidationHandler = { [weak self] in
                self?.logger.warning("Daemon connection to our listener invalidated.")
            }
            newConnection.resume()
            logger.info("Daemon connection to our listener resumed.")
            return true
        }

        // MARK: - FWAClientNotificationProtocol Implementation
        func daemonDidUpdateDeviceConnectionStatus(_ guid: UInt64, isConnected: Bool, isInitialized: Bool, deviceName: String?, vendorName: String?) {
            logger.debug("[Callback] daemonDidUpdateDeviceConnectionStatus: GUID 0x\(String(format: "%llX", guid))")
            Task { @MainActor in
                self.xpcManager?.daemonDidUpdateStatus(guid: guid, isConnected: isConnected, isInitialized: isInitialized, name: deviceName, vendor: vendorName)
            }
        }

        func daemonDidUpdateDeviceConfiguration(_ guid: UInt64, configInfo: [AnyHashable : Any]) {
            logger.debug("[Callback] daemonDidUpdateDeviceConfiguration: GUID 0x\(String(format: "%llX", guid))")
            Task { @MainActor in
                self.xpcManager?.daemonDidUpdateConfig(guid: guid, configInfo: configInfo)
            }
        }

        // Renamed to match Swift import convention
        func didReceiveLogMessage(from senderID: String, level: Int32, message: String) {
            Task { @MainActor in
                self.xpcManager?.daemonDidSendLog(sender: senderID, level: level, message: message)
            }
        }

        func driverConnectionStatusDidChange(_ isConnected: Bool) {
            logger.debug("[Callback] driverConnectionStatusDidChange: \(isConnected)")
            Task { @MainActor in
                self.xpcManager?.daemonDidUpdateDriverConnectionStatus(isConnected: isConnected)
            }
        }

        // --- Control Requests FORWARDED to GUI Client ---
        func performSetNominalSampleRate(_ guid: UInt64, rate: Double, withReply reply: @escaping (Bool) -> Void) {
            logger.info("[Callback] performSetNominalSampleRate: GUID 0x\(String(format: "%llX", guid)), Rate: \(rate)")
            Task {
                let success = await self.xpcManager?.deviceManager?.performHardwareSampleRateSet(guid: guid, rate: rate) ?? false
                reply(success)
            }
        }

        func performSetClockSource(_ guid: UInt64, clockSourceID: UInt32, withReply reply: @escaping (Bool) -> Void) {
            logger.info("[Callback] performSetClockSource: GUID 0x\(String(format: "%llX", guid)), ID: \(clockSourceID)")
            Task {
                let success = await self.xpcManager?.deviceManager?.performHardwareClockSourceSet(guid: guid, sourceID: clockSourceID) ?? false
                reply(success)
            }
        }

        func performSetMasterVolumeScalar(_ guid: UInt64, scope: UInt32, element: UInt32, scalarValue: Float, withReply reply: @escaping (Bool) -> Void) {
            logger.info("[Callback] performSetMasterVolumeScalar: GUID 0x\(String(format: "%llX", guid)) Val: \(scalarValue)")
            Task {
                let success = await self.xpcManager?.deviceManager?.performHardwareVolumeSet(guid: guid, scope: scope, element: element, value: scalarValue) ?? false
                reply(success)
            }
        }

        func performSetMasterMute(_ guid: UInt64, scope: UInt32, element: UInt32, muteState: Bool, withReply reply: @escaping (Bool) -> Void) {
            logger.info("[Callback] performSetMasterMute: GUID 0x\(String(format: "%llX", guid)) State: \(muteState)")
            Task {
                let success = await self.xpcManager?.deviceManager?.performHardwareMuteSet(guid: guid, scope: scope, element: element, state: muteState) ?? false
                reply(success)
            }
        }

        func performStartIO(_ guid: UInt64, withReply reply: @escaping (Bool) -> Void) {
            logger.info("[Callback] performStartIO: GUID 0x\(String(format: "%llX", guid))")
            Task {
                let success = await self.xpcManager?.deviceManager?.performHardwareStartIO(guid: guid) ?? false
                reply(success)
            }
        }

        func performStopIO(_ guid: UInt64) {
            logger.info("[Callback] performStopIO: GUID 0x\(String(format: "%llX", guid))")
            Task {
                await self.xpcManager?.deviceManager?.performHardwareStopIO(guid: guid)
            }
        }
    }
