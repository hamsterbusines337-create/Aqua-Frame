import SwiftUI
import WebKit

private let ocean = Color(red: 11 / 255, green: 22 / 255, blue: 40 / 255)
private let toolbar = Color(red: 20 / 255, green: 36 / 255, blue: 58 / 255)
private let accent = Color(red: 0, green: 184 / 255, blue: 230 / 255)
private let ink = Color(red: 230 / 255, green: 241 / 255, blue: 1)

@MainActor
struct BrowserView: View {
    @StateObject private var browser = Browser()
    @State private var showingTabs = false
    @State private var showingBookmarks = false

    var body: some View {
        TabViewContent(tab: browser.selected, bookmark: { url, title in
            browser.toggleBookmark(url, title: title)
        }, isBookmarked: { url in browser.bookmarks.contains { $0.url == url } },
        showTabs: { showingTabs = true }, showBookmarks: { showingBookmarks = true })
        .id(browser.selected.id)
        .sheet(isPresented: $showingTabs) { tabs }
        .sheet(isPresented: $showingBookmarks) { bookmarks }
        .tint(accent)
    }

    private var tabs: some View {
        NavigationStack {
            List(browser.tabs, id: \.id) { tab in
                HStack {
                    Button {
                        browser.selected = tab
                        showingTabs = false
                    } label: {
                        Label(tab.title, systemImage: tab.isPrivate ? "eye.slash" : "globe")
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .lineLimit(1)
                    }
                    .buttonStyle(.borderless)
                    Button {
                        browser.close(tab)
                    } label: {
                        Image(systemName: "xmark")
                    }
                    .buttonStyle(.borderless)
                    .accessibilityLabel("Закрыть вкладку")
                }
            }
            .scrollContentBackground(.hidden)
            .background(ocean)
            .navigationTitle("Вкладки")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button("Готово") { showingTabs = false }
                }
                ToolbarItemGroup(placement: .bottomBar) {
                    Button("Новая вкладка") {
                        browser.addTab(isPrivate: false)
                        showingTabs = false
                    }
                    Spacer()
                    Button("Частная вкладка") {
                        browser.addTab(isPrivate: true)
                        showingTabs = false
                    }
                }
            }
        }
    }

    private var bookmarks: some View {
        NavigationStack {
            List {
                if browser.bookmarks.isEmpty {
                    ContentUnavailableView("Нет закладок", systemImage: "star")
                }
                ForEach(browser.bookmarks) { item in
                    Button(item.title) {
                        browser.selected.open(item.url)
                        showingBookmarks = false
                    }
                    .swipeActions {
                        Button("Удалить", role: .destructive) {
                            browser.toggleBookmark(item.url, title: item.title)
                        }
                    }
                }
            }
            .scrollContentBackground(.hidden)
            .background(ocean)
            .navigationTitle("Закладки")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("Готово") { showingBookmarks = false }
                }
            }
        }
    }
}

private struct TabViewContent: View {
    @ObservedObject var tab: BrowserTab
    let bookmark: (URL, String) -> Void
    let isBookmarked: (URL) -> Bool
    let showTabs: () -> Void
    let showBookmarks: () -> Void
    @State private var address = ""
    @FocusState private var editing: Bool

