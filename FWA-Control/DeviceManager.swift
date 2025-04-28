// === FWA-Control/DeviceManager.swift ===
// CORRECTED VERSION
//
//  DeviceManager.swift
//  FireWireAudioDaemon
//
//  Created by Alexander Shabelnikov on 23.04.2025.
//

import SwiftUI
import Combine // For AnyCancellable
import os      // For Logger
import FWA_CAPI // Import the C module (via module map)
import Logging // Add swift-log
import FWADaemonXPC
import ServiceManagement
import AppKit // For NSApplication activation state
// Domain models are in DomainModels.swift
// JSON decodables are in JsonDecodables.swift

@MainActor
final class DeviceManager: ObservableObject {
    
    // MARK: - Published Properties for UI
    @Published var isRunning: Bool = false
    @Published var devices: [UInt64: DeviceInfo] = [:] // Use Domain Model
    @Published var deviceJsons: [UInt64: String] = [:] // Store original JSON per device
    // New: Published property for device connection status
    @Published var isDaemonConnected = false // Connection GUI -> Daemon
    @Published var isDriverConnected = false // Connection Driver -> Daemon
    // New: show an install‐driver prompt
    @Published var showDriverInstallPrompt = false
    // New: Daemon install status for UI
    @Published var daemonInstallStatus: SMAppService.Status = .notFound
    @Published var showDaemonInstallPrompt = false
    
    // Published property for UI logs, populated by InMemoryLogHandler
    @Published var uiLogEntries: [UILogEntry] = [] {
        didSet {
            if uiLogEntries.count > logDisplayBufferSize {
                uiLogEntries.removeFirst(uiLogEntries.count - logDisplayBufferSize)
            }
        }
    }
    @AppStorage("settings.logDisplayBufferSize") var logDisplayBufferSize: Int = 500
    
    // MARK: - Internal State
    private var fwaEngineRef: FWAEngineRef?
    private var cancellables = Set<AnyCancellable>()
    private let logger = AppLoggers.deviceManager
    private var daemonStatusCheckTimer: Timer?
    private var wasInBackground: Bool = false // Track app background state
    
    // MARK: - XPCManager Integration
    private let xpcManager = XPCManager() // Dedicated XPC manager
    
    // MARK: - Daemon setup
    
    // Remove: private var xpcConnection: NSXPCConnection?
    
    // Remove: startDaemonConnection()
    
    // MARK: - Callback Storage
    private var cLogCallbackClosure: FWALogCallback?
    private var cDeviceCallbackClosure: FWADeviceNotificationCallback?
    
    // MARK: - Initialization / Deinitialization
    init() {
        logger.info("Initializing DeviceManager...")
        self.fwaEngineRef = FWAEngine_Create()
        
        if self.fwaEngineRef == nil {
            logger.critical("Failed to create FWA Engine C API instance!")
        } else {
            logger.info("FWA C API Engine created successfully.")
            do {
                try setupCAPILoggingCallback()
            } catch {
                logger.error("setupCAPILoggingCallback failed: \(error.localizedDescription)")
            }
            setupLogSubscription()
        }
        setupXPCBindings()
        xpcManager.connect() // Initiate XPC connection
        checkDaemonStatus() // Check daemon install status on init
        // Example: If you want to react to daemonInstallStatus changes elsewhere
        $daemonInstallStatus
            .receive(on: DispatchQueue.main)
            .sink { [weak self] status in
                // Show prompt if daemon is not enabled
                self?.showDaemonInstallPrompt = (status != .enabled)
            }
            .store(in: &cancellables)
        // Start timer only if needed initially
        if daemonInstallStatus != .enabled && !xpcManager.isConnected {
            startDaemonStatusTimer()
        }
        setupAppLifecycleObserver()
    }
    
    deinit {
        logger.warning("Deinitializing DeviceManager...")
        Task { @MainActor in
            stopDaemonStatusTimer() // Stop timer on deinit
        }
        if let engine = fwaEngineRef {
            logger.info("Attempting to stop engine via C API during deinit...")
            let stopResult = FWAEngine_Stop(engine)
            if stopResult != kIOReturnSuccess {
                logger.error("FWAEngine_Stop failed during deinit: \(stopResult)")
            }
            FWAEngine_Destroy(engine)
            fwaEngineRef = nil
            logger.info("FWA C API Engine destroyed.")
        }
        cancellables.forEach { $0.cancel() }
    }
    
    // Subscribe to log entries from the InMemoryLogHandler
    private func setupLogSubscription() {
        do {
            try logEntrySubject
                .receive(on: DispatchQueue.main)
                .sink { [weak self] entry in
                    self?.uiLogEntries.append(entry)
                }
                .store(in: &cancellables)
        } catch {
            logger.error("setupLogSubscription failed: \(error.localizedDescription)")
        }
        logger.debug("Subscribed to UI log entries.")
    }
    
    // MARK: - XPCManager Bindings
    private func setupXPCBindings() {
        do {
            try xpcManager.$isConnected
                .receive(on: DispatchQueue.main)
                .assign(to: &$isDaemonConnected)
        } catch {
            logger.error("setupXPCBindings failed: \(error.localizedDescription)")
        }
        // Subscribe to notifications from XPCManager
        xpcManager.deviceStatusUpdate
            .receive(on: DispatchQueue.main)
            .sink { [weak self] update in
                self?.handleDaemonDeviceStatusUpdate(guid: update.guid,
                                                     isConnected: update.isConnected,
                                                     isInitialized: update.isInitialized,
                                                     name: update.name,
                                                     vendor: update.vendor)
            }
            .store(in: &cancellables)
        xpcManager.deviceConfigUpdate
            .receive(on: DispatchQueue.main)
            .sink { [weak self] update in
                self?.handleDaemonConfigUpdate(guid: update.guid, configInfo: update.configInfo)
            }
            .store(in: &cancellables)
        xpcManager.logMessageReceived
            .receive(on: DispatchQueue.main)
            .sink { [weak self] logInfo in
                self?.logger.log(level: logInfo.level, "[\(logInfo.sender)] \(logInfo.message)")
            }
            .store(in: &cancellables)
        // --- Driver Install Prompt Logic ---
        // Assign driver status directly for potential other UI bindings
        xpcManager.driverConnectionStatus
            .receive(on: DispatchQueue.main)
            .assign(to: &$isDriverConnected)
        //            .store(in: &cancellables)
        // React to driver status changes to manage the prompt
        xpcManager.driverConnectionStatus
            .receive(on: DispatchQueue.main)
            .sink { [weak self] driverIsUp in
                guard let self = self else { return }
                // Only evaluate showing the prompt if the daemon *is* connected
                if self.isDaemonConnected {
                    self.showDriverInstallPrompt = !driverIsUp // Show if driver is down
                } else {
                    self.showDriverInstallPrompt = false // Hide if daemon is down
                }
            }
            .store(in: &cancellables)
        // React to daemon connection status changes to manage the prompt
        xpcManager.$isConnected // Listen to changes in daemon connection
            .receive(on: DispatchQueue.main)
            .sink { [weak self] daemonIsUp in
                if !daemonIsUp {
                    self?.showDriverInstallPrompt = false // Hide the prompt if daemon disconnects
                }
                // No action needed if daemon connects; the driver status sink handles that.
            }
            .store(in: &cancellables)
    }
    
