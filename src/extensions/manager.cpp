#include "manager.h"

#include "include/cef_frame.h"

#include <algorithm>

namespace aqua {
namespace {
constexpr auto extensions_url = "chrome://extensions/";
}

bool ExtensionManager::Open() {
    if (closing_) return false;
    if (opening_) return true;

    for (const auto& browser : browsers_) {
        auto host = browser->GetHost();
        if (host->IsReadyToBeClosed()) continue;

        const auto window = host->GetWindowHandle();
        if (window && IsWindow(window)) {
            browser->GetMainFrame()->LoadURL(extensions_url);
            if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
            SetForegroundWindow(window);
            host->SetFocus(true);
            return true;
        }
    }

    CefWindowInfo info;
    info.SetAsPopup(nullptr, L"Aqua Frame — Расширения");
    info.runtime_style = CEF_RUNTIME_STYLE_CHROME;
    CefBrowserSettings settings;
    opening_ = CefBrowserHost::CreateBrowser(info, this, extensions_url,
                                             settings, nullptr, nullptr);
    return opening_;
}

bool ExtensionManager::HasBrowsers() const {
    return opening_ || !browsers_.empty();
}

void ExtensionManager::Close() {
    if (closing_) return;
    closing_ = true;
    const auto browsers = browsers_;
    for (const auto& browser : browsers)
        browser->GetHost()->CloseBrowser(true);
}

void ExtensionManager::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    opening_ = false;
    browsers_.push_back(browser);
    if (closing_) browser->GetHost()->CloseBrowser(true);
}

void ExtensionManager::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    const auto id = browser->GetIdentifier();
    std::erase_if(browsers_, [id](const auto& item) {
        return item->GetIdentifier() == id;
    });
    if (closing_ && !HasBrowsers()) PostMessageW(owner_, WM_CLOSE, 0, 0);
}
}
