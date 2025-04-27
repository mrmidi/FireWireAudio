// === FWA-Control/LogConsoleView.swift ===

import SwiftUI
import UniformTypeIdentifiers
import Logging // Add Logging import

struct LogConsoleView: View {
    @EnvironmentObject var manager: DeviceManager // Get the manager from the environment

    // State for filtering and UI controls
    @State private var selectedMinLevel: Logger.Level = .trace // Use Logger.Level
    @State private var searchText: String = ""
    @State private var isAutoScrollEnabled: Bool = true
    @State private var showExportSheet = false
    @State private var logExportContent: String = ""

    // Computed property to filter logs based on state
    var filteredLogs: [UILogEntry] {
        manager.uiLogEntries.filter { entry in
            // Filter by level (entry level must be >= selectedMinLevel)
            let levelMatch = entry.level >= selectedMinLevel

            // Filter by search text (if not empty)
            let searchMatch = searchText.isEmpty ||
                              entry.displayMessage.localizedCaseInsensitiveContains(searchText) ||
                              entry.level.rawValue.localizedCaseInsensitiveContains(searchText) ||
                              entry.source.localizedCaseInsensitiveContains(searchText)

            return levelMatch && searchMatch
        }
    }

    var body: some View {
        VStack(spacing: 0) {
            ScrollViewReader { proxy in
                List {
                    ForEach(filteredLogs) { entry in
                        logEntryRow(entry)
                            .id(entry.id)
                    }
                    .onChange(of: filteredLogs) { _ in
                        if isAutoScrollEnabled, let lastID = filteredLogs.last?.id {
                            DispatchQueue.main.async {
                                proxy.scrollTo(lastID, anchor: .bottom)
                            }
                        }
                    }
                }
                .listStyle(.plain)
                .border(Color.gray.opacity(0.3), width: 1)
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
                 print("Log exported successfully to: \(url)")
             case .failure(let error):
                 print("Log export failed: \(error.localizedDescription)")
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

            TextField("Search Logs", text: $searchText)
                .textFieldStyle(.roundedBorder)
                .frame(maxWidth: 200)

            Spacer()

            Toggle("Auto-Scroll", isOn: $isAutoScrollEnabled)
                .toggleStyle(.checkbox)

            Button {
                logExportContent = manager.exportLogs()
                showExportSheet = true
            } label: {
                Label("Export", systemImage: "square.and.arrow.up")
            }
            .help("Export displayed logs to a text file")

            Button(role: .destructive) {
                manager.clearLogs()
            } label: {
                Label("Clear", systemImage: "trash")
            }
            .help("Clear the log display")
        }
        .padding(.horizontal)
        .padding(.vertical, 8)
        .background(.thinMaterial)
    }

    // Helper function to format a log entry row using UILogEntry
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

    // Helper function for log level color based on Logger.Level
    private func logColor(_ level: Logger.Level) -> Color {
        switch level {
            case .trace:    return .gray
            case .debug:    return .secondary
            case .info:     return .blue
            case .notice:   return .accentColor
            case .warning:  return .orange
            case .error:    return .red
            case .critical: return .red.opacity(0.8)
            default:        return .clear
        }
    }

    private func formattedTimestamp() -> String {
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyyMMdd-HHmmss"
        return formatter.string(from: Date())
    }
}

// Add CaseIterable conformance to Logger.Level for Picker
extension Logger.Level: CaseIterable {
    public static var allCases: [Logger.Level] = [
        .trace,
        .debug,
        .info,
        .notice,
        .warning,
        .error,
        .critical
    ]
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
