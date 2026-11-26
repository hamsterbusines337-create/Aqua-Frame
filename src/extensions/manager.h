#pragma once

#include "include/cef_client.h"

#include <functional>
#include <string>
#include <vector>

namespace aqua {
class ExtensionManager final : public CefClient, public CefLifeSpanHandler, public CefRequestHandler {
public:
    ExtensionManager(HWND owner, std::function<void(std::wstring)> open_web_tab);
    bool Open();
    void Close();
    bool HasBrowsers() const;

    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
    bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                        CefRefPtr<CefRequest>, bool, bool) override;
    bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int,
        const CefString&, const CefString&, WindowOpenDisposition, bool,
        const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
        CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override;

private:
    void OpenExternalLink(const CefString& target, bool user_gesture);

    HWND owner_;
    std::function<void(std::wstring)> open_web_tab_;
    cef_color_variant_t previous_theme_ = CEF_COLOR_VARIANT_SYSTEM;
    cef_color_t previous_color_ = 0;
    bool opening_ = false;
    bool closing_ = false;
    std::vector<CefRefPtr<CefBrowser>> browsers_;

    IMPLEMENT_REFCOUNTING(ExtensionManager);
};
}
