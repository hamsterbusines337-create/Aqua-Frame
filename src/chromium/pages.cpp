#include "pages.h"
#include "include/cef_request.h"
#include "include/cef_scheme.h"
#include "include/cef_stream.h"
#include "include/wrapper/cef_stream_resource_handler.h"
#include <cstring>

namespace aqua {
namespace {
constexpr char home[] = R"HTML(<!doctype html>
<html lang="ru"><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; form-action https://www.google.com; base-uri 'none'">
<title>Новая вкладка</title>
<style>
:root{color-scheme:dark;background:#0b1628;color:#e6f1ff;font:16px 'Segoe UI',sans-serif}
body{margin:0;min-height:100vh;display:grid;place-items:center;background:
radial-gradient(ellipse 90% 60% at -10% 72%,#123254 0%,#0a1b2c 57%,transparent 58%),
radial-gradient(ellipse 78% 54% at 108% 94%,#103155 15%,#09213b 61%,transparent 62%),#0b1628}
main{text-align:center;width:min(580px,90vw);padding:24px}
svg{width:76px;height:76px}h1{margin:3px 0 22px;font-size:27px}
form{display:flex;align-items:center;overflow:hidden;border:1px solid #2b3d52;border-radius:28px;background:#14243a;padding:0 12px}
input{font:inherit;color:#e6f1ff;outline:none;background:transparent;border:0;padding:14px 10px;width:100%}
form:focus-within{border-color:#00b8e6}button{font:inherit;background:transparent;border:0;color:#e6f1ff;cursor:pointer}
nav{display:flex;justify-content:center;flex-wrap:wrap;gap:14px;margin:23px auto}
a{display:grid;gap:7px;color:#e6f1ff;text-decoration:none;min-width:74px;font-size:13px}
b{display:grid;place-items:center;height:47px;background:#14243a;border:1px solid #2b3d52;border-radius:8px;font-size:21px}
a:hover b,a:focus-visible b{border-color:#7dd3fc;background:#203b59}
p{color:#8ca0b3;font-size:13px;margin:22px 0 0}.google{color:#7dd3fc;font-weight:700}
</style>
<main>
<svg viewBox="0 0 80 80" aria-hidden="true"><defs><linearGradient id="a" x2="1" y2="1"><stop stop-color="#7dd3fc"/><stop offset="1" stop-color="#0089c4"/></linearGradient></defs><path fill="url(#a)" d="M36 8h10l29 61H60L40 25l-9 21 14 2 6 12-25-3-6 12H5z"/><path fill="#00b8e6" d="m36 8 4 17 6-17z"/></svg>
<h1>Aqua Frame</h1>
<form action="https://www.google.com/search" method="get">
<span class="google" aria-hidden="true">G</span><input name="q" type="search" aria-label="Поиск в Google" placeholder="Поиск в Google" required>
<button aria-label="Искать">⌕</button>
</form>
<nav aria-label="Быстрые ссылки">
<a href="https://www.youtube.com/"><b aria-hidden="true">▶</b>YouTube</a>
<a href="https://www.chess.com/"><b aria-hidden="true">♟</b>Chess.com</a>
<a href="https://lichess.org/"><b aria-hidden="true">♘</b>Lichess</a>
<a href="https://github.com/"><b aria-hidden="true">&lt;/&gt;</b>GitHub</a>
</nav></main></html>)HTML";

class Pages final : public CefSchemeHandlerFactory {
public:
    CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
        const CefString&, CefRefPtr<CefRequest> request) override {
        if (request->GetMethod() != "GET" || request->GetURL() != "aqua://newtab/")
            return nullptr;
        CefResponse::HeaderMap headers{
            {"X-Frame-Options", "DENY"},
            {"X-Content-Type-Options", "nosniff"},
            {"Content-Security-Policy", "frame-ancestors 'none'"}};
        auto stream = CefStreamReader::CreateForData(const_cast<char*>(home), sizeof(home) - 1);
        return new CefStreamResourceHandler(200, "OK", "text/html", headers, stream);
    }
private:
    IMPLEMENT_REFCOUNTING(Pages);
};
}

void RegisterAquaScheme(CefRawPtr<CefSchemeRegistrar> registrar) {
    registrar->AddCustomScheme("aqua", CEF_SCHEME_OPTION_STANDARD |
        CEF_SCHEME_OPTION_SECURE | CEF_SCHEME_OPTION_DISPLAY_ISOLATED);
}

bool RegisterAquaPages() {
    return CefRegisterSchemeHandlerFactory("aqua", "newtab", new Pages());
}
}
