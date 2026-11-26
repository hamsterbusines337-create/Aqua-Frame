#pragma once

#include "include/cef_client.h"
#include "../scripts/runner.h"
#include <cstdint>

namespace aqua {
class Window;
class Client final : public CefClient, public CefDisplayHandler,
                     public CefLifeSpanHandler, public CefKeyboardHandler, public CefRequestHandler,
                     public CefLoadHandler, public CefDownloadHandler {
public:
    Client(Window& window, uint64_t tab, bool private_tab) : window_(window), tab_(tab), private_tab_(private_tab) {}
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
    CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
    bool OnBeforeDownload(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem>,
                          const CefString&, CefRefPtr<CefBeforeDownloadCallback>) override;
    void OnDownloadUpdated(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem>,
                           CefRefPtr<CefDownloadItemCallback>) override;
    void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int status) override;
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
    const bool private_tab_;
    CefRefPtr<ScriptRun> script_run_;
    IMPLEMENT_REFCOUNTING(Client);
};
}
