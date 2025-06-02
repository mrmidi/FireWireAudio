// === FWA-Control/Views/SharedOverviewComponents.swift ===

import SwiftUI

// MARK: - StatusIndicatorCard

struct StatusIndicatorCard: View {
    let title: String
    let value: String
    let icon: String
    let color: Color
    
    var body: some View {
        VStack(spacing: 8) {
            HStack {
                Image(systemName: icon)
                    .foregroundColor(color)
                    .font(.title3)
                Spacer()
            }
            
            VStack(alignment: .leading, spacing: 2) {
                Text(value)
                    .font(.title2)
                    .fontWeight(.semibold)
                
                Text(title)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .padding()
        .background(Color(NSColor.quaternaryLabelColor).opacity(0.3), in: RoundedRectangle(cornerRadius: 12))
    }
}

// MARK: - ExpandableSection

struct ExpandableSection<Content: View>: View {
    let title: String
    let icon: String
    let isExpanded: Bool
    let content: Content
    let onToggle: (Bool) -> Void
    
    @State private var isHovered = false
    
    init(title: String, 
         icon: String, 
         isExpanded: Bool, 
         onToggle: @escaping (Bool) -> Void,
         @ViewBuilder content: () -> Content) {
        self.title = title
        self.icon = icon
        self.isExpanded = isExpanded
        self.onToggle = onToggle
        self.content = content()
    }
    
    var body: some View {
        VStack(spacing: 0) {
            sectionHeader
            
            if isExpanded {
                content
                    .padding()
                    .background(.thinMaterial, in: RoundedRectangle(cornerRadius: 12))
                    .padding(.top, 8)
            }
        }
        .animation(.easeInOut(duration: 0.3), value: isExpanded)
    }
    
    @ViewBuilder
    private var sectionHeader: some View {
        Button {
            onToggle(!isExpanded)
        } label: {
            HStack {
                Label(title, systemImage: icon)
                    .font(.headline)
                    .foregroundStyle(.primary)
                
                Spacer()
                
                Image(systemName: "chevron.right")
                    .font(.caption)
                    .fontWeight(.semibold)
                    .foregroundStyle(.secondary)
                    .rotationEffect(.degrees(isExpanded ? 90 : 0))
            }
            .padding()
            .background(headerBackground, in: RoundedRectangle(cornerRadius: 12))
        }
        .buttonStyle(.plain)
        .onHover { hovering in
            withAnimation(.easeInOut(duration: 0.2)) {
                isHovered = hovering
            }
        }
        .help("Click to \(isExpanded ? "collapse" : "expand") \(title.lowercased())")
    }
    
    private var headerBackground: some ShapeStyle {
        if isHovered {
            return AnyShapeStyle(Color(NSColor.controlAccentColor).opacity(0.1))
        } else {
            return AnyShapeStyle(.regularMaterial)
        }
    }
}

// MARK: - SampleRateChip

struct SampleRateChip: View {
    let rate: SampleRate
    let isActive: Bool
    let onSelect: () -> Void
    
    @State private var isHovered = false
    
    var body: some View {
        Button(action: onSelect) {
            Text(rate.description)
                .font(.caption)
                .fontWeight(.medium)
                .padding(.horizontal, 12)
                .padding(.vertical, 6)
                .background(chipBackground, in: Capsule())
                .foregroundColor(isActive ? .white : .primary)
                .scaleEffect(isActive ? 1.05 : (isHovered ? 1.02 : 1.0))
                .animation(.easeInOut(duration: 0.2), value: isActive)
                .animation(.easeInOut(duration: 0.15), value: isHovered)
        }
        .buttonStyle(.plain)
        .onHover { hovering in
            isHovered = hovering
        }
        .help("Click to set sample rate to \(rate.description)")
    }
    
    private var chipBackground: Color {
        if isActive {
            return .blue
        } else if isHovered {
            return Color(NSColor.tertiaryLabelColor).opacity(0.3)
        } else {
            return Color(NSColor.quaternaryLabelColor).opacity(0.3)
        }
    }
}

// MARK: - SpecRow

struct SpecRow: View {
    let label: String
    let value: String
    
    var body: some View {
        HStack {
            Text(label)
                .foregroundStyle(.secondary)
            Spacer()
            Text(value)
                .fontDesign(.monospaced)
                .textSelection(.enabled)
        }
        .font(.caption)
    }
}

// MARK: - Legacy Extension Migration

extension View {
    func fwaCardStyle() -> some View {
        self
            .padding(8)
            .background(.ultraThinMaterial)
            .cornerRadius(8)
            .shadow(radius: 2)
    }
}