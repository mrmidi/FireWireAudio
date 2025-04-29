// === FWA-Control/LogConsoleView.swift ===

import SwiftUI
import UniformTypeIdentifiers
import Logging

extension Logger.Level {
    /// A numeric rank so we can compare severity.
    fileprivate var severity: Int {
        switch self {
        case .trace:    return 0
        case .debug:    return 1
        case .info:     return 2
        case .notice:   return 3
        case .warning:  return 4
        case .error:    return 5
        case .critical: return 6
        }
    }
}

extension Logger.Level: Comparable {
    public static func < (lhs: Logger.Level, rhs: Logger.Level) -> Bool {
        lhs.severity < rhs.severity
    }
}

struct LogConsoleView: View {
    @EnvironmentObject var manager: DeviceManager
    private let logger = AppLoggers.app
    @State private var selectedMinLevel: Logger.Level = .info
    @State private var searchText: String = ""
    @State private var isAutoScrollEnabled: Bool = true
    @State private var showExportSheet = false
    @State private var logExportContent: String = ""

    var filteredLogs: [UILogEntry] {
        manager.uiLogEntries.filterBy { entry in
            let levelMatch = entry.level >= selectedMinLevel
            let searchMatch = searchText.isEmpty
                          || entry.displayMessage.localizedCaseInsensitiveContains(searchText)
                          || entry.level.rawValue.localizedCaseInsensitiveContains(searchText)
                          || entry.source.localizedCaseInsensitiveContains(searchText)
            return levelMatch && searchMatch
        }
    }

    var body: some View {
        VStack(spacing: 0) {
            logToolbar
            Divider()
            ScrollViewReader { proxy in
                List {
                    ForEach(filteredLogs) { entry in
                        logEntryRow(entry)
                            .id(entry.id)
                    }
                    .onChange(of: filteredLogs.count) { _ in
                        scrollToBottomIfNeeded(proxy: proxy)
                    }
                    .onAppear {
                        scrollToBottomIfNeeded(proxy: proxy)
                    }
                }
                .listStyle(.plain)
            }
        }
        .fileExporter(
             isPresented: $showExportSheet,
             document: LogDocument(content: logExportContent),
             contentType: .plainText,
             defaultFilename: "fwa-control-log-\(formattedTimestamp()).txt"
         ) { result in
             switch result {
             case .success(let url):
                 logger.info("Log exported successfully to: \(url.path)")
             case .failure(let error):
                 logger.error("Log export failed: \(error.localizedDescription)")
             }
         }
    }

    @ViewBuilder
    private var logToolbar: some View {
        HStack {
            Picker("Level:", selection: $selectedMinLevel) {
                ForEach(Logger.Level.allCases, id: \.self) { level in
                    Text(level.rawValue.uppercased()).tag(level)
                }
            }
            .pickerStyle(.menu)
            .frame(maxWidth: 120)
            .help("Show logs at this level or higher")

            TextField("Search Logs", text: $searchText)
                .textFieldStyle(.roundedBorder)
                .frame(maxWidth: 250)

            Spacer()

            Toggle("Auto-Scroll", isOn: $isAutoScrollEnabled)
                .toggleStyle(.checkbox)
                .onChange(of: isAutoScrollEnabled) { enabled in
                    if enabled {
                        // No-op: handled in list's onChange
                    }
                }

            Button {
                logger.info("Export Logs button clicked. Preparing content...")
                logExportContent = manager.exportLogs(filtered: filteredLogs)
                if logExportContent.isEmpty {
                    logger.warning("No log entries to export.")
                } else {
                    showExportSheet = true
                }
            } label: {
                Label("Export", systemImage: "square.and.arrow.up")
            }
            .help("Export currently displayed logs to a text file")
            .disabled(filteredLogs.isEmpty)

            Button(role: .destructive) {
                logger.info("Clear Logs button clicked.")
                manager.clearLogs()
            } label: {
                Label("Clear", systemImage: "trash")
            }
            .help("Clear all log entries")
            .disabled(manager.uiLogEntries.isEmpty)
        }
        .padding(.horizontal)
        .padding(.vertical, 8)
        .background(.thinMaterial)
    }

