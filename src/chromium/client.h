#pragma once

#include "include/cef_client.h"
#include <cstdint>

namespace aqua {
class Window;
class Client final : public CefClient, public CefDisplayHandler,
                     public CefLifeSpanHandler, public CefKeyboardHandler, public CefRequestHandler {
public:
    Client(Window& window, uint64_t tab) : window_(window), tab_(tab) {}
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
    bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>,
                        bool user_gesture, bool is_redirect) override;
    bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int,
        const CefString&, const CefString&, CefLifeSpanHandler::WindowOpenDisposition, bool,
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
