#include "client.h"
#include "../ui/window.h"
#include "include/cef_frame.h"
#include "../settings/page.h"
#include "../scripts/drop.h"

namespace aqua {
bool Client::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                       CefProcessId source, CefRefPtr<CefProcessMessage> message) {
    if (message->GetName() != script_drop_message) return false;
    if (source != PID_RENDERER || private_tab_ || browser->IsPopup() || !frame->IsMain() ||
        frame->GetURL() != "aqua://settings/#scripts") return true;
    const auto args = message->GetArgumentList();
    if (args && args->GetSize() == 2 && args->GetType(0) == VTYPE_STRING && args->GetType(1) == VTYPE_STRING)
        window_.QueueDroppedScript(tab_, args->GetString(0), args->GetString(1));
    return true;
}

bool Client::OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefRequest> request, bool user_gesture, bool is_redirect) {
    const auto target = request->GetURL().ToString();
    if (private_tab_ && (target.starts_with("aqua:") || target.starts_with("chrome:")) &&
        target != "aqua://newtab/") return true;
    if (target.starts_with("aqua://library/action/")) {
        if (frame->IsMain() && !browser->IsPopup() && user_gesture && !is_redirect &&
            request->GetMethod() == "GET" && frame->GetURL() == "aqua://library/downloads")
            window_.QueueDownloadAction(tab_, target);
        return true;
    }
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

void Client::OnFullscreenModeChange(CefRefPtr<CefBrowser> browser, bool fullscreen) {
    if (!browser->IsPopup()) window_.DocumentFullscreen(tab_, fullscreen);
}

bool Client::OnBeforeDownload(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem> item,
                              const CefString& name, CefRefPtr<CefBeforeDownloadCallback> callback) {
    if (!private_tab_) window_.TrackDownload(item->GetId(), name, L"Выбор места сохранения");
    callback->Continue(CefString(), true);
    return true;
}

void Client::OnDownloadUpdated(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem> item,
                               CefRefPtr<CefDownloadItemCallback> callback) {
    if (private_tab_ || !item->IsValid()) return;
    std::wstring state = item->IsComplete() ? L"Завершена" : item->IsCanceled() ? L"Отменена" :
                         item->IsInterrupted() ? L"Прервана" : item->IsPaused() ? L"На паузе" : L"Загружается";
    const auto percent = item->GetPercentComplete();
    if (item->IsInProgress() && percent >= 0) state += L" · " + std::to_wstring(percent) + L"%";
    window_.TrackDownload(item->GetId(), item->GetSuggestedFileName(), std::move(state), item->IsPaused(),
                          item->IsInProgress() || item->IsPaused() ? callback : nullptr);
}

bool Client::OnRequestMediaAccessPermission(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
    const CefString& origin, uint32_t requested, CefRefPtr<CefMediaAccessCallback> callback) {
    constexpr uint32_t supported = CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE |
                                   CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE;
    if (!frame->IsMain() || !requested || (requested & ~supported)) {
        callback->Cancel();
        return true;
    }
    std::wstring names;
    if (requested & CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE) names = L"Камера\n";
    if (requested & CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE) names += L"Микрофон\n";
    window_.RequestSitePermission(tab_, origin, std::move(names),
        [callback, requested](bool allowed) {
            if (allowed) callback->Continue(requested);
            else callback->Cancel();
        });
    return true;
}

bool Client::OnShowPermissionPrompt(CefRefPtr<CefBrowser>, uint64_t, const CefString& origin,
                                    uint32_t requested, CefRefPtr<CefPermissionPromptCallback> callback) {
    constexpr uint32_t supported = CEF_PERMISSION_TYPE_GEOLOCATION | CEF_PERMISSION_TYPE_NOTIFICATIONS |
        CEF_PERMISSION_TYPE_CLIPBOARD | CEF_PERMISSION_TYPE_CAMERA_STREAM | CEF_PERMISSION_TYPE_MIC_STREAM;
    if (!requested || (requested & ~supported)) return false;
    std::wstring names;
    if (requested & CEF_PERMISSION_TYPE_GEOLOCATION) names += L"Геолокация\n";
    if (requested & CEF_PERMISSION_TYPE_NOTIFICATIONS) names += L"Уведомления\n";
    if (requested & CEF_PERMISSION_TYPE_CLIPBOARD) names += L"Буфер обмена\n";
    if (requested & CEF_PERMISSION_TYPE_CAMERA_STREAM) names += L"Камера\n";
    if (requested & CEF_PERMISSION_TYPE_MIC_STREAM) names += L"Микрофон\n";
    window_.RequestSitePermission(tab_, origin, std::move(names), [callback](bool allowed) {
        callback->Continue(allowed ? CEF_PERMISSION_RESULT_ACCEPT : CEF_PERMISSION_RESULT_DENY);
    });
    return true;
}

#if CEF_API_ADDED(15800)
bool Client::OnPreKeyboardEvent(CefRefPtr<CefBrowser> browser, const CefKeyboardInputEvent& event,
                               const CefInputEventContext&, bool*) {
#else
bool Client::OnPreKeyEvent(CefRefPtr<CefBrowser> browser, const CefKeyEvent& event, CefEventHandle, bool*) {
#endif
    return !browser->IsPopup() && (event.type == KEYEVENT_RAWKEYDOWN || event.type == KEYEVENT_KEYDOWN) &&
           window_.Shortcut(event.windows_key_code, event.modifiers);
}
}
