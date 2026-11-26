import SwiftUI
import WebKit

struct Bookmark: Codable, Identifiable {
    let title: String
    let url: URL
    var id: URL { url }
}

@MainActor
final class Browser: ObservableObject {
    @Published private(set) var tabs: [BrowserTab]
    @Published var selected: BrowserTab
    @Published private(set) var bookmarks: [Bookmark]

    init() {
        let first = BrowserTab(isPrivate: false)
        tabs = [first]
        selected = first
        if let data = UserDefaults.standard.data(forKey: "bookmarks"),
           let saved = try? JSONDecoder().decode([Bookmark].self, from: data) {
            bookmarks = saved
        } else {
            bookmarks = []
        }
    }

    func addTab(isPrivate: Bool) {
        let tab = BrowserTab(isPrivate: isPrivate)
        tabs.append(tab)
        selected = tab
    }

    func close(_ tab: BrowserTab) {
        guard let index = tabs.firstIndex(where: { $0.id == tab.id }) else { return }
        tab.webView.stopLoading()
        tabs.remove(at: index)
        if tabs.isEmpty {
            addTab(isPrivate: false)
        } else if selected.id == tab.id {
            selected = tabs[min(index, tabs.count - 1)]
        }
    }

    func toggleBookmark(_ url: URL, title: String) {
        guard !selected.isPrivate else { return }
        if let index = bookmarks.firstIndex(where: { $0.url == url }) {
            bookmarks.remove(at: index)
        } else {
            bookmarks.insert(Bookmark(title: title, url: url), at: 0)
        }
        if let data = try? JSONEncoder().encode(bookmarks) {
            UserDefaults.standard.set(data, forKey: "bookmarks")
        }
    }
}

@MainActor
final class BrowserTab: NSObject, ObservableObject, WKNavigationDelegate, WKUIDelegate {
    let id = UUID()
    let webView: WKWebView
    let isPrivate: Bool
    @Published private(set) var url: URL?
    @Published private(set) var title = "Новая вкладка"
    @Published private(set) var showingHome = true
    @Published private(set) var progress = 0.0
    @Published private(set) var canGoBack = false
    @Published private(set) var canGoForward = false
    @Published var error: String?
    private var lastRequestedURL: URL?
    private var observations: [NSKeyValueObservation] = []

    init(isPrivate: Bool) {
        self.isPrivate = isPrivate
        let config = WKWebViewConfiguration()
        config.websiteDataStore = isPrivate ? .nonPersistent() : .default()
        webView = WKWebView(frame: .zero, configuration: config)
        super.init()
        webView.navigationDelegate = self
        webView.uiDelegate = self
        webView.allowsBackForwardNavigationGestures = true
        observations = [
            webView.observe(\.estimatedProgress, options: [.new]) { [weak self] _, _ in
                Task { @MainActor in self?.refresh() }
            },
            webView.observe(\.title, options: [.new]) { [weak self] _, _ in
                Task { @MainActor in self?.refresh() }
            },
            webView.observe(\.url, options: [.new]) { [weak self] _, _ in
                Task { @MainActor in self?.refresh() }
            }
        ]
    }

    func submit(_ input: String) {
        if let target = Address.resolve(input) {
            open(target)
        } else if !input.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            error = "Введите HTTPS-адрес или поисковый запрос."
        }
    }

    func open(_ target: URL) {
        guard Address.mayLoad(target) else { return }
        lastRequestedURL = target
        url = target
        title = target.host ?? "Новая вкладка"
        showingHome = false
        progress = 0
        error = nil
        webView.load(URLRequest(url: target))
    }

    func back() {
        if canGoBack { webView.goBack() }
        else { goHome() }
    }

    func goHome() {
        showingHome = true
        webView.stopLoading()
        title = "Новая вкладка"
        url = nil
        error = nil
    }

    func reload() {
        guard !showingHome else { return }
        if error != nil, let target = lastRequestedURL {
            open(target)
        } else {
            webView.reload()
        }
    }

    private func refresh() {
        guard !showingHome else { return }
        if let current = webView.url, Address.mayLoad(current) {
            url = current
            title = webView.title.flatMap { $0.isEmpty ? nil : $0 } ?? current.host ?? "Aqua Frame"
        }
        progress = webView.estimatedProgress
        canGoBack = webView.canGoBack
        canGoForward = webView.canGoForward
    }

    func webView(_ webView: WKWebView, decidePolicyFor action: WKNavigationAction,
                 decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
        guard let target = action.request.url else {
            decisionHandler(.cancel)
            return
        }
        guard Address.mayLoad(target) else {
            // WebKit handles non-network subframes within its ordinary sandbox.
            if action.targetFrame?.isMainFrame == false,
               ["about", "blob", "data"].contains(target.scheme?.lowercased() ?? "") {
                decisionHandler(.allow)
                return
            }
            if action.navigationType == .linkActivated {
                error = "Переход доступен только по HTTPS."
            }
            decisionHandler(.cancel)
            return
        }
        if action.targetFrame?.isMainFrame == true {
            lastRequestedURL = target
            error = nil
        }
        decisionHandler(.allow)
    }

    func webView(_ webView: WKWebView, createWebViewWith configuration: WKWebViewConfiguration,
                 for action: WKNavigationAction, windowFeatures: WKWindowFeatures) -> WKWebView? {
        if action.targetFrame == nil, action.navigationType == .linkActivated,
           let target = action.request.url, Address.mayLoad(target) {
            open(target)
        }
        return nil
    }

    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        refresh()
    }

    func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!,
                 withError error: Error) {
        report(error)
    }

    func webView(_ webView: WKWebView, didFail navigation: WKNavigation!, withError error: Error) {
        report(error)
    }

    func webViewWebContentProcessDidTerminate(_ webView: WKWebView) {
        if !showingHome { error = "Страница остановлена. Нажмите кнопку обновления." }
    }

    private func report(_ failure: Error) {
        let failure = failure as NSError
        if !showingHome && !(failure.domain == NSURLErrorDomain && failure.code == NSURLErrorCancelled) {
            error = failure.localizedDescription
        }
    }
}
