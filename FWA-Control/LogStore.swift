// LogStore.swift
import Foundation
import Combine
import SwiftUI // For AppStorage
import Logging
import SwiftCollectionsManual // For Deque

@MainActor
final class LogStore: ObservableObject {

    @Published private(set) var uiLogEntries = Deque<UILogEntry>()

    @AppStorage("settings.logDisplayBufferSize") var logDisplayBufferSize: Int = 500 {
        didSet {
            applyBufferSize() // Apply immediately when changed via Settings
        }
    }

    private var cancellables = Set<AnyCancellable>()
    private let logger = AppLoggers.app // Use a relevant logger

    init() {
        logger.info("LogStore initialized.")
        subscribeToLogSource()
    }

    private func subscribeToLogSource() {
        LogBroadcaster.shared.subject
            .collect(.byTime(DispatchQueue.main, .milliseconds(50))) // 👈 group
            .sink { [weak self] batch in
                guard let self, !batch.isEmpty else { return }
                self.addLogEntries(batch)                            // ⇢ one UI diff
            }
            .store(in: &cancellables)
    }
    
    @MainActor
    private func addLogEntries(_ entries: [UILogEntry]) {
        uiLogEntries.append(contentsOf: entries)
        if uiLogEntries.count > logDisplayBufferSize {
            uiLogEntries.removeFirst(uiLogEntries.count - logDisplayBufferSize)
        }
    }

    private func addLogEntry(_ entry: UILogEntry) {
        uiLogEntries.append(entry)
        if uiLogEntries.count > logDisplayBufferSize {
            uiLogEntries.removeFirst(uiLogEntries.count - logDisplayBufferSize)
        }
        applyBufferSize() // Trim if needed after adding
    }

    private func applyBufferSize() {
        if uiLogEntries.count > logDisplayBufferSize {
            uiLogEntries.removeFirst(uiLogEntries.count - logDisplayBufferSize)
        }
    }

    // MARK: - Public Actions
    func clearLogs() {
        self.uiLogEntries.removeAll()
        logger.info("-- Log display cleared by user (via LogStore) --")
    }

    func exportLogs() -> String {
        exportLogs(filtered: Array(self.uiLogEntries))
    }

    func exportLogs(filtered entries: [UILogEntry]) -> String {
        logger.info("LogStore exporting \(entries.count) log entries.")
        let dateFormatter = DateFormatter()
        dateFormatter.dateFormat = "yyyy-MM-dd HH:mm:ss.SSS"
        return entries.map { entry in
            let timestampStr = dateFormatter.string(from: entry.timestamp)
            return "\(timestampStr) [\(entry.level.rawValue.uppercased())] \(entry.displayMessage)"
        }.joined(separator: "\n")
    }
}
