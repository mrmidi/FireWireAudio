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


struct LogConsoleView: View {
    @EnvironmentObject var uiManager: UIManager
    private var logStore: LogStore? { uiManager.logStore }
    private let logger = AppLoggers.app
    @State private var selectedMinLevel: Logger.Level = .info
    @State private var searchText: String = ""
    @State private var isAutoScrollEnabled: Bool = true
    @State private var showExportSheet = false
    @State private var logExportContent: String = ""

    var filteredLogs: Deque<UILogEntry> {
        (logStore?.uiLogEntries ?? []).filter { entry in
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
             document: LogDocument(content: logStore?.exportLogs(filtered: Array(filteredLogs)) ?? ""),
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
                logExportContent = logStore?.exportLogs(filtered: filteredLogs) ?? ""
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
                logStore?.clearLogs()
            } label: {
                Label("Clear", systemImage: "trash")
            }
            .help("Clear all log entries")
            .disabled(logStore?.uiLogEntries.isEmpty ?? true)
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
        // 1. Create LogStore and add dummy data
        let previewLogStore = LogStore()
        previewLogStore.uiLogEntries = [
            UILogEntry(level: .info, message: "Engine Starting...", metadata: nil, source: "Preview", file: #file, function: #function, line: #line),
            UILogEntry(level: .debug, message: "Callback registered.", metadata: nil, source: "Preview", file: #file, function: #function, line: #line),
            UILogEntry(level: .warning, message: "Device X reported low voltage.", metadata: nil, source: "Preview", file: #file, function: #function, line: #line),
            UILogEntry(level: .info, message: "Initialization complete.", metadata: nil, source: "App", file: #file, function: #function, line: #line),
        ]
        // 2. Create UIManager, injecting the LogStore (and nil for others)
        let previewUIManager = UIManager(
            engineService: nil,
            systemServicesManager: nil,
            logStore: previewLogStore
        )
        // 3. Provide UIManager to the view
        return LogConsoleView()
            .environmentObject(previewUIManager)
            .frame(height: 400)
    }
}

