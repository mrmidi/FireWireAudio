import Foundation
import Logging
import Combine

struct UILogEntry: Identifiable, Equatable {
    let id = UUID()
    let timestamp: Date = Date()
    let level: Logger.Level
    let message: Logger.Message
    let metadata: Logger.Metadata?
    let source: String
    let file: String
    let function: String
    let line: UInt
    var displayMessage: String { message.description }
}

let logEntrySubject = PassthroughSubject<UILogEntry, Never>()

struct InMemoryLogHandler: LogHandler {
    private let label: String
    var metadata: Logger.Metadata = [:]
    var logLevel: Logger.Level = .trace // Default log level
    // Removed forwardingHandler property

    // Simplified init - no forwarding parameter
    init(label: String, initialLevel: Logger.Level = .trace) {
        self.label = label
        self.logLevel = initialLevel
    }

    subscript(metadataKey key: String) -> Logger.Metadata.Value? {
        get { metadata[key] }
        set {
            metadata[key] = newValue
            // Removed updating forwarding handler
        }
    }

    func log(level: Logger.Level,
             message: Logger.Message,
             metadata: Logger.Metadata?,
             source: String,
             file: String,
             function: String,
             line: UInt) {

        // Combine metadata
        let effectiveMetadata = !self.metadata.isEmpty ? self.metadata.merging(metadata ?? [:], uniquingKeysWith: { $1 }) : metadata

        // Create the entry for the UI
        let uiEntry = UILogEntry(
            level: level,
            message: message,
            metadata: effectiveMetadata,
            source: source,
            file: file,
            function: function,
            line: line
        )

        // Publish the entry for subscribers (like DeviceManager)
        logEntrySubject.send(uiEntry)

        // Removed forwarding call
    }
}
