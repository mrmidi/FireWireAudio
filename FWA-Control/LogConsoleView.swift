// === FWA-Control/LogConsoleView.swift ===

import SwiftUI
import UniformTypeIdentifiers

struct LogConsoleView: View {
    @EnvironmentObject var manager: DeviceManager // Get the manager from the environment

    // State for filtering and UI controls
    @State private var selectedMinLevel: SwiftLogLevel = .trace // Show all by default
    @State private var searchText: String = ""
    @State private var isAutoScrollEnabled: Bool = true
    @State private var showExportSheet = false
    @State private var logExportContent: String = ""

    // Computed property to filter logs based on state
    var filteredLogs: [LogEntry] {
        manager.logs.filter { entry in
            // Filter by level (entry level must be >= selectedMinLevel)
            let levelMatch = entry.level.rawValue >= selectedMinLevel.rawValue

            // Filter by search text (if not empty)
            let searchMatch = searchText.isEmpty ||
                              entry.message.localizedCaseInsensitiveContains(searchText) ||
                              entry.level.description.localizedCaseInsensitiveContains(searchText)

            return levelMatch && searchMatch
        }
    }

    var body: some View {
        VStack(spacing: 0) {
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(spacing: 0, pinnedViews: [.sectionHeaders]) {
                        Section(header: logToolbar) {
                            ForEach(filteredLogs) { entry in
                                logEntryRow(entry)
                                    .id(entry.id)
                            }
                        }
                    }
                }
                // Jump-to-Latest floating button
                if isAutoScrollEnabled {
                    Button(action: {
                        if let lastID = filteredLogs.last?.id {
                            proxy.scrollTo(lastID, anchor: .bottom)
                        }
                    }) {
                        Image(systemName: "arrow.down.circle.fill")
                            .font(.largeTitle)
                            .opacity(0.6)
                    }
                    .padding()
                    .accessibilityLabel("Jump to latest log")
                }
            }
            .border(Color.gray.opacity(0.3), width: 1)
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
                ForEach(SwiftLogLevel.allCasesSorted, id: \.self) { level in
                    Text(level.description).tag(level)
                }
            }
            .pickerStyle(.menu)
            .frame(maxWidth: 120)

            Spacer()

            Toggle("Auto-Scroll", isOn: $isAutoScrollEnabled)
                .toggleStyle(.checkbox)

            Button {
                logExportContent = manager.exportLogs()
                showExportSheet = true
            } label: {
                Label("Export", systemImage: "square.and.arrow.up")
            }
            .help("Export visible logs to a text file")

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

    // Helper function to format a log entry row
    @ViewBuilder
    private func logEntryRow(_ entry: LogEntry) -> some View {
        HStack(alignment: .top, spacing: 8) {
            Text(entry.timestamp, format: .dateTime.hour().minute().second().secondFraction(.fractional(3)))
                .font(.system(size: 10, design: .monospaced))
                .foregroundColor(.secondary)
                .frame(minWidth: 75, alignment: .leading)

            Text("[\(entry.level.description)]")
                .font(.system(size: 10, design: .monospaced).weight(.medium))
                .foregroundColor(logColor(entry.level))
                .frame(minWidth: 50, alignment: .leading)

            Text(entry.message)
                .font(.body.monospaced())
                .accessibilityLabel("Log message: \(entry.message)")
                .textSelection(.enabled)
                .lineLimit(nil)
                .layoutPriority(1)

            Spacer(minLength: 0)
        }
        .padding(.vertical, 2)
    }

    // Helper function for log level color
    private func logColor(_ level: SwiftLogLevel) -> Color {
        switch level {
            case .trace: return .gray
            case .debug: return .secondary
            case .info: return .primary
            case .warn: return .orange
            case .error: return .red
            case .critical: return .red.opacity(0.8)
            case .off: return .clear
        }
    }

    private func formattedTimestamp() -> String {
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyyMMdd-HHmmss"
        return formatter.string(from: Date())
    }
}

// Add CaseIterable and Identifiable conformance to SwiftLogLevel for Picker
extension SwiftLogLevel: CaseIterable, Identifiable {
    public var id: Int32 { self.rawValue }

    // Manually list all cases so the compiler can synthesize conformance
    static var allCases: [SwiftLogLevel] = [
        .trace,
        .debug,
        .info,
        .warn,
        .error,
        .critical,
        .off
    ]

    static var allCasesSorted: [SwiftLogLevel] {
        allCases
            .filter { $0 != .off }
            .sorted { $0.rawValue < $1.rawValue }
    }
}

struct LogDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.plainText] }
    var content: String

    init(content: String) {
        self.content = content
    }

    init(configuration: ReadConfiguration) throws {
        guard let data = configuration.file.regularFileContents,
              let string = String(data: data, encoding: .utf8)
        else {
            throw CocoaError(.fileReadCorruptFile)
        }
        content = string
    }

    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        return FileWrapper(regularFileWithContents: content.data(using: .utf8)!)
    }
}

// MARK: - Preview
struct LogConsoleView_Previews: PreviewProvider {
    static var previews: some View {
        let previewManager = DeviceManager()
        previewManager.logs = [
            LogEntry(level: .info, message: "Engine Starting..."),
            LogEntry(level: .debug, message: "Callback registered."),
            LogEntry(level: .warn, message: "Device X reported low voltage."),
            LogEntry(level: .trace, message: "Polling device Y status."),
            LogEntry(level: .error, message: "Failed to decode response from device Z."),
            LogEntry(level: .critical, message: "C API Engine crashed unexpectedly!"),
        ]
        for i in 1...20 {
            previewManager.logs.append(LogEntry(level: .trace, message: "Background task \(i) completed."))
        }
        previewManager.logs.append(LogEntry(level: .info, message: "Initialization complete."))

        return LogConsoleView()
            .environmentObject(previewManager)
            .frame(height: 400)
    }
}
