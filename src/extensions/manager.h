#pragma once

#include "include/cef_client.h"

#include <vector>

namespace aqua {
class ExtensionManager final : public CefClient, public CefLifeSpanHandler {
public:
    explicit ExtensionManager(HWND owner) : owner_(owner) {}
    bool Open();
    void Close();
    bool HasBrowsers() const;

    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

private:
    HWND owner_;
    bool opening_ = false;
    bool closing_ = false;
    std::vector<CefRefPtr<CefBrowser>> browsers_;

    IMPLEMENT_REFCOUNTING(ExtensionManager);
};
}