    // --- Methods to handle XPCManager notifications ---
    private func handleDaemonDeviceStatusUpdate(guid: UInt64, isConnected: Bool, isInitialized: Bool, name: String?, vendor: String?) {
        logger.info("Handling Daemon Status Update: GUID 0x\(String(format: "%llX", guid)) Conn:\(isConnected) Init:\(isInitialized)")
        if !isConnected || !isInitialized {
            if devices[guid] != nil {
                devices[guid]?.isConnected = false
            }
        } else {
            if devices[guid] == nil || devices[guid]?.isConnected == false {
                Task {
                    do {
                        try await fetchAndAddOrUpdateDevice(guid: guid)
                    } catch {
                        logger.error("Failed to fetch and update device for GUID 0x\(String(format: "%llX", guid)): \(error.localizedDescription)")
                    }
                }
            }
        }
    }
    
    private func handleDaemonConfigUpdate(guid: UInt64, configInfo: [AnyHashable : Any]) {
        logger.info("Handling Daemon Config Update: GUID 0x\(String(format: "%llX", guid))")
        // Optionally refresh device info if needed
        // Task { await fetchAndAddOrUpdateDevice(guid: guid) }
    }
    
    // --- Example user action method using XPCManager ---
    func userSetSampleRate(guid: UInt64, rate: Double) {
        guard isDaemonConnected else {
            logger.error("Cannot set sample rate, daemon not connected.")
            return
        }
        logger.info("User requesting sample rate \(rate) for GUID 0x\(String(format: "%llX", guid)) via XPC...")
        xpcManager.requestSetSampleRate(guid: guid, rate: rate) { [weak self] success in
            Task { @MainActor in
                if success {
                    self?.logger.info("Daemon confirmed sample rate set for GUID 0x\(String(format: "%llX", guid)). Refreshing info.")
                    try await self?.fetchAndAddOrUpdateDevice(guid: guid)
                } else {
                    self?.logger.error("Daemon reported failure setting sample rate for GUID 0x\(String(format: "%llX", guid)).")
                }
            }
        }
    }
    
    // MARK: - Public Control Methods
    func start() {
        guard let engine = fwaEngineRef else {
            logger.error("Cannot start, C API engine pointer is null.")
            return
        }
        guard !isRunning else {
            logger.warning("Start called, but engine already running.")
            return
        }
        
        logger.info("Starting FWA Engine via C API...")
        let context = Unmanaged.passUnretained(self).toOpaque()
        
        // --- Define Device Callback ---
        let swiftDeviceCallback: FWADeviceNotificationCallback = { (contextPtr, deviceRef, connected) in
            guard let context = contextPtr else {
                Logging.Logger(label: "DeviceCallback.Error").error("Received device callback with null context.")
                return
            }
            let manager = Unmanaged<DeviceManager>.fromOpaque(context).takeUnretainedValue()
            
            var guid: UInt64 = 0
            let guidResult = FWADevice_GetGUID(deviceRef, &guid) // Use deviceRef directly
            
            if guidResult == kIOReturnSuccess && guid != 0 {
                manager.logger.debug("Device Callback: GUID 0x\(String(format: "%llX", guid)), Connected: \(connected)")
                Task { // Use Task for async operations on main actor
                    await manager.handleDeviceUpdate(guid: guid, added: connected)
                }
            } else {
                manager.logger.error("Failed to get GUID from deviceRef in callback. Connected: \(connected), Error: \(guidResult)")
            }
        }
        self.cDeviceCallbackClosure = swiftDeviceCallback // Store reference
        
        // --- Call C API Start ---
        let result = FWAEngine_Start(engine, swiftDeviceCallback, context)
        
        if result == kIOReturnSuccess {
            self.isRunning = true
            logger.info("FWA Engine started successfully via C API.")
        } else {
            self.isRunning = false
            logger.error("Failed to start FWA Engine via C API: IOReturn \(result)")
        }
    }
    
    func stop() {
        guard let engine = fwaEngineRef else {
            logger.error("Cannot stop, C API engine pointer is null.")
            return
        }
        guard isRunning else {
            logger.warning("Stop called, but engine not running.")
            return
        }
        
        logger.info("Stopping FWA Engine via C API...")
        let result = FWAEngine_Stop(engine)
        
        self.isRunning = false
        self.devices.removeAll() // Clear device list
        
        if result == kIOReturnSuccess {
            logger.info("FWA Engine stopped successfully via C API.")
        } else {
            logger.error("Failed to stop FWA Engine via C API: IOReturn \(result)")
        }
    }
    
    func refreshDevice(guid: UInt64) {
        guard fwaEngineRef != nil else {
            logger.error("Cannot refresh device, engine is nil.")
            return
        }
        guard devices[guid] != nil else {
            logger.warning("Attempted to refresh non-existent device GUID: 0x\(String(format: "%llX", guid))")
            return
        }
        logger.info("Refreshing device info for GUID: 0x\(String(format: "%llX", guid))...")
        Task { // Use Task for async work triggered by UI action
            try await fetchAndAddOrUpdateDevice(guid: guid)
        }
    }
    
