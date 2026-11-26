#include "client.h"
#include "../ui/window.h"
#include "include/cef_frame.h"
#include "../settings/page.h"

namespace aqua {
bool Client::OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefRequest> request, bool user_gesture, bool is_redirect) {
    const auto target = request->GetURL().ToString();
    if (private_tab_ && (target.starts_with("aqua:") || target.starts_with("chrome:")) &&
        target != "aqua://newtab/") return true;
    if (frame->IsMain() && target.starts_with("http://") && window_.HttpsOnly()) {
        if (!is_redirect && request->GetMethod() == "GET") window_.UpgradeNavigation(tab_, target);
        return true;
    }
    if (!target.starts_with("aqua://settings/action/")) {
        if (frame->IsMain() && script_run_) {
            script_run_->Cancel();
            script_run_ = nullptr;
        }
        return false;
    }

    const auto source = frame->GetURL().ToString();
    if (!browser->IsPopup() && frame->IsMain() && request->GetMethod() == "GET" &&
        user_gesture && !is_redirect &&
        (source == "aqua://settings/" || source.starts_with("aqua://settings/#"))) {
        if (source == "aqua://settings/#scripts" && window_.QueueScriptAction(tab_, target)) return true;
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
        window_.AddTab(url, private_tab_);
    return true;
}

void Client::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    if (!browser->IsPopup()) window_.BrowserCreated(tab_, browser);
}

bool Client::DoClose(CefRefPtr<CefBrowser> browser) {
    return !browser->IsPopup() && window_.QueueBrowserClose(tab_);
}

void Client::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    if (script_run_) {
        script_run_->Cancel();
        script_run_ = nullptr;
    }
    if (!browser->IsPopup()) window_.BrowserClosed(tab_);
}

void Client::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int status) {
    if (browser->IsPopup() || !frame->IsMain() || status < 200 || status >= 400) return;
    if (private_tab_) return;
    window_.RecordVisit(tab_, frame->GetURL(), status);
    auto url = frame->GetURL().ToString();
    auto scripts = window_.ScriptsFor(url);
    if (scripts.empty()) return;
    if (script_run_) script_run_->Cancel();
    script_run_ = new ScriptRun(browser->GetHost(), std::move(url), std::move(scripts));
    script_run_->Start();
}

void Client::OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) {
    if (!browser->IsPopup()) window_.TitleChanged(tab_, title);
}

void Client::OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                              const CefString& address) {
    if (!browser->IsPopup() && frame->IsMain()) window_.AddressChanged(tab_, address);
}

bool Client::OnBeforeDownload(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem> item,
                              const CefString& name, CefRefPtr<CefBeforeDownloadCallback> callback) {
    if (!private_tab_) window_.TrackDownload(item->GetId(), name, L"Выбор места сохранения");
    callback->Continue(CefString(), true);
    return true;
}

void Client::OnDownloadUpdated(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem> item,
                               CefRefPtr<CefDownloadItemCallback>) {
    if (private_tab_ || !item->IsValid()) return;
    std::wstring state = item->IsComplete() ? L"Завершена" : item->IsCanceled() ? L"Отменена" :
                         item->IsInterrupted() ? L"Прервана" : L"Загружается";
    const auto percent = item->GetPercentComplete();
    if (item->IsInProgress() && percent >= 0) state += L" · " + std::to_wstring(percent) + L"%";
    window_.TrackDownload(item->GetId(), item->GetSuggestedFileName(), std::move(state));
}

bool Client::OnPreKeyEvent(CefRefPtr<CefBrowser> browser, const CefKeyEvent& event,
                           CefEventHandle, bool*) {
    return !browser->IsPopup() && event.type == KEYEVENT_RAWKEYDOWN &&
           window_.Shortcut(event.windows_key_code, event.modifiers);
}
}
