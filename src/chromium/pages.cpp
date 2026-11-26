#include "pages.h"
#include "../settings/page.h"
#include "include/cef_request.h"
#include "include/cef_scheme.h"
#include "include/cef_stream.h"
#include "include/wrapper/cef_stream_resource_handler.h"
#include "include/wrapper/cef_byte_read_handler.h"

#include <algorithm>
#include <mutex>

namespace aqua {
namespace {
constexpr char home[] = R"HTML(<!doctype html>
<html lang="ru"><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; form-action https://www.google.com https://www.bing.com https://duckduckgo.com; base-uri 'none'">
<title>Новая вкладка</title>
<style>
:root{color-scheme:dark;background:#0b1628;color:#e6f1ff;font:16px 'Segoe UI',sans-serif}
body{margin:0;min-height:100vh;display:grid;place-items:center;background:#0b1628}
.waves{position:fixed;inset:0;width:100%;height:100%;pointer-events:none}
main{position:relative;text-align:center;width:min(580px,88vw);padding:24px}
.brand{width:76px;height:76px}h1{margin:3px 0 22px;font-size:27px}
form{display:flex;align-items:center;overflow:hidden;border:1px solid #2b3d52;border-radius:28px;background:#14243a;padding:0 12px}
input{font:inherit;color:#e6f1ff;outline:none;background:transparent;border:0;padding:14px 10px;width:100%}
form:focus-within{border-color:#00b8e6}button{font:inherit;background:transparent;border:0;color:#e6f1ff;cursor:pointer}
nav{display:flex;justify-content:center;flex-wrap:wrap;gap:14px;margin:23px auto}
a{display:grid;gap:7px;color:#e6f1ff;text-decoration:none;min-width:74px;font-size:13px}
.shortcut{display:grid;place-items:center;height:47px;background:#14243a;border:1px solid #2b3d52;border-radius:8px}
.shortcut svg{display:block;width:24px;height:24px}
a .shortcut{transition:transform 135ms ease,border-color 135ms ease,background-color 135ms ease}
a:hover .shortcut{transform:translateY(-2px)}
a:hover .shortcut,a:focus-visible .shortcut{border-color:#7dd3fc;background:#203b59}
@media(prefers-reduced-motion:reduce){.shortcut{transition:none!important;transform:none!important}}
.recent{margin-top:30px;text-align:left}.recent h2{font-size:13px;font-weight:600;color:#8ca0b3}
.recent ul{list-style:none;display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:8px;padding:0}
.recent a{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;background:#14243a;border:1px solid #2b3d52;border-radius:8px;padding:8px 10px}
.recent a:hover,.recent a:focus-visible{border-color:#7dd3fc}
.google{color:#7dd3fc;font-weight:700}
</style>
<svg class="waves" viewBox="0 0 1440 680" preserveAspectRatio="none" aria-hidden="true">
  <defs>
    <linearGradient id="ocean-a" x2="0" y2="1" gradientUnits="objectBoundingBox">
      <stop stop-color="#102f51"/><stop offset="1" stop-color="#0b192c"/>
    </linearGradient>
    <linearGradient id="ocean-b" x2="1" y2="0" gradientUnits="objectBoundingBox">
      <stop stop-color="#0b182a"/><stop offset="1" stop-color="#103659"/>
    </linearGradient>
  </defs>
  <path d="M0 190C240 292 383 299 546 328C747 364 786 497 1440 451V680H0Z" fill="url(#ocean-a)"/>
  <path d="M0 439C325 340 435 362 657 445C936 548 1080 585 1440 338V680H0Z" fill="url(#ocean-b)"/>
  <path d="M0 610C346 570 511 645 739 625C992 600 1193 590 1440 491V680H0Z" fill="#0a1c33"/>
</svg>
<main>
<svg class="brand" viewBox="0 0 80 80" aria-hidden="true"><defs><linearGradient id="a" x2="1" y2="1"><stop stop-color="#7dd3fc"/><stop offset="1" stop-color="#0089c4"/></linearGradient></defs><path fill="url(#a)" d="M36 8h10l29 61H60L40 25l-9 21 14 2 6 12-25-3-6 12H5z"/><path fill="#00b8e6" d="m36 8 4 17 6-17z"/></svg>
<h1>Aqua Frame</h1>
<form action="https://www.google.com/search" method="get">
<span class="google" aria-hidden="true">G</span><input name="q" type="search" aria-label="Поиск в Google" placeholder="Поиск в Google" required>
<button aria-label="Искать">⌕</button>
</form>
<nav aria-label="Быстрые ссылки">
<a href="https://www.youtube.com/">
  <span class="shortcut" aria-hidden="true"><svg viewBox="0 0 32 32">
    <path fill="#ff0033" d="M16 5c-4 0-10.9 0-12.9 1.4C1 7.9 1 11.5 1 16s0 8.1 2.1 9.6C5.1 27 12 27 16 27s10.9 0 12.9-1.4C31 24.1 31 20.5 31 16s0-8.1-2.1-9.6C26.9 5 20 5 16 5z"/>
    <path fill="#fff" d="M13 11.5v9l8-4.5z"/>
  </svg></span>YouTube
</a>
<a href="https://www.chess.com/">
  <span class="shortcut" aria-hidden="true"><svg viewBox="0 0 32 32">
    <path fill="#5c843e" d="M16 1a5 5 0 0 0-3 9l-3.5 2.5.5 2h3.3c0 4-1.8 6-4.1 8.1C6.9 24.7 6 27 6 30c2.8 2 17.2 2 20 0 0-3-.9-5.3-3.2-7.4-2.3-2.1-4.1-4.1-4.1-8.1H22l.5-2L19 10a5 5 0 0 0-3-9z"/>
    <path fill="#81aa50" d="M16 1a5 5 0 0 0-3 9l-3.5 2.5.5 2h8l.4-2-2-2.5A5 5 0 0 0 16 1zm-2.7 14.7c0 3.2-1.8 5-4.1 6.9a9 9 0 0 0-2.8 4.3c3.4 1 8.4 1.5 9.6-1.1.8-1.7.9-4.6 1.7-7.8.3-1.1-1.7-2.1-4.4-2.3z"/>
    <ellipse cx="14.6" cy="3.8" rx="2.4" ry="1.2" fill="#a2c66a" transform="rotate(-40 14.6 3.8)"/>
  </svg></span>Chess.com
</a>
<a href="https://lichess.org/">
  <span class="shortcut" aria-hidden="true"><svg viewBox="0 0 50 50">
    <path fill="#fff" stroke="#fff" stroke-linejoin="round" d="M38.956.5c-3.53.418-6.452.902-9.286 2.984C5.534 1.786-.692 18.533.68 29.364 3.493 50.214 31.918 55.785 41.329 41.7c-7.444 7.696-19.276 8.752-28.323 3.084S-.506 27.392 4.683 17.567C9.873 7.742 18.996 4.535 29.03 6.405c2.43-1.418 5.225-3.22 7.655-3.187l-1.694 4.86 12.752 21.37c-.439 5.654-5.459 6.112-5.459 6.112-.574-1.47-1.634-2.942-4.842-6.036-3.207-3.094-17.465-10.177-15.788-16.207-2.001 6.967 10.311 14.152 14.04 17.663 3.73 3.51 5.426 6.04 5.795 6.756 0 0 9.392-2.504 7.838-8.927L37.4 7.171z"/>
  </svg></span>Lichess
</a>
<a href="https://github.com/">
  <span class="shortcut" aria-hidden="true"><svg viewBox="0 0 24 24">
    <path fill="#fff" d="M10.226 17.284c-2.965-.36-5.054-2.493-5.054-5.256 0-1.123.404-2.336 1.078-3.144-.292-.741-.247-2.314.09-2.965.898-.112 2.111.36 2.83 1.01.853-.269 1.752-.404 2.853-.404 1.1 0 1.999.135 2.807.382.696-.629 1.932-1.1 2.83-.988.315.606.36 2.179.067 2.942.72.854 1.101 2 1.101 3.167 0 2.763-2.089 4.852-5.098 5.234.763.494 1.28 1.572 1.28 2.807v2.336c0 .674.561 1.056 1.235.786 4.066-1.55 7.255-5.615 7.255-10.646C23.5 6.188 18.334 1 11.978 1 5.62 1 .5 6.188.5 12.545c0 4.986 3.167 9.12 7.435 10.669.606.225 1.19-.18 1.19-.786V20.63a2.9 2.9 0 0 1-1.078.224c-1.483 0-2.359-.808-2.987-2.313-.247-.607-.517-.966-1.034-1.033-.27-.023-.359-.135-.359-.27 0-.27.45-.471.898-.471.652 0 1.213.404 1.797 1.235.45.651.921.943 1.483.943.561 0 .92-.202 1.437-.719.382-.381.674-.718.944-.943"/>
  </svg></span>GitHub
</a>
</nav></main></html>)HTML";

constexpr auto welcome = R"HTML(<!doctype html><html lang="ru"><meta charset="utf-8">
<title>Добро пожаловать в Aqua Frame</title>
<style>
:root{font:16px 'Segoe UI',sans-serif;background:#0b1628;color:#e6f1ff}
body{min-height:100vh;margin:0;display:grid;place-items:center;text-align:center}
main{padding:36px;background:#14243a;border:1px solid #2b3d52;border-radius:8px}
a{color:#7dd3fc}
</style><main><h1>Добро пожаловать в Aqua Frame</h1><p>Создан чистый профиль браузера.</p>
<a href="aqua://newtab/">Открыть главную страницу</a>
</main></html>)HTML";

std::mutex page_mutex;
Preferences page_preferences;
std::vector<Script> page_scripts;
std::vector<PageEntry> page_history, page_bookmarks;
std::vector<DownloadEntry> page_downloads;

class PageBytes final : public CefBaseRefCounted {
public:
    explicit PageBytes(std::string value) : text(std::move(value)) {}
    const std::string text;
private:
    IMPLEMENT_REFCOUNTING(PageBytes);
};

class Pages final : public CefSchemeHandlerFactory {
public:
    explicit Pages(bool private_session) : private_session_(private_session) {}
    CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
        const CefString&, CefRefPtr<CefRequest> request) override {
        if (request->GetMethod() != "GET") return nullptr;

        const auto url = request->GetURL().ToString();
        std::string page;
        if (url == "aqua://newtab/") {
            SearchEngine engine;
            Theme theme;
            bool animations;
            std::vector<PageEntry> recent;
            {
                const std::scoped_lock lock(page_mutex);
                engine = page_preferences.search;
                theme = page_preferences.theme;
                animations = page_preferences.animations;
                if (!private_session_)
                    recent.assign(page_history.begin(), page_history.begin() + std::min(size_t{4}, page_history.size()));
            }
            page = home;
            if (!animations) page.insert(page.find("</style>"), ".shortcut{transition:none!important;transform:none!important}");
            if (theme == Theme::contrast) {
                const auto style_end = page.find("</style>");
                page.insert(style_end,
                    ":root{background:#07101c;color:#fff}"
                    "body{background:#07101c}.waves{display:none}"
                    "form,nav a .shortcut{background:#12243a;border-color:#6687a7}");
            }
            const std::string_view endpoint = "https://www.google.com/search";
            const auto at = page.find(endpoint);
            if (at != std::string::npos) page.replace(at, endpoint.size(), SearchEndpoint(engine));
            if (engine != SearchEngine::google) {
                constexpr std::string_view label = "Поиск в Google";
                auto match = page.find(label);
                while (match != std::string::npos) {
                    const auto replacement = "Поиск · " + std::string(SearchName(engine));
                    page.replace(match, label.size(), replacement);
                    match = page.find(label, match + replacement.size());
                }
                const auto badge = page.find(">G</span>");
                if (badge != std::string::npos)
                    page[badge + 1] = SearchName(engine).front();
            }
            if (!recent.empty()) {
                std::string list = "<section class=\"recent\"><h2>Недавно посещённые</h2><ul>";
                for (const auto& entry : recent) {
                    const auto href = HtmlEscape(CefString(entry.url).ToString());
                    list += "<li><a href=\"" + href + "\">" +
                        HtmlEscape(CefString(entry.title.empty() ? entry.url : entry.title).ToString()) + "</a></li>";
                }
                page.insert(page.find("</main>"), list + "</ul></section>");
            }
        } else if (url == "aqua://settings/" || url.starts_with("aqua://settings/#")) {
            const std::scoped_lock lock(page_mutex);
            page = RenderSettings(page_preferences, page_scripts);
        } else if (url == "aqua://welcome/") {
            page = welcome;
        } else if (url == "aqua://library/history" || url == "aqua://library/bookmarks" ||
                   url == "aqua://library/downloads") {
            const std::scoped_lock lock(page_mutex);
            page = RenderLibrary(page_history, page_bookmarks, page_downloads, url);
        } else {
            return nullptr;
        }
        const auto policy = url == "aqua://newtab/"
            ? "frame-ancestors 'none'"
            : "default-src 'none'; style-src 'unsafe-inline'; frame-ancestors 'none'; base-uri 'none'";
        CefResponse::HeaderMap headers{
            {"Cache-Control", "no-store"},
            {"X-Frame-Options", "DENY"},
            {"X-Content-Type-Options", "nosniff"},
            {"Referrer-Policy", "no-referrer"},
            {"Content-Security-Policy", policy}};
        CefRefPtr<PageBytes> storage = new PageBytes(std::move(page));
        auto reader = new CefByteReadHandler(
            reinterpret_cast<const unsigned char*>(storage->text.data()), storage->text.size(), storage);
        return new CefStreamResourceHandler(200, "OK", "text/html", headers,
                                            CefStreamReader::CreateForHandler(reader));
    }
private:
    const bool private_session_;
    IMPLEMENT_REFCOUNTING(Pages);
};

}

void RegisterAquaScheme(CefRawPtr<CefSchemeRegistrar> registrar) {
    registrar->AddCustomScheme("aqua", CEF_SCHEME_OPTION_STANDARD |
        CEF_SCHEME_OPTION_SECURE | CEF_SCHEME_OPTION_DISPLAY_ISOLATED);
}

bool RegisterAquaPages(CefRefPtr<CefRequestContext> context) {
    auto pages = CefRefPtr<Pages>(new Pages(context != nullptr));
    if (context) return context->RegisterSchemeHandlerFactory("aqua", "newtab", pages);
    return CefRegisterSchemeHandlerFactory("aqua", "newtab", pages) &&
           CefRegisterSchemeHandlerFactory("aqua", "library", pages) &&
           CefRegisterSchemeHandlerFactory("aqua", "settings", pages) &&
           CefRegisterSchemeHandlerFactory("aqua", "welcome", pages);
}

void UpdateAquaPageSettings(const Preferences& settings) {
    const std::scoped_lock lock(page_mutex);
    page_preferences.search = settings.search;
    page_preferences.startup_page = settings.startup_page;
    page_preferences.theme = settings.theme;
    page_preferences.limit_restored_tabs = settings.limit_restored_tabs;
    page_preferences.warm_restored_tab = settings.warm_restored_tab;
    page_preferences.allow_tab_unload = settings.allow_tab_unload;
    page_preferences.prefer_high_performance_gpu = settings.prefer_high_performance_gpu;
    page_preferences.restore_session = settings.restore_session;
    page_preferences.zoom = settings.zoom;
    page_preferences.animations = settings.animations;
    page_preferences.https_only = settings.https_only;
}
void UpdateAquaHistory(const std::vector<PageEntry>& history) {
    auto snapshot = history;
    const std::scoped_lock lock(page_mutex);
    page_history.swap(snapshot);
}
void UpdateAquaBookmarks(const std::vector<PageEntry>& bookmarks) {
    auto snapshot = bookmarks;
    const std::scoped_lock lock(page_mutex);
    page_bookmarks.swap(snapshot);
}
void UpdateAquaDownloads(const std::vector<DownloadEntry>& downloads) {
    auto snapshot = downloads;
    const std::scoped_lock lock(page_mutex);
    page_downloads.swap(snapshot);
}
void UpdateAquaScriptList(const std::vector<Script>& scripts) {
    const std::scoped_lock lock(page_mutex);
    page_scripts.clear();
    for (const auto& script : scripts)
        page_scripts.push_back({script.id, script.name, script.origin, {}, script.enabled});
}

}