    func refreshAllDevices() {
        guard isRunning else {
            logger.warning("Cannot refresh devices, manager not running.")
            return
        }
        logger.info("Refreshing all connected devices...")
        let currentGUIDs = Array(devices.keys)
        if currentGUIDs.isEmpty {
            logger.info("No devices currently known to refresh.")
            return
        }
        Task { // Use Task for async work triggered by UI action
            for guid in currentGUIDs {
                try await fetchAndAddOrUpdateDevice(guid: guid) // Call the async fetcher
            }
            logger.info("Finished refreshing all devices.")
        }
    }
    
    // MARK: - Command Sending
    func sendCommand(guid: UInt64, command: Data) -> Data? {
        guard let engine = fwaEngineRef else {
            logger.error("Cannot send command, C API engine is null.")
            return nil
        }
        guard devices[guid] != nil else {
            logger.error("Cannot send command to unknown GUID: 0x\(String(format: "%llX", guid))")
            return nil
        }
        
        logger.trace("Sending command to GUID 0x\(String(format: "%llX", guid)): \(command.map { String(format: "%02X", $0) }.joined())")
        
        var responseDataPtr: UnsafeMutablePointer<UInt8>? = nil
        var responseLen: Int = 0 // Corresponds to size_t in C
        
        let commandResult: IOReturn = command.withUnsafeBytes { (commandBufferPtr: UnsafeRawBufferPointer) -> IOReturn in
            guard let commandBytes = commandBufferPtr.baseAddress?.assumingMemoryBound(to: UInt8.self) else {
                logger.error("Failed to get command data bytes.")
                return kIOReturnBadArgument
            }
            // Ensure signature matches FWA_CAPI definition
            return FWAEngine_SendCommand(
                engine,
                guid,
                commandBytes,
                command.count,
                &responseDataPtr,
                &responseLen
            )
        }
        
        if commandResult != kIOReturnSuccess {
            logger.error("FWAEngine_SendCommand failed for GUID 0x\(String(format: "%llX", guid)): IOReturn \(commandResult)")
            // CRITICAL: Free buffer even on API error, just in case C allocated partially
            if let buffer = responseDataPtr { FWADevice_FreeResponseBuffer(buffer) }
            return nil
        }
        
        // Check if response buffer is valid and has data
        guard let buffer = responseDataPtr, responseLen > 0 else {
            logger.trace("Command sent successfully to GUID 0x\(String(format: "%llX", guid)), no response data returned.")
            // CRITICAL: Free buffer if allocated but len is 0 (shouldn't happen with check above, but safety first)
            if let allocatedBufferWithZeroLen = responseDataPtr { FWADevice_FreeResponseBuffer(allocatedBufferWithZeroLen) }
            return Data() // Return empty Data for success with no payload
        }
        
        // Copy data and free C buffer
        let responseData = Data(bytes: buffer, count: Int(responseLen))
        logger.trace("Received response from GUID 0x\(String(format: "%llX", guid)) (\(responseLen) bytes): \(responseData.map { String(format: "%02X", $0) }.joined())")
        FWADevice_FreeResponseBuffer(buffer) // Free the C-allocated buffer
        
        return responseData
    }
    
    // MARK: - Private Callback Handlers & Helpers
    
    private func setupCAPILoggingCallback() {
        guard let engine = fwaEngineRef else { return }
        let context = Unmanaged.passUnretained(self).toOpaque()
        
        
        func mapCAPILogLevel(_ cLevel: FWALogLevel) -> Logging.Logger.Level {
            switch cLevel {
            case FWA_LOG_LEVEL_TRACE:    return .trace
            case FWA_LOG_LEVEL_DEBUG:    return .debug
            case FWA_LOG_LEVEL_INFO:     return .info
            case FWA_LOG_LEVEL_WARN:     return .warning
            case FWA_LOG_LEVEL_ERROR:    return .error
            case FWA_LOG_LEVEL_CRITICAL: return .critical
            default:
                Logging.Logger(label: "mapCAPILogLevel").warning("Unknown FWALogLevel encountered: \(cLevel)")
                return .debug
            }
        }
        
        let swiftLogCallback: FWALogCallback = { (contextPtr, cLevel, messagePtr) in
            guard let context = contextPtr, let messagePtr = messagePtr else { return }
            let manager = Unmanaged<DeviceManager>.fromOpaque(context).takeUnretainedValue()
            let message = String(cString: messagePtr)
            let swiftLevel = mapCAPILogLevel(cLevel)
            manager.logger.log(level: swiftLevel, "\(message)", metadata: ["source": "FWA_CAPI"])
        }
        self.cLogCallbackClosure = swiftLogCallback
        
        let result = FWAEngine_SetLogCallback(engine, swiftLogCallback, context)
        if result != kIOReturnSuccess {
            logger.error("Failed to set C log callback: IOReturn \(result)")
        } else {
            logger.info("C log callback registered.")
        }
    }
    
    /// Handles device notifications received from the C callback (runs on main actor via Task).
    private func handleDeviceUpdate(guid: UInt64, added: Bool) async {
        // This method is now async and called within a Task on the main actor
        if added {
            logger.info("Device added: GUID 0x\(String(format: "%llX", guid)). Fetching details...")
            do  { try await fetchAndAddOrUpdateDevice(guid: guid) }
            catch {
                logger.error("Failed to fetch device info for GUID 0x\(String(format: "%llX", guid)): \(error.localizedDescription)")
            }// Call the async fetcher
        } else {
            logger.info("Device removed: GUID 0x\(String(format: "%llX", guid)).")
            devices.removeValue(forKey: guid)
            deviceJsons.removeValue(forKey: guid) // Also remove JSON
        }
    }
    
