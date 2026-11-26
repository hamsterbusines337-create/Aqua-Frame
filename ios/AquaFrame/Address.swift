import Foundation

enum Address {
    static func resolve(_ input: String) -> URL? {
        let text = input.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return nil }

        if !text.contains(where: { $0.isWhitespace }) {
            if let components = URLComponents(string: text), components.host != nil {
                if let url = secureURL(components) { return url }
                if components.scheme != nil { return nil }
            }
            if text.contains("://") { return nil }
            let authority = text.prefix { !"/?#".contains($0) }
            let host = authority.split(separator: ":", maxSplits: 1).first ?? ""
            if host.contains("."), !authority.contains("@"), !authority.contains("\\"),
               let url = URLComponents(string: "https://" + text).flatMap(secureURL) {
                return url
            }
        }

        var search = URLComponents()
        search.scheme = "https"
        search.host = "www.google.com"
        search.path = "/search"
        search.queryItems = [URLQueryItem(name: "q", value: text)]
        return search.url
    }

    static func mayLoad(_ url: URL) -> Bool {
        url.scheme?.lowercased() == "https" && url.host != nil &&
            url.user == nil && url.password == nil
    }

    private static func secureURL(_ components: URLComponents) -> URL? {
        var components = components
        if components.scheme?.lowercased() == "http" {
            components.scheme = "https"
            if components.port == 80 { components.port = nil }
        }
        guard let url = components.url, mayLoad(url) else { return nil }
        return url
    }
}