    var body: some View {
        VStack(spacing: 0) {
            addressBar
            if !tab.showingHome && tab.progress < 1 {
                ProgressView(value: tab.progress).tint(accent)
            }
            if tab.showingHome {
                home
            } else {
                WebView(webView: tab.webView)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
            controls
        }
        .foregroundStyle(ink)
        .background(ocean.ignoresSafeArea())
        .onAppear { address = tab.url?.absoluteString ?? "" }
        .onChange(of: tab.url) { _, url in
            if !editing { address = url?.absoluteString ?? "" }
        }
        .alert("Aqua Frame", isPresented: Binding(
            get: { tab.error != nil },
            set: { if !$0 { tab.error = nil } }
        )) {
            Button("OK", role: .cancel) { tab.error = nil }
        } message: {
            Text(tab.error ?? "")
        }
    }

    private var addressBar: some View {
        HStack(spacing: 10) {
            Image(systemName: tab.isPrivate ? "eye.slash" : "magnifyingglass")
                .foregroundStyle(accent)
                .accessibilityLabel(tab.isPrivate ? "Частный режим" : "Поиск")
            TextField("Поиск Google или URL", text: $address)
                .accessibilityIdentifier("address")
                .focused($editing)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .keyboardType(.webSearch)
                .submitLabel(.go)
                .onSubmit {
                    editing = false
                    tab.submit(address)
                }
            if !tab.isPrivate, let url = tab.url {
                icon(isBookmarked(url) ? "star.fill" : "star", "Сохранить или удалить закладку") {
                    bookmark(url, tab.title)
                }
            }
        }
        .padding(.horizontal, 12)
        .frame(minHeight: 48)
        .background(ocean, in: RoundedRectangle(cornerRadius: 12))
        .padding(8)
        .background(toolbar)
    }

    private var home: some View {
        ScrollView {
            VStack(spacing: 22) {
                Image("Brand")
                    .resizable()
                    .scaledToFit()
                    .frame(width: 86, height: 86)
                    .accessibilityHidden(true)
                Text("Aqua Frame").font(.largeTitle.bold())
                if tab.isPrivate {
                    Label("Частная вкладка", systemImage: "eye.slash")
                } else {
                    Text("Beyond the ordinary.").foregroundStyle(.secondary)
                }
                Button {
                    editing = true
                } label: {
                    Label("Поиск в Google", systemImage: "magnifyingglass")
                        .frame(maxWidth: .infinity, minHeight: 48)
                        .background(toolbar, in: Capsule())
                }
                LazyVGrid(columns: [GridItem(.adaptive(minimum: 130))], spacing: 12) {
                    shortcut("YouTube", icon: "play.rectangle.fill", "https://www.youtube.com/")
                    shortcut("Chess.com", icon: "checkerboard.rectangle", "https://www.chess.com/")
                    shortcut("Lichess", icon: "checkerboard.shield", "https://lichess.org/")
                    shortcut("GitHub", icon: "chevron.left.forwardslash.chevron.right", "https://github.com/")
                }
            }
            .padding(.horizontal, 22)
            .padding(.vertical, 32)
        }
        .background(alignment: .bottomTrailing) {
            Ellipse().fill(toolbar).frame(width: 420, height: 210)
                .rotationEffect(.degrees(-22)).offset(x: 110, y: 95)
        }
        .clipped()
    }

    private func shortcut(_ title: String, icon: String, _ address: String) -> some View {
        Button {
            if let url = URL(string: address) { tab.open(url) }
        } label: {
            Label(title, systemImage: icon)
                .frame(maxWidth: .infinity, minHeight: 48)
                .background(toolbar, in: RoundedRectangle(cornerRadius: 8))
        }
    }

    private var controls: some View {
        HStack {
            icon("chevron.left", "Назад") { tab.back() }
                .disabled(tab.showingHome)
            Spacer()
            icon("chevron.right", "Вперёд") { tab.webView.goForward() }
                .disabled(tab.showingHome || !tab.canGoForward)
            Spacer()
            icon("arrow.clockwise", "Обновить") { tab.reload() }
                .disabled(tab.showingHome)
            Spacer()
            icon("house", "Домой") { tab.goHome() }
            Spacer()
            if !tab.isPrivate {
                icon("star", "Закладки", action: showBookmarks)
                Spacer()
            }
            icon("square.on.square", "Вкладки", action: showTabs)
        }
        .padding(.horizontal, 8)
        .background(toolbar)
    }

    private func icon(_ image: String, _ label: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: image)
                .frame(minWidth: 44, minHeight: 44)
                .contentShape(Rectangle())
        }
        .accessibilityLabel(label)
    }
}

private struct WebView: UIViewRepresentable {
    let webView: WKWebView
    func makeUIView(context: Context) -> WKWebView { webView }
    func updateUIView(_ view: WKWebView, context: Context) {}
}