    /// Fetches JSON info, decodes, maps to Domain Model, and updates published dictionary. (async)
    private func fetchAndAddOrUpdateDevice(guid: UInt64) async throws {
        guard let engine = fwaEngineRef else {
            logger.error("fetchAndAddOrUpdateDevice failed: engine is nil for GUID 0x\(String(format: "%llX", guid))")
            return
        }
        logger.debug("Fetching JSON for GUID: 0x\(String(format: "%llX", guid))...")
        
        // --- Step 1: Call C API ---
        guard let cJsonString = FWAEngine_GetInfoJSON(engine, guid) else {
            logger.error("FWAEngine_GetInfoJSON returned NULL for GUID 0x\(String(format: "%llX", guid)).")
            // Optionally remove device if info fetch fails critically
            // devices.removeValue(forKey: guid)
            return
        }
        defer { FWADevice_FreeString(cJsonString) } // Ensure C string is freed
        let jsonString = String(cString: cJsonString)
        deviceJsons[guid] = jsonString // Store original JSON
        
        // --- Step 2: Decode JSON string into intermediate `JsonDeviceData` ---
        guard !jsonString.isEmpty, let jsonData = jsonString.data(using: .utf8) else {
            logger.error("Failed to convert C JSON string to Data or string was empty for GUID 0x\(String(format: "%llX", guid)).")
            devices.removeValue(forKey: guid) // Remove if JSON is fundamentally broken
            return
        }
        
        let decoder = JSONDecoder()
        let jsonDataWrapper: JsonDeviceData
        do {
            jsonDataWrapper = try decoder.decode(JsonDeviceData.self, from: jsonData)
            logger.debug("Successfully decoded JSON string to JsonDeviceData for GUID 0x\(String(format: "%llX", guid))")
        } catch {
            logger.error("JSON Decoding Error for GUID 0x\(String(format: "%llX", guid)): \(error.localizedDescription)")
            if let decodingError = error as? DecodingError {
                switch decodingError {
                case let .typeMismatch(type, context):
                    logger.error("→ Type mismatch: \(type), at path \(context.codingPath.map(\.stringValue).joined(separator: "."))")
                case let .valueNotFound(type, context):
                    logger.error("→ Value not found: \(type), at path \(context.codingPath.map(\.stringValue).joined(separator: "."))")
                case let .keyNotFound(key, context):
                    // Convert key and path to plain Strings:
                    let keyName    = key.stringValue
                    let pathString = context.codingPath.map(\.stringValue).joined(separator: ".")
                    logger.error("→ Key not found: \(keyName), at path \(pathString), desc: \(context.debugDescription)")
                case let .dataCorrupted(context):
                    let pathString = context.codingPath.map(\.stringValue).joined(separator: ".")
                    logger.error("→ Data corrupted at path \(pathString): \(context.debugDescription)")
                @unknown default:
                    logger.error("→ Unknown decoding error: \(decodingError)")
                }
            }
            logger.error("Received JSON String that failed decoding:\n---\n\(jsonString)\n---") // Log problematic JSON
            devices.removeValue(forKey: guid) // Remove if JSON cannot be decoded
            return
        }
        
        // --- Step 3: Map `JsonDeviceData` to the `DeviceInfo` domain model ---
        if let domainDeviceInfo = mapJsonToDomainDevice(jsonData: jsonDataWrapper, guidForLog: guid) {
            // --- Step 4: Update Published Property (already on main actor) ---
            self.devices[domainDeviceInfo.guid] = domainDeviceInfo // Update or add
            self.logger.info("Successfully mapped and updated DeviceInfo for GUID 0x\(String(format: "%llX", domainDeviceInfo.guid)) (\(domainDeviceInfo.deviceName))")
        } else {
            // Mapping function indicated a critical failure (e.g., couldn't parse GUID)
            self.logger.error("Failed to map JsonDeviceData to DeviceInfo domain model for GUID 0x\(String(format: "%llX", guid)). Check previous mapping logs.")
            self.devices.removeValue(forKey: guid) // Remove if mapping fails critically
        }
    }
    
    
    // MARK: - Mapping Helper Functions (JSON Decodables -> Domain Models)
    
    /// Maps the raw decoded JSON data structure to the application's domain model.
    private func mapJsonToDomainDevice(jsonData: JsonDeviceData, guidForLog: UInt64) -> DeviceInfo? {
        var deviceInfo = DeviceInfo() // Use Domain Model
        
        let guidString = jsonData.guid.hasPrefix("0x") ? String(jsonData.guid.dropFirst(2)) : jsonData.guid
        guard let guid = UInt64(guidString, radix: 16), guid != 0 else {
            logger.error("GUID 0x\(String(format: "%llX", guidForLog)): Failed to parse GUID string: '\(jsonData.guid)'. Cannot create DeviceInfo.")
            return nil // Critical failure
        }
        deviceInfo.guid = guid // Use parsed GUID
        
        deviceInfo.deviceName = jsonData.deviceName
        deviceInfo.vendorName = jsonData.vendorName
        deviceInfo.vendorIdString = jsonData.vendorId
        deviceInfo.modelIdString = jsonData.modelId
        deviceInfo.isConnected = true // Assume connected since we are fetching info
        
        let unitAddress: UInt8 = 0xFF
        if let jsonUnitPlugs = jsonData.unitPlugs {
            deviceInfo.numIsoInPlugs = jsonUnitPlugs.numIsoInput ?? UInt32(jsonUnitPlugs.isoInputPlugs?.count ?? 0)
            deviceInfo.numIsoOutPlugs = jsonUnitPlugs.numIsoOutput ?? UInt32(jsonUnitPlugs.isoOutputPlugs?.count ?? 0)
            deviceInfo.numExtInPlugs = jsonUnitPlugs.numExternalInput ?? UInt32(jsonUnitPlugs.externalInputPlugs?.count ?? 0)
            deviceInfo.numExtOutPlugs = jsonUnitPlugs.numExternalOutput ?? UInt32(jsonUnitPlugs.externalOutputPlugs?.count ?? 0)
            
            deviceInfo.isoInputPlugs = jsonUnitPlugs.isoInputPlugs?.compactMap { mapJsonToDomainPlug(jsonPlug: $0, defaultSubunitAddress: unitAddress, guid: guid) } ?? []
            deviceInfo.isoOutputPlugs = jsonUnitPlugs.isoOutputPlugs?.compactMap { mapJsonToDomainPlug(jsonPlug: $0, defaultSubunitAddress: unitAddress, guid: guid) } ?? []
            deviceInfo.externalInputPlugs = jsonUnitPlugs.externalInputPlugs?.compactMap { mapJsonToDomainPlug(jsonPlug: $0, defaultSubunitAddress: unitAddress, guid: guid) } ?? []
            deviceInfo.externalOutputPlugs = jsonUnitPlugs.externalOutputPlugs?.compactMap { mapJsonToDomainPlug(jsonPlug: $0, defaultSubunitAddress: unitAddress, guid: guid) } ?? []
        } else {
            logger.warning("GUID 0x\(String(format: "%llX", guid)): No 'unitPlugs' data found in JSON.")
        }
        
        if let jsonSubunits = jsonData.subunits {
            deviceInfo.audioSubunit = mapJsonToDomainAudioSubunit(jsonContainer: jsonSubunits.audio, guid: guid)
            deviceInfo.musicSubunit = mapJsonToDomainMusicSubunit(jsonContainer: jsonSubunits.music, guid: guid)
        } else {
            logger.warning("GUID 0x\(String(format: "%llX", guid)): No 'subunits' data found in JSON.")
        }
        
        return deviceInfo
    }
    
