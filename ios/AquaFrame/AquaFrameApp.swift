import SwiftUI

@main
struct AquaFrameApp: App {
    var body: some Scene {
        WindowGroup {
            BrowserView()
                .preferredColorScheme(.dark)
        }
    }
}
