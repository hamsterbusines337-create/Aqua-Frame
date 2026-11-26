#pragma once

#include "../extensions/manager.h"
#include "../settings/page.h"
#include "include/cef_browser.h"
#include <windows.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace aqua {
class Window final {
public:
    bool Create(HINSTANCE instance, int show, const std::filesystem::path& profile);
    bool ResetRequested() const { return reset_requested_; }
    void QueueSettingsAction(uint64_t tab, SettingsAction action);
    bool QueueScriptAction(uint64_t tab, std::string_view target);
    std::vector<Script> ScriptsFor(std::string_view url) const;
    void AddTab(const std::wstring& url = L"aqua://newtab/");
    void BrowserCreated(uint64_t tab, CefRefPtr<CefBrowser> browser);
    bool QueueBrowserClose(uint64_t tab);
    void BrowserClosed(uint64_t tab);
    void AddressChanged(uint64_t tab, const CefString& address);
    void TitleChanged(uint64_t tab, const CefString& title);
    bool Shortcut(int key, int modifiers);
    CefRefPtr<CefClient> DefaultChromeClient() const;

private:
    struct Tab {
        uint64_t id;
        std::wstring title;
        std::wstring url;
        CefRefPtr<CefBrowser> browser;
        bool closing = false;
    };

    static LRESULT CALLBACK Procedure(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK AddressProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT Message(UINT, WPARAM, LPARAM);
    void Layout();
    void Paint();
    void CloseTab(uint64_t id, bool force);
    void Select(uint64_t id);
    void Navigate();
    void Menu();
    void OpenExtensions();
    void OpenScriptManager();
    void RunScriptAction();
    void PickScriptFile();
    void ImportScript(const std::filesystem::path& path);
    void RefreshScriptSettings();
    void ApplySettingsAction(uint64_t tab, SettingsAction action);
    void SaveSession();
    void ApplyZoom(const CefRefPtr<CefBrowser>& browser, std::wstring_view url);
    RECT TabBounds(size_t index) const;
    Tab* Find(uint64_t id);
    CefRefPtr<CefBrowser> Current();
    int Scale(int value) const;

    HWND hwnd_ = nullptr;
    HWND edit_ = nullptr;
    HFONT font_ = nullptr;
    HBRUSH address_brush_ = nullptr;
    CefRefPtr<ExtensionManager> extensions_;
    std::filesystem::path profile_;
    Preferences preferences_;
    ScriptLibrary scripts_;
    struct PendingScriptAction {
        uint64_t tab;
        std::string action;
        std::string script;
    };
    std::optional<PendingScriptAction> script_action_;
    std::string last_web_origin_;
    std::vector<Tab> tabs_;
    std::vector<std::wstring> closed_;
    uint64_t next_id_ = 1;
    uint64_t selected_ = 0;
    UINT dpi_ = 96;
    bool exiting_ = false;
    bool reset_requested_ = false;
};
}