    private func mapJsonToDomainPlug(jsonPlug: JsonPlugData, defaultSubunitAddress: UInt8, guid: UInt64) -> AudioPlugInfo? {
        guard let direction = PlugDirection(rawValue: jsonPlug.direction) else {
            logger.warning("GUID 0x\(String(format: "%llX", guid)): Failed to map plug direction: '\(jsonPlug.direction)' for plug #\(jsonPlug.plugNumber) @ 0x\(String(format: "%02X", defaultSubunitAddress)). Skipping plug.")
            return nil
        }
        let usage = PlugUsage(rawValue: jsonPlug.usage) ?? .unknown
        if usage == .unknown {
            logger.warning("GUID 0x\(String(format: "%llX", guid)): Unknown plug usage: '\(jsonPlug.usage)' for plug #\(jsonPlug.plugNumber) @ 0x\(String(format: "%02X", defaultSubunitAddress)). Using .unknown.")
        }
        
        let connectionInfo = mapJsonToDomainConnection(jsonConnInfo: jsonPlug.connectionInfo, guid: guid)
        let currentFormat = mapJsonToDomainStreamFormat(jsonFormat: jsonPlug.currentFormat, guid: guid)
        let supportedFormats = jsonPlug.supportedFormats?.compactMap { mapJsonToDomainStreamFormat(jsonFormat: $0, guid: guid) } ?? []
        
        var info = AudioPlugInfo(
            subunitAddress: defaultSubunitAddress,
            plugNumber: jsonPlug.plugNumber,
            direction: direction,
            usage: usage,
            plugName: jsonPlug.name,
            connectionInfo: connectionInfo,
            currentStreamFormat: currentFormat,
            supportedStreamFormats: supportedFormats
        )
        // If top‐level (unit) plug has no JSON name, assign “Unit Input #0” etc.
        if defaultSubunitAddress == 0xFF, info.plugName == nil {
            info.plugName = "Unit \(direction.description) #\(jsonPlug.plugNumber)"
        }
        return info
    }
    
    private func mapJsonToDomainConnection(jsonConnInfo: JsonConnectionInfo?, guid: UInt64) -> PlugConnectionInfo? {
        guard let jsonInfo = jsonConnInfo else { return nil }
        var domainInfo = PlugConnectionInfo()
        
        let subunitString = jsonInfo.sourceSubUnit.hasPrefix("0x") ? String(jsonInfo.sourceSubUnit.dropFirst(2)) : jsonInfo.sourceSubUnit
        domainInfo.sourceSubUnitAddress = UInt8(subunitString, radix: 16) ?? 0xFF
        if domainInfo.sourceSubUnitAddress == 0xFF && !subunitString.isEmpty {
            logger.warning("GUID 0x\(String(format: "%llX", guid)): Failed to parse sourceSubUnit hex: '\(jsonInfo.sourceSubUnit)'.")
        }
        
        let statusString = jsonInfo.sourcePlugStatus.hasPrefix("0x") ? String(jsonInfo.sourcePlugStatus.dropFirst(2)) : jsonInfo.sourcePlugStatus
        domainInfo.sourcePlugStatusValue = UInt8(statusString, radix: 16) ?? 0xFF
        if domainInfo.sourcePlugStatusValue == 0xFF && !statusString.isEmpty {
            logger.warning("GUID 0x\(String(format: "%llX", guid)): Failed to parse sourcePlugStatus hex: '\(jsonInfo.sourcePlugStatus)'.")
        }
        
        domainInfo.sourcePlugNumber = jsonInfo.sourcePlugNum
        domainInfo.isConnected = (domainInfo.sourceSubUnitAddress < 0xEF) // Original logic: Consider if status is better
        
        return domainInfo
    }
    
    private func mapJsonToDomainStreamFormat(jsonFormat: JsonAudioStreamFormat?, guid: UInt64) -> AudioStreamFormat? {
        guard let jsonFmt = jsonFormat else { return nil }
        guard let formatType = FormatType(rawValue: jsonFmt.formatType) else {
            logger.warning("GUID 0x\(String(format: "%llX", guid)): Failed to map format type: '\(jsonFmt.formatType)'. Skipping format.")
            return nil
        }
        let sampleRate = SampleRate(rawValue: jsonFmt.sampleRate) ?? .unknown
        if sampleRate == .unknown {
            logger.warning("GUID 0x\(String(format: "%llX", guid)): Unknown sample rate: '\(jsonFmt.sampleRate)'. Using .unknown.")
        }
        
        let channels = jsonFmt.channels?.compactMap { mapJsonToDomainChannelInfo(jsonChan: $0, guid: guid) } ?? []
        
        return AudioStreamFormat(
            formatType: formatType,
            sampleRate: sampleRate,
            syncSource: jsonFmt.syncSource,
            channels: channels
        )
    }
    
