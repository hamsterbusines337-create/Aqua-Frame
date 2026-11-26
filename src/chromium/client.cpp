#include "client.h"
#include "../ui/window.h"
#include "include/cef_frame.h"
#include "../settings/page.h"

namespace aqua {
bool Client::OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefRequest> request, bool user_gesture, bool is_redirect) {
    const auto target = request->GetURL().ToString();
    if (!target.starts_with("aqua://settings/action/")) return false;

    const auto source = frame->GetURL().ToString();
    if (!browser->IsPopup() && frame->IsMain() && request->GetMethod() == "GET" &&
        user_gesture && !is_redirect &&
        (source == "aqua://settings/" || source.starts_with("aqua://settings/#"))) {
        const auto action = ParseSettingsAction(target);
        if (action != SettingsAction::unknown) window_.QueueSettingsAction(tab_, action);
    }
    return true;
}

bool Client::OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int,
    const CefString& target, const CefString&, CefLifeSpanHandler::WindowOpenDisposition, bool gesture,
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

bool Client::DoClose(CefRefPtr<CefBrowser> browser) {
    return !browser->IsPopup() && window_.QueueBrowserClose(tab_);
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
