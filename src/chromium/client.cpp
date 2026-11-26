#include "client.h"
#include "../ui/window.h"
#include "include/cef_frame.h"

namespace aqua {
bool Client::OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int,
    const CefString& target, const CefString&, WindowOpenDisposition, bool gesture,
    const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
    CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) {
    const auto url = target.ToWString();
    if (gesture && (url.starts_with(L"https://") || url.starts_with(L"http://")))
        window_.AddTab(url);
    return true;
}

void Client::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    if (!browser->IsPopup()) window_.BrowserCreated(tab_, browser);
}

void Client::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    if (!browser->IsPopup()) window_.BrowserClosed(tab_);
}

void Client::OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) {
    if (!browser->IsPopup()) window_.TitleChanged(tab_, title);
}

void Client::OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                              const CefString& address) {
    if (!browser->IsPopup() && frame->IsMain()) window_.AddressChanged(tab_, address);
}

bool Client::OnPreKeyEvent(CefRefPtr<CefBrowser> browser, const CefKeyEvent& event,
                           CefEventHandle, bool*) {
    return !browser->IsPopup() && event.type == KEYEVENT_RAWKEYDOWN &&
           window_.Shortcut(event.windows_key_code, event.modifiers);
}
}
