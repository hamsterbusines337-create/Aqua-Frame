#pragma once

#include "include/cef_client.h"
#include <cstdint>

namespace aqua {
class Window;
class Client final : public CefClient, public CefDisplayHandler,
                     public CefLifeSpanHandler, public CefKeyboardHandler {
public:
    Client(Window& window, uint64_t tab) : window_(window), tab_(tab) {}
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
    bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int,
        const CefString&, const CefString&, WindowOpenDisposition, bool,
        const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
        CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override;
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    bool DoClose(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
    void OnTitleChange(CefRefPtr<CefBrowser>, const CefString&) override;
    void OnAddressChange(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString&) override;
    bool OnPreKeyEvent(CefRefPtr<CefBrowser>, const CefKeyEvent&, CefEventHandle, bool*) override;

private:
    Window& window_;
    uint64_t tab_;
    IMPLEMENT_REFCOUNTING(Client);
};
}