    private func mapJsonToDomainChannelInfo(jsonChan: JsonChannelFormatInfo?, guid: UInt64) -> ChannelFormatInfo? {
        guard let jsonCh = jsonChan else { return nil }
        guard let formatCode = StreamFormatCode(rawValue: jsonCh.formatCode) else {
            logger.warning("GUID 0x\(String(format: "%llX", guid)): Failed to map channel format code: '\(jsonCh.formatCode)'. Skipping channel.")
            return nil
        }
        return ChannelFormatInfo(
            channelCount: jsonCh.channelCount,
            formatCode: formatCode
        )
    }
    
    private func mapJsonToDomainAudioSubunit(jsonContainer: JsonAudioSubunitContainer?, guid: UInt64) -> AudioSubunitInfo? {
        guard let container = jsonContainer else { return nil }
        var audioSubunitInfo = AudioSubunitInfo()
        let audioAddress: UInt8 = SubunitType.audio.rawValue
        
        audioSubunitInfo.subunitType = .audio
        audioSubunitInfo.audioDestPlugCount = container.numDestPlugs ?? UInt32(container.destPlugs?.count ?? 0)
        audioSubunitInfo.audioSourcePlugCount = container.numSourcePlugs ?? UInt32(container.sourcePlugs?.count ?? 0)
        audioSubunitInfo.audioDestPlugs = container.destPlugs?.compactMap { mapJsonToDomainPlug(jsonPlug: $0, defaultSubunitAddress: audioAddress, guid: guid) } ?? []
        audioSubunitInfo.audioSourcePlugs = container.sourcePlugs?.compactMap { mapJsonToDomainPlug(jsonPlug: $0, defaultSubunitAddress: audioAddress, guid: guid) } ?? []
        return audioSubunitInfo
    }
    
    private func mapJsonToDomainMusicSubunit(jsonContainer: JsonMusicSubunitContainer?, guid: UInt64) -> MusicSubunitInfo? {
        guard let container = jsonContainer else { return nil }
        var musicSubunitInfo = MusicSubunitInfo()
        let musicAddress: UInt8 = SubunitType.music.rawValue
        
        musicSubunitInfo.subunitType = .music
        musicSubunitInfo.musicDestPlugCount = container.numDestPlugs ?? UInt32(container.destPlugs?.count ?? 0)
        musicSubunitInfo.musicSourcePlugCount = container.numSourcePlugs ?? UInt32(container.sourcePlugs?.count ?? 0)
        musicSubunitInfo.musicDestPlugs = container.destPlugs?.compactMap { mapJsonToDomainPlug(jsonPlug: $0, defaultSubunitAddress: musicAddress, guid: guid) } ?? []
        musicSubunitInfo.musicSourcePlugs = container.sourcePlugs?.compactMap { mapJsonToDomainPlug(jsonPlug: $0, defaultSubunitAddress: musicAddress, guid: guid) } ?? []
        musicSubunitInfo.statusDescriptorInfoBlocks = container.statusDescriptorParsed?.compactMap { mapJsonToDomainInfoBlock(jsonBlock: $0, guid: guid) }
        return musicSubunitInfo
    }
    
    private func mapJsonToDomainInfoBlock(jsonBlock: JsonAVCInfoBlock?, guid: UInt64) -> AVCInfoBlockInfo? {
        guard let block = jsonBlock else { return nil }
        var domainBlock = AVCInfoBlockInfo()
        
        let typeString = block.type.hasPrefix("0x") ? String(block.type.dropFirst(2)) : block.type
        if let typeVal = UInt16(typeString, radix: 16), let infoType = InfoBlockType(rawValue: typeVal) {
            domainBlock.type = infoType
        } else {
            logger.warning("GUID 0x\(String(format: "%llX", guid)): Unknown or invalid InfoBlockType string: '\(block.type)'. Using .unknown.")
            domainBlock.type = .unknown
        }
        
        domainBlock.compoundLength = block.compoundLength ?? 0
        domainBlock.primaryFieldsLength = block.primaryFieldsLength ?? 0
        domainBlock.nestedBlocks = block.nestedBlocks?.compactMap { mapJsonToDomainInfoBlock(jsonBlock: $0, guid: guid) } ?? []
        domainBlock.parsedData = parsePrimaryFields(type: domainBlock.type, parsedDict: block.primaryFieldsParsed, guid: guid)
        return domainBlock
    }
    
