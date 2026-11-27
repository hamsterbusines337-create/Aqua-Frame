#pragma once

#include "../extensions/manager.h"
#include "../settings/page.h"
#include "../scripts/runner.h"
#include "../performance/processes.h"
#include "include/cef_browser.h"
#include <windows.h>
#include <cstdint>
#include <future>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aqua {
class Window final {
public:
    bool Create(HINSTANCE instance, int show, const std::filesystem::path& profile, Preferences preferences);
    bool ResetRequested() const { return reset_requested_; }
    void QueueSettingsAction(uint64_t tab, SettingsAction action);
    bool QueueScriptAction(uint64_t tab, std::string_view target);
    std::vector<Script> ScriptsFor(std::string_view url) const;
    void AddTab(const std::wstring& url = L"aqua://newtab/", bool private_tab = false, bool deferred = false);
    void RecordVisit(uint64_t tab, const CefString& url, int status);
    void TrackDownload(uint32_t id, const CefString& name, std::wstring state, bool paused = false,
                       CefRefPtr<CefDownloadItemCallback> callback = nullptr);
    void QueueDownloadAction(uint64_t tab, std::string_view url);
    void DocumentFullscreen(uint64_t tab, bool enabled);
    bool HttpsOnly() const { return preferences_.https_only; }
    void UpgradeNavigation(uint64_t tab, const std::string& url);
    void RequestSitePermission(uint64_t tab, const CefString& origin, std::wstring names,
                               std::function<void(bool)> done);
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
        bool private_tab = false;
        bool deferred = false;
        bool pinned = false;
        unsigned group = 0;
        bool unloading = false;
        bool focus_on_show = false;
    };

    static LRESULT CALLBACK Procedure(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK AddressProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT Message(UINT, WPARAM, LPARAM);
    void Layout();
    bool StartTab(Tab& tab);
    void Paint();
    void CloseTab(uint64_t id, bool force);
    bool CanUnloadTab(const Tab& tab) const;
    void UnloadTab(uint64_t id);
    void WarmRestoredTab();
    void Select(uint64_t id, bool focus_content = false);
    void Navigate();
    void Menu();
    void TabMenu(uint64_t id, POINT position);
    uint64_t TabAt(POINT point) const;
    void OpenExtensions();
    void OpenScriptManager();
    void RunScriptAction();
    void PickScriptFile();
    void ImportScript(const std::filesystem::path& path);
    void RefreshScriptSettings();
    void ApplySettingsAction(uint64_t tab, SettingsAction action);
    void SaveSession();
    void ShowLibrary(const wchar_t* section);
    void ToggleBookmark();
    void ToggleFullscreen();
    void MeasureNavigation();
    void ShowNavigationTiming(uint64_t request);
    void RefreshDownloads();
    void RunDownloadAction(uint64_t tab);
    void ScheduleHistorySave();
    void WriteHistoryInBackground();
    void FinishHistoryWrite();
    bool SavePreferences(const Preferences& snapshot);
    void ConfirmSitePermission();
    int ContentTop();
    void ApplyZoom(const CefRefPtr<CefBrowser>& browser, std::wstring_view url);
    RECT TabBounds(size_t index) const;
    RECT TabBounds(size_t index, long viewport_width) const;
    Tab* Find(uint64_t id);
    CefRefPtr<CefBrowser> Current();
    int Scale(int value) const;

    HWND hwnd_ = nullptr;
    HWND edit_ = nullptr;
    HFONT font_ = nullptr;
    HBRUSH address_brush_ = nullptr;
    CefRefPtr<ExtensionManager> extensions_;
    CefRefPtr<CefRequestContext> private_context_;
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
    struct Download {
        DownloadEntry entry;
        CefRefPtr<CefDownloadItemCallback> callback;
    };
    std::vector<Download> downloads_;
    std::optional<std::pair<uint32_t, std::string>> download_action_;
    std::map<uint64_t, std::wstring> https_upgrades_;
    struct SitePermission {
        uint64_t tab;
        std::string origin;
        std::wstring names;
        std::function<void(bool)> done;
    };
    std::optional<SitePermission> permission_;
    bool permission_open_ = false;
    bool history_dirty_ = false;
    std::future<bool> history_write_;
    uint64_t restore_foreground_ = 0;
    uint64_t restore_warm_tab_ = 0;
    bool bookmark_bar_ = false;
    WINDOWPLACEMENT windowed_{sizeof(WINDOWPLACEMENT)};
    bool fullscreen_ = false;
    uint64_t fullscreen_document_ = 0;
    uint64_t dragged_tab_ = 0;
    POINT drag_start_{};
    bool tab_reordered_ = false;
    uint64_t timing_tab_ = 0;
    uint64_t timing_request_ = 0;
    CefRefPtr<ScriptRun> timing_run_;
    std::optional<NavigationTiming> timing_result_;
    ProcessSnapshot performance_before_;
    UINT font_dpi_ = 0;
    uint64_t next_id_ = 1;
    uint64_t selected_ = 0;
    UINT dpi_ = 96;
    bool exiting_ = false;
    bool reset_requested_ = false;
};
}
