#include "library.h"
#include "include/cef_parser.h"

namespace aqua {
std::string HtmlEscape(std::string_view text) {
    std::string result;
    for (const char c : text) {
        switch (c) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&#39;"; break;
        default: result += c;
        }
    }
    return result;
}

bool IsLibraryUrl(std::wstring_view url) {
    if (url.empty() || url.size() > 4096) return false;
    CefURLParts parts;
    if (!CefParseURL(CefString(std::wstring(url)), parts)) return false;
    const auto scheme = CefString(&parts.scheme).ToString();
    return (scheme == "https" || scheme == "http") && !CefString(&parts.host).empty() &&
        CefString(&parts.username).empty() && CefString(&parts.password).empty();
}

std::string RenderLibrary(const std::vector<PageEntry>& history, const std::vector<PageEntry>& bookmarks,
                          const std::vector<std::wstring>& downloads, std::string_view url) {
    const bool show_bookmarks = url == "aqua://library/bookmarks";
    const bool show_downloads = url == "aqua://library/downloads";
    std::string page = R"HTML(<!doctype html><html lang="ru"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Библиотека Aqua Frame</title><style>
:root{color-scheme:dark;color:#e6f1ff;background:#0b1628;font:15px 'Segoe UI',sans-serif}
body{margin:0}nav{padding:16px 24px;background:#14243a;border-bottom:1px solid #2b3d52;display:flex;gap:20px;flex-wrap:wrap}
a{color:#7dd3fc;text-decoration:none}main{max-width:820px;margin:auto;padding:24px}
ul{list-style:none;padding:0}li{margin:8px 0}li>a,.download{display:block;padding:12px;border:1px solid #2b3d52;border-radius:8px;background:#14243a;overflow-wrap:anywhere}
li>a{color:#e6f1ff}li>a:hover,li>a:focus-visible{border-color:#7dd3fc}
summary,small,p{color:#8ca0b3}small{display:block;margin-top:3px}
</style><nav><a href="/history">История</a><a href="/bookmarks">Закладки</a><a href="/downloads">Загрузки</a></nav><main>)HTML";
    if (show_downloads) {
        page += "<h1>Загрузки</h1><p>Текущая сессия · <a href=\"/downloads\">Обновить список</a></p><ul>";
        for (const auto& status : downloads)
            page += "<li class=\"download\">" + HtmlEscape(CefString(status).ToString()) + "</li>";
        if (downloads.empty()) page += "<li>Загрузок пока нет.</li>";
    } else {
        page += show_bookmarks ? "<h1>Закладки</h1><p>Ctrl+D добавляет или убирает текущую страницу.</p><ul>" :
                                "<h1>История</h1><p><a href=\"aqua://settings/#privacy\">Очистить историю</a></p><ul>";
        const auto& entries = show_bookmarks ? bookmarks : history;
        for (const auto& entry : entries) {
            const auto url_text = HtmlEscape(CefString(entry.url).ToString());
            page += "<li><a href=\"" + url_text + "\">" +
                HtmlEscape(CefString(entry.title.empty() ? entry.url : entry.title).ToString()) +
                "<small>" + url_text + "</small></a></li>";
        }
        if (entries.empty()) page += "<li>Пока пусто.</li>";
    }
    return page + "</ul></main></html>";
}
}