    /// Parses the `primaryFieldsParsed` dictionary based on the `InfoBlockType`.
    private func parsePrimaryFields(type: InfoBlockType, parsedDict: [String: AnyCodable]?, guid: UInt64) -> AVCInfoBlockInfo.ParsedInfoBlockData? {
        guard let dict = parsedDict else { return .unknown } // Return unknown if no fields dict
        
        switch type {
        case .rawText:
            if let text = dict["text"]?.value as? String {
                return .rawText(text: text)
            } else {
                logger.warning("GUID 0x\(String(format: "%llX", guid)): Missing 'text' field for RawText InfoBlock.")
                return .unknown
            }
            
        case .generalMusicStatus:
            let info = GeneralMusicStatus(
                currentTransmitCapability: dict["currentTransmitCapability"]?.value as? Int,
                currentReceiveCapability: dict["currentReceiveCapability"]?.value as? Int,
                // Handle potential Int or UInt from JSON for large numbers
                currentLatencyCapability: (dict["currentLatencyCapability"]?.value as? UInt).flatMap(UInt32.init) ?? (dict["currentLatencyCapability"]?.value as? Int).flatMap(UInt32.init)
            )
            return .generalMusicStatus(info: info)
            
        case .routingStatus:
            let info = RoutingStatus(
                numberOfSubunitSourcePlugs: dict["numberOfSubunitSourcePlugs"]?.value as? Int,
                numberOfSubunitDestPlugs: dict["numberOfSubunitDestPlugs"]?.value as? Int,
                numberOfMusicPlugs: dict["numberOfMusicPlugs"]?.value as? Int
            )
            return .routingStatus(info: info)
            
        case .subunitPlugInfo:
            let info = SubunitPlugInfo(
                subunitPlugId: dict["subunitPlugId"]?.value as? Int,
                plugType: dict["plugType"]?.value as? Int,
                numberOfClusters: dict["numberOfClusters"]?.value as? Int,
                signalFormat: dict["signalFormat"]?.value as? Int,
                numberOfChannels: dict["numberOfChannels"]?.value as? Int
            )
            return .subunitPlugInfo(info: info)
            
        case .clusterInfo:
            var mappedSignals: [SignalInfo]? = nil
            if let signalsAny = dict["signals"]?.value as? [Any] {
                mappedSignals = signalsAny.compactMap { mapAnyToSignalInfo($0, guid: guid) }
            } else if dict["signals"] != nil {
                logger.warning("GUID 0x\(String(format: "%llX", guid)): 'signals' field in ClusterInfo is not an array.")
            }
            let info = ClusterInfo(
                streamFormat: dict["streamFormat"]?.value as? Int,
                portType: dict["portType"]?.value as? Int,
                numberOfSignals: dict["numberOfSignals"]?.value as? Int,
                signals: mappedSignals
            )
            return .clusterInfo(info: info)
            
        case .musicPlugInfo:
            let info = MusicPlugInfo(
                musicPlugId: dict["musicPlugId"]?.value as? Int,
                musicPlugType: dict["musicPlugType"]?.value as? Int,
                routingSupport: dict["routingSupport"]?.value as? Int,
                source: mapAnyToPlugEndpointInfo(dict["source"]?.value, guid: guid),
                destination: mapAnyToPlugEndpointInfo(dict["destination"]?.value, guid: guid)
            )
            return .musicPlugInfo(info: info)
            
        case .unknown:
            return .unknown // Type was already unknown
        }
    }
    
    /// Helper to map an `Any` dictionary (from AnyCodable) to `SignalInfo`.
    private func mapAnyToSignalInfo(_ anyValue: Any, guid: UInt64) -> SignalInfo? {
        guard let dict = anyValue as? [String: Any] else {
            logger.warning("GUID 0x\(String(format: "%llX", guid)): Expected dictionary for SignalInfo, got \(type(of: anyValue))")
            return nil
        }
        return SignalInfo(
            musicPlugId: dict["musicPlugId"] as? Int,
            streamLocation: dict["streamLocation"] as? Int,
            streamPosition: dict["streamPosition"] as? Int
        )
    }
    
    /// Helper to map an `Any` dictionary (from AnyCodable) to `PlugEndpointInfo`.
    private func mapAnyToPlugEndpointInfo(_ anyValue: Any?, guid: UInt64) -> PlugEndpointInfo? {
        guard let dict = anyValue as? [String: Any] else {
            // Don't warn if nil, only if wrong type
            if anyValue != nil {
                logger.warning("GUID 0x\(String(format: "%llX", guid)): Expected dictionary for PlugEndpointInfo, got \(type(of: anyValue!))")
            }
            return nil
        }
        return PlugEndpointInfo(
            plugFunctionType: dict["plugFunctionType"] as? Int,
            plugFunctionBlockId: dict["plugFunctionBlockId"] as? Int,
            plugId: dict["plugId"] as? Int,
            streamLocation: dict["streamLocation"] as? Int,
            streamPosition: dict["streamPosition"] as? Int
        )
    }
    
    // MARK: - Log Management
    /// Clears the log buffer displayed in the UI.
    func clearLogs() {
        self.uiLogEntries.removeAll()
        logger.info("-- Log display cleared by user --")
    }
    
    /// Exports the current log buffer content as a formatted string.
    /// - Returns: A string containing all log entries, formatted with timestamp, level, and message.
    func exportLogs() -> String {
        logger.info("Exporting \(self.uiLogEntries.count) displayed log entries.")
        // Define a date formatter for consistent timestamp output
        let dateFormatter = DateFormatter()
        dateFormatter.dateFormat = "yyyy-MM-dd HH:mm:ss.SSS" // ISO-like format
        
        let logText = uiLogEntries.map { entry in
            let timestampStr = dateFormatter.string(from: entry.timestamp)
            // Format: "YYYY-MM-DD HH:MM:SS.ms [LEVEL] Message"
            return "\(timestampStr) [\(entry.level.rawValue.uppercased())] \(entry.displayMessage)"
        }.joined(separator: "\n")
        
        return logText
    }
    
    func updateLogBufferSize(_ newSize: Int) {
        self.logDisplayBufferSize = newSize
        if uiLogEntries.count > logDisplayBufferSize {
            uiLogEntries.removeFirst(uiLogEntries.count - logDisplayBufferSize)
        }
        logger.info("UI log buffer size set to \(newSize)")
    }
    
    // MARK: - Hardware Interaction Methods (Called by XPC Handler)
    
    func performHardwareSampleRateSet(guid: UInt64, rate: Double) async -> Bool {
        logger.info("--> Hardware CMD: Set Sample Rate \(rate) for GUID 0x\(String(format: "%llX", guid))")
        // TODO: Implement FWAEngine_SendCommand or specific C API call for setting rate
        do {
            try await Task.sleep(nanoseconds: 100_000_000) // Simulate delay
        } catch {
            logger.error("Task.sleep failed: \(error.localizedDescription)")
            return false
        }
        logger.warning("--> Hardware CMD: Set Sample Rate - STUBBED SUCCESS")
        return true // Placeholder
    }
    
    func performHardwareClockSourceSet(guid: UInt64, sourceID: UInt32) async -> Bool {
        logger.info("--> Hardware CMD: Set Clock Source \(sourceID) for GUID 0x\(String(format: "%llX", guid))")
        // TODO: Implement C API call
        do {
            try await Task.sleep(nanoseconds: 50_000_000)
        } catch {
            logger.error("Task.sleep failed: \(error.localizedDescription)")
            return false
        }
        logger.warning("--> Hardware CMD: Set Clock Source - STUBBED SUCCESS")
        return true
    }
    
