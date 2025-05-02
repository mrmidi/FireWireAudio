import Foundation
import Logging
@preconcurrency import Combine // Keep @preconcurrency

// --- Define UILogEntry ---
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

// --- NEW: LogBroadcaster Actor ---
actor LogBroadcaster {
    // Shared instance
    static let shared = LogBroadcaster()

    // Subject is now internal and MainActor isolated
    @MainActor let subject = PassthroughSubject<UILogEntry, Never>()

    // Private init for singleton
    private init() {}

    // Async method to receive logs from any context
    func broadcast(entry: UILogEntry) async {
        // Switch to main actor to send safely
        await MainActor.run {
            subject.send(entry)
        }
    }
}
// --- End LogBroadcaster ---

struct InMemoryLogHandler: LogHandler {
    private let label: String
    var metadata: Logger.Metadata = [:]
    var logLevel: Logger.Level = .trace

    init(label: String, initialLevel: Logger.Level = .trace) {
        self.label = label
        self.logLevel = initialLevel
    }

    subscript(metadataKey key: String) -> Logger.Metadata.Value? {
        get { metadata[key] }
        set { metadata[key] = newValue }
    }

    // log runs on caller's context
    func log(level: Logger.Level,
             message: Logger.Message,
             metadata: Logger.Metadata?,
             source: String,
             file: String,
             function: String,
             line: UInt) {

        // Create the entry
        let effectiveMetadata = !self.metadata.isEmpty ? self.metadata.merging(metadata ?? [:], uniquingKeysWith: { $1 }) : metadata
        let uiEntry = UILogEntry(
            level: level,
            message: message,
            metadata: effectiveMetadata,
            source: source,
            file: file,
            function: function,
            line: line
        )

        // --- FIX: Send via Actor ---
        // Use Task.detached for fire-and-forget without blocking caller
        Task.detached {
             await LogBroadcaster.shared.broadcast(entry: uiEntry)
        }
        // --- End FIX ---
    }
}
