import SwiftUI

struct ExpandableSectionView<Content: View>: View {
    var title: String
    @ViewBuilder var content: () -> Content
    @State private var expanded: Bool = false
    var body: some View {
        DisclosureGroup(isExpanded: $expanded) {
            content()
        } label: {
            Text(title).font(.subheadline).bold()
        }
    }
}