    private func scrollToBottomIfNeeded(proxy: ScrollViewProxy) {
        if isAutoScrollEnabled, let lastID = filteredLogs.last?.id {
            DispatchQueue.main.async {
                withAnimation(.easeOut(duration: 0.2)) {
                    proxy.scrollTo(lastID, anchor: .bottom)
                }
            }
        }
    }

    @ViewBuilder
    private func logEntryRow(_ entry: UILogEntry) -> some View {
        HStack(alignment: .top, spacing: 8) {
            Text(entry.timestamp, format: .dateTime.hour().minute().second().secondFraction(.fractional(3)))
                .font(.system(size: 10, design: .monospaced))
                .foregroundColor(.secondary)
                .frame(minWidth: 75, alignment: .leading)

            Text("[\(entry.level.rawValue.uppercased())]")
                .font(.system(size: 10, design: .monospaced).weight(.semibold))
                .foregroundColor(logColor(entry.level))
                .frame(minWidth: 65, alignment: .leading)

            Text(entry.displayMessage)
                .font(.system(size: 11, design: .monospaced))
                .textSelection(.enabled)
                .lineLimit(nil)
                .layoutPriority(1)

            Spacer(minLength: 0)
        }
    }

    private func logColor(_ level: Logger.Level) -> Color {
        switch level {
            case .trace:    return .gray
            case .debug:    return .secondary
            case .info:     return .blue
            case .notice:   return .accentColor
            case .warning:  return .orange
            case .error:    return .red
            case .critical: return .red.opacity(0.8)
        }
    }

    private func formattedTimestamp() -> String {
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyyMMdd-HHmmss"
        return formatter.string(from: Date())
    }
}

struct LogDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.plainText] }
    var content: String
    init(content: String) { self.content = content }
    init(configuration: ReadConfiguration) throws {
        guard let data = configuration.file.regularFileContents,
              let string = String(data: data, encoding: .utf8)
        else { throw CocoaError(.fileReadCorruptFile) }
        content = string
    }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        return FileWrapper(regularFileWithContents: content.data(using: .utf8)!)
    }
}

// MARK: - Preview
struct LogConsoleView_Previews: PreviewProvider {
    static var previews: some View {
        // Create a preview manager and manually add some UILogEntry items
        let previewManager = DeviceManager()
        previewManager.uiLogEntries = [
            UILogEntry(level: .info, message: "Engine Starting...", metadata: nil, source: "Manager", file: #file, function: #function, line: #line),
            UILogEntry(level: .debug, message: "Callback registered.", metadata: nil, source: "Manager", file: #file, function: #function, line: #line),
            UILogEntry(level: .warning, message: "Device X reported low voltage.", metadata: nil, source: "C_API", file: #file, function: #function, line: #line),
            UILogEntry(level: .trace, message: "Polling device Y status.", metadata: nil, source: "Manager", file: #file, function: #function, line: #line),
            UILogEntry(level: .error, message: "Failed to decode response from device Z.", metadata: nil, source: "Manager", file: #file, function: #function, line: #line),
            UILogEntry(level: .critical, message: "C API Engine crashed unexpectedly!", metadata: nil, source: "C_API", file: #file, function: #function, line: #line),
        ]
        for i in 1...20 {
             previewManager.uiLogEntries.append(UILogEntry(level: .trace, message: "Background task \(i) completed.", metadata: nil, source: "Manager", file: #file, function: #function, line: #line))
        }
         previewManager.uiLogEntries.append(UILogEntry(level: .info, message: "Initialization complete.", metadata: nil, source: "App", file: #file, function: #function, line: #line))

        return LogConsoleView()
            .environmentObject(previewManager)
            .frame(height: 400)
    }
}

private extension Sequence {
    /// A non-ambiguous version of `filter(_:)` that always uses the
    /// classic `(Element) -> Bool` overload.
    func filterBy(_ isIncluded: (Element) -> Bool) -> [Element] {
        filter(isIncluded)
    }
}