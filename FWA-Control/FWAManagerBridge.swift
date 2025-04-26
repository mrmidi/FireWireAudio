import SwiftUI

extension View {
    func fwaCardStyle() -> some View {
        self
            .padding(8)
            .background(.ultraThinMaterial)
            .cornerRadius(8)
            .shadow(radius: 2)
    }
}
