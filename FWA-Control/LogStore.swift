// LogStore.swift
import Foundation
import Combine
import SwiftUI // For AppStorage
import Logging
import Collections // <--- IMPORT

@MainActor
final class LogStore: ObservableObject {

    // --- CHANGE TYPE TO Deque ---
    @Published var uiLogEntries: Deque<UILogEntry> = []
    // --------------------------

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
        // --- FIX: Subscribe to the broadcaster's subject ---
        LogBroadcaster.shared.subject
            .receive(on: DispatchQueue.main)
            .sink { [weak self] entry in
                // --- ADD THIS LINE ---
                self?.logger.debug(">>>> LogStore received entry via Combine sink: \(entry.id) Level: \(entry.level) Source: \(entry.source)")
                // ---------------------
                self?.addLogEntry(entry)
            }
            .store(in: &cancellables)
        // --- End FIX ---
        logger.debug("LogStore subscribed to LogBroadcaster.")
    }

    private func addLogEntry(_ entry: UILogEntry) {
        // --- Use append for Deque (same as Array) ---
        uiLogEntries.append(entry)
        // -------------------------------------------
        applyBufferSize() // Trim if needed after adding
    }

    private func applyBufferSize() {
        // --- Use Deque's efficient popFirst ---
        while uiLogEntries.count > logDisplayBufferSize {
             uiLogEntries.popFirst() // O(1) operation!
        }
        // -------------------------------------
    }

    // MARK: - Public Actions
    func clearLogs() {
        self.uiLogEntries.removeAll()
        logger.info("-- Log display cleared by user (via LogStore) --")
    }

    // --- Export methods need to work with Deque ---
    func exportLogs() -> String {
        exportLogs(filtered: Array(self.uiLogEntries)) // Convert Deque to Array for filtering/mapping if needed
    }

    func exportLogs(filtered entries: [UILogEntry]) -> String { // Keep accepting Array if filter logic expects it
        logger.info("LogStore exporting \(entries.count) log entries.")
        let dateFormatter = DateFormatter()
        dateFormatter.dateFormat = "yyyy-MM-dd HH:mm:ss.SSS"
        return entries.map { entry in // Map over the Array copy
            let timestampStr = dateFormatter.string(from: entry.timestamp)
            return "\(timestampStr) [\(entry.level.rawValue.uppercased())] \(entry.displayMessage)"
        }.joined(separator: "\n")
    }
    // -------------------------------------------
}