    func performHardwareVolumeSet(guid: UInt64, scope: UInt32, element: UInt32, value: Float) async -> Bool {
        logger.info("--> Hardware CMD: Set Volume \(value) for GUID 0x\(String(format: "%llX", guid)), Scope \(scope), Elem \(element)")
        // TODO: Implement C API call
        do {
            try await Task.sleep(nanoseconds: 30_000_000)
        } catch {
            logger.error("Task.sleep failed: \(error.localizedDescription)")
            return false
        }
        logger.warning("--> Hardware CMD: Set Volume - STUBBED SUCCESS")
        return true
    }
    
    func performHardwareMuteSet(guid: UInt64, scope: UInt32, element: UInt32, state: Bool) async -> Bool {
        logger.info("--> Hardware CMD: Set Mute \(state) for GUID 0x\(String(format: "%llX", guid)), Scope \(scope), Elem \(element)")
        // TODO: Implement C API call
        do {
            try await Task.sleep(nanoseconds: 30_000_000)
        } catch {
            logger.error("Task.sleep failed: \(error.localizedDescription)")
            return false
        }
        logger.warning("--> Hardware CMD: Set Mute - STUBBED SUCCESS")
        return true
    }
    
    func performHardwareStartIO(guid: UInt64) async -> Bool {
        logger.info("--> Hardware CMD: Start IO for GUID 0x\(String(format: "%llX", guid))")
        // TODO: Implement interaction with FWAIsoch via C API or direct call
        do {
            try await Task.sleep(nanoseconds: 150_000_000) // Simulate potentially longer setup
        } catch {
            logger.error("Task.sleep failed: \(error.localizedDescription)")
            return false
        }
        logger.warning("--> Hardware CMD: Start IO - STUBBED SUCCESS")
        return true // Placeholder
    }
    
    func performHardwareStopIO(guid: UInt64) async {
        logger.info("--> Hardware CMD: Stop IO for GUID 0x\(String(format: "%llX", guid))")
        // TODO: Implement interaction with FWAIsoch via C API or direct call
        do {
            try await Task.sleep(nanoseconds: 50_000_000)
        } catch {
            logger.error("Task.sleep failed: \(error.localizedDescription)")
        }
        logger.warning("--> Hardware CMD: Stop IO - STUBBED")
    }
    
    // MARK: - Driver Installation
    /// Kick off the AppleScript‐based installer.
    func installDriverFromBundle() async {
        logger.info("🚀 User initiated driver installation...")
        do  { try await installFWADriver() } // Call the global function }
        catch {
            logger.error("Driver installation failed: \(error.localizedDescription)")
        }
        
    }
    
    // MARK: - Daemon Install Status & Auto-Connect
    func checkDaemonStatus() {
        let status = SMAppService.daemon(plistName: "FWADaemon.plist").status
        logger.trace("Checking daemon status: \(status)") // Log every check
        // Only update and potentially trigger connect if status changed or not connected
        if status != daemonInstallStatus || (status == .enabled && !xpcManager.isConnected) {
            logger.info("Daemon status changed to \(status) (was \(daemonInstallStatus))")
            daemonInstallStatus = status
            showDaemonInstallPrompt = (status != .enabled && status != .notFound) // Show prompt only if requires approval or disabled
            // *** Automatic Connection Logic ***
            if status == .enabled && !xpcManager.isConnected {
                logger.info("Daemon is enabled and XPC is not connected. Attempting automatic connection...")
                xpcManager.connect(reason: "Automatic connection on daemon enabled status") // Pass reason
                stopDaemonStatusTimer() // Stop polling once enabled and connected
            } else if status != .enabled && daemonStatusCheckTimer == nil {
                // If status is not enabled, ensure timer is running (or start it)
                logger.info("Daemon not enabled (\(status)). Starting status check timer.")
                startDaemonStatusTimer()
            } else if status == .enabled && xpcManager.isConnected {
                // If daemon is enabled and we are connected, stop the timer
                stopDaemonStatusTimer()
            }
        } else if status == .enabled && xpcManager.isConnected {
            // Also stop timer if status is already enabled and we are connected (e.g., on initial check)
            stopDaemonStatusTimer()
        }
    }
    @MainActor
    private func startDaemonStatusTimer() {
        guard daemonStatusCheckTimer == nil else { return } // Don't start multiple timers
        logger.info("Starting daemon status check timer (interval: 5s).")
        // Schedule timer on the main run loop
        daemonStatusCheckTimer = Timer.scheduledTimer(withTimeInterval: 5.0, repeats: true) { [weak self] _ in
            Task { @MainActor [weak self] in // Ensure check runs on main actor
                // Only check if the app is active to avoid unnecessary work
                if NSApplication.shared.isActive {
                    self?.checkDaemonStatus()
                } else {
                    self?.logger.trace("App inactive, skipping periodic daemon status check.")
                }
            }
        }
        // Allow timer to run even when UI is tracking (e.g., scrolling)
        RunLoop.current.add(daemonStatusCheckTimer!, forMode: RunLoopMode.commonModes)
    }
    @MainActor
    private func stopDaemonStatusTimer() {
        if daemonStatusCheckTimer != nil {
            logger.info("Stopping daemon status check timer.")
            daemonStatusCheckTimer?.invalidate()
            daemonStatusCheckTimer = nil
        }
    }
    // Setup observer for app activation/deactivation
    private func setupAppLifecycleObserver() {
        NotificationCenter.default.addObserver(forName: NSApplication.didBecomeActiveNotification, object: nil, queue: .main) { [weak self] _ in
            guard let self = self else { return }
            self.logger.debug("App became active.")
            // If we were in the background, trigger an immediate check upon activation
            // especially if the daemon isn't enabled/connected yet.
            if self.wasInBackground && self.daemonInstallStatus != .enabled {
                self.logger.info("App activated, performing immediate daemon status check.")
                self.checkDaemonStatus()
            }
            self.wasInBackground = false
        }
        NotificationCenter.default.addObserver(forName: NSApplication.willResignActiveNotification, object: nil, queue: .main) { [weak self] _ in
            self?.logger.debug("App resigning active.")
            self?.wasInBackground = true
        }
    }
} // End Class DeviceManager
