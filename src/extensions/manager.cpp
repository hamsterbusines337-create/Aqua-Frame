#include "manager.h"

#include "include/cef_frame.h"
#include "include/cef_request_context.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_fill_layout.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"

#include <algorithm>
#include <dwmapi.h>

namespace aqua {
namespace {
constexpr auto extensions_url = "chrome://extensions/";
constexpr cef_color_t aqua_toolbar = 0xff14243a;
constexpr cef_color_t aqua_background = 0xff0b1628;
constexpr cef_color_t aqua_accent = 0xff7dd3fc;

class ManagerBrowserView final : public CefBrowserViewDelegate {
public:
    cef_runtime_style_t GetBrowserRuntimeStyle() override {
        return CEF_RUNTIME_STYLE_CHROME;
    }

private:
    IMPLEMENT_REFCOUNTING(ManagerBrowserView);
};

class InstallHelp final : public CefButtonDelegate {
public:
    void OnButtonPressed(CefRefPtr<CefButton> button) override {
        auto window = button->GetWindow();
        if (!window) return;
        MessageBoxW(window->GetWindowHandle(),
            L"Включите режим разработчика и перетащите ZIP или CRX из Проводника на страницу.\n\n"
            L"Для ZIP файл manifest.json должен лежать в корне архива.",
            L"Установка расширений — Aqua Frame", MB_OK | MB_ICONINFORMATION);
    }

private:
    IMPLEMENT_REFCOUNTING(InstallHelp);
};

class ManagerWindow final : public CefWindowDelegate {
public:
    explicit ManagerWindow(CefRefPtr<CefBrowserView> view) : view_(view) {}

    cef_runtime_style_t GetWindowRuntimeStyle() override {
        return CEF_RUNTIME_STYLE_CHROME;
    }

    void OnWindowCreated(CefRefPtr<CefWindow> window) override {
        window->SetTitle(L"Расширения — Aqua Frame");
        window->SetBackgroundColor(aqua_background);
        const BOOL dark = TRUE;
        DwmSetWindowAttribute(window->GetWindowHandle(), 20, &dark, sizeof(dark));

        CefBoxLayoutSettings settings;
        settings.horizontal = false;
        auto layout = window->SetToBoxLayout(settings);

        auto toolbar = CefPanel::CreatePanel(nullptr);
        toolbar->SetBackgroundColor(aqua_toolbar);
        toolbar->SetToFillLayout();
        auto help = CefLabelButton::CreateLabelButton(new InstallHelp, L"Как перетащить файл");
        help->SetMinimumSize(CefSize(0, 42));
        help->SetEnabledTextColors(aqua_accent);
        help->SetFontList("Segoe UI, 13px");
        help->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
        toolbar->AddChildView(help);

        window->AddChildView(toolbar);
        window->AddChildView(view_);
        layout->SetFlexForView(view_, 1);
        window->CenterWindow(CefSize(900, 660));
        window->Show();
        view_->RequestFocus();
    }

    bool CanClose(CefRefPtr<CefWindow>) override {
        const auto browser = view_->GetBrowser();
        return !browser || browser->GetHost()->TryCloseBrowser();
    }

    void OnWindowDestroyed(CefRefPtr<CefWindow>) override { view_ = nullptr; }

private:
    CefRefPtr<CefBrowserView> view_;

    IMPLEMENT_REFCOUNTING(ManagerWindow);
};

bool IsManagerPage(const std::string& url) {
    return url == "chrome://extensions" || url.starts_with(extensions_url);
}
}

ExtensionManager::ExtensionManager(HWND owner, std::function<void(std::wstring)> open_web_tab)
    : owner_(owner), open_web_tab_(std::move(open_web_tab)) {}

bool ExtensionManager::Open() {
    if (closing_) return false;
    if (opening_) return true;

    for (const auto& browser : browsers_) {
        if (browser->GetHost()->IsReadyToBeClosed()) continue;

        auto view = CefBrowserView::GetForBrowser(browser);
        auto window = view ? view->GetWindow() : nullptr;
        if (window && !window->IsClosed()) {
            if (window->IsMinimized()) window->Restore();
            window->Activate();
            view->RequestFocus();
            return true;
        }
    }

    auto context = CefRequestContext::GetGlobalContext();
    const bool first_window = browsers_.empty();
    if (first_window) {
        previous_theme_ = context->GetChromeColorSchemeMode();
        previous_color_ = context->GetChromeColorSchemeColor();
        context->SetChromeColorScheme(CEF_COLOR_VARIANT_DARK, previous_color_);
    }

    CefBrowserSettings settings;
    settings.background_color = aqua_background;
    auto view = CefBrowserView::CreateBrowserView(this, extensions_url, settings,
                                                  nullptr, context, new ManagerBrowserView);
    opening_ = true;
    if (view && CefWindow::CreateTopLevelWindow(new ManagerWindow(view))) return true;

    opening_ = false;
    if (first_window) context->SetChromeColorScheme(previous_theme_, previous_color_);
    return false;
}

bool ExtensionManager::HasBrowsers() const {
    return opening_ || !browsers_.empty();
}

void ExtensionManager::Close() {
    if (closing_) return;
    closing_ = true;
    const auto browsers = browsers_;
    for (const auto& browser : browsers) {
        if (browser->IsValid()) browser->GetHost()->CloseBrowser(true);
    }
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
    if (HasBrowsers()) return;
    browser->GetHost()->GetRequestContext()->SetChromeColorScheme(previous_theme_, previous_color_);
    if (closing_) PostMessageW(owner_, WM_CLOSE, 0, 0);
}

bool ExtensionManager::OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                                      CefRefPtr<CefRequest> request, bool user_gesture, bool) {
    const auto url = request->GetURL();
    if (!frame->IsMain() || IsManagerPage(url.ToString())) return false;
    OpenExternalLink(url, user_gesture);
    return true;
}

bool ExtensionManager::OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int,
    const CefString& target, const CefString&, CefLifeSpanHandler::WindowOpenDisposition, bool user_gesture,
    const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&, CefBrowserSettings&,
    CefRefPtr<CefDictionaryValue>&, bool*) {
    OpenExternalLink(target, user_gesture);
    return true;
}

void ExtensionManager::OpenExternalLink(const CefString& target, bool user_gesture) {
    const auto url = target.ToWString();
    if (!closing_ && user_gesture && (url.starts_with(L"https://") || url.starts_with(L"http://"))) {
        open_web_tab_(url);
        SetForegroundWindow(owner_);
    }
}
}
