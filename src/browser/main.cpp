#include <windows.h>
#include <filesystem>
#include <optional>
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_sandbox_win.h"
#include "../chromium/pages.h"
#include "../ui/window.h"
#include "../profiles/profile.h"
#include "../reset/restart.h"
#include "../scripts/drop.h"

namespace {
class App final : public CefApp, public CefBrowserProcessHandler, public CefRenderProcessHandler {
public:
    App(HINSTANCE instance, int show) : instance_(instance), show_(show) {}
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }
    void OnContextCreated(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                          CefRefPtr<CefV8Context> context) override {
        aqua::BindScriptDrop(frame, context);
    }
    CefRefPtr<CefClient> GetDefaultClient() override { return window_.DefaultChromeClient(); }
    void OnBeforeCommandLineProcessing(const CefString& process_type,
                                       CefRefPtr<CefCommandLine> command_line) override {
        if (!process_type.empty() || profile_.empty()) return;
        startup_preferences_ = aqua::ReadPreferences(profile_);
        if (startup_preferences_->prefer_high_performance_gpu)
            command_line->AppendSwitch("force-high-performance-gpu");
    }
    void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override {
        aqua::RegisterAquaScheme(registrar);
    }
    void OnContextInitialized() override {
        auto preferences = startup_preferences_ ? std::move(*startup_preferences_) : aqua::ReadPreferences(profile_);
        startup_preferences_.reset();
        if (!aqua::RegisterAquaPages() || !window_.Create(instance_, show_, profile_, std::move(preferences))) {
            failed_ = true;
        }
    }
    bool Failed() const { return failed_; }
    bool ResetRequested() const { return window_.ResetRequested(); }
    void SetProfile(std::filesystem::path profile) { profile_ = std::move(profile); }

private:
    HINSTANCE instance_;
    int show_;
    aqua::Window window_;
    std::filesystem::path profile_;
    std::optional<aqua::Preferences> startup_preferences_;
    bool failed_ = false;
    IMPLEMENT_REFCOUNTING(App);
};

}

CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE instance, LPWSTR, int show,
                                    void* sandbox_info, cef_version_info_t*) {
    if (!sandbox_info) return 1;
    CefMainArgs args(instance);

    // Register the same scheme in every process before Chromium starts.
    CefRefPtr<App> app = new App(instance, show);
    const int child_code = CefExecuteProcess(args, app, sandbox_info);
    if (child_code >= 0) return child_code;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto root = aqua::ProfileRoot();
    if (!aqua::EnsureProfileContainer(root)) {
        MessageBoxW(nullptr, L"Не удалось проверить расположение профиля Aqua Frame.",
                    L"Aqua Frame", MB_OK | MB_ICONERROR);
        return 1;
    }
    const auto gate = aqua::LockProfile(root);
    if (gate == INVALID_HANDLE_VALUE) {
        MessageBoxW(nullptr, L"Профиль Aqua Frame уже используется или недоступен.",
                    L"Aqua Frame", MB_OK | MB_ICONWARNING);
        return 1;
    }
    if (GetFileAttributesW((root.parent_path() / L"Reset Pending").c_str()) != INVALID_FILE_ATTRIBUTES) {
        const bool started = aqua::StartResetHelper(gate);
        CloseHandle(gate);
        if (!started) MessageBoxW(nullptr, L"Не удалось продолжить заводской сброс.",
                                  L"Aqua Frame", MB_OK | MB_ICONERROR);
        return started ? 0 : 1;
    }
    if (GetFileAttributesW(root.c_str()) != INVALID_FILE_ATTRIBUTES && !aqua::IsPlainDirectory(root)) {
        CloseHandle(gate);
        MessageBoxW(nullptr, L"Папка профиля Aqua Frame перенаправлена.",
                    L"Aqua Frame", MB_OK | MB_ICONERROR);
        return 1;
    }
    app->SetProfile(root);
    CefSettings settings;
    settings.command_line_args_disabled = true;
    CefString(&settings.root_cache_path) = root.wstring();
    CefString(&settings.cache_path) = root.wstring();
    CefString(&settings.log_file) = (root / L"cef.log").wstring();
    if (!CefInitialize(args, settings, app, sandbox_info)) {
        CloseHandle(gate);
        return CefGetExitCode();
    }
    if (!app->Failed()) CefRunMessageLoop();
    CefShutdown();
    const bool reset = app->ResetRequested();
    const bool started = !reset || aqua::StartResetHelper(gate);
    CloseHandle(gate);
    if (!started) {
        MessageBoxW(nullptr, L"Не удалось запустить помощник очистки. Данные сохранены.",
                    L"Aqua Frame", MB_OK | MB_ICONERROR);
    }
    return app->Failed() || !started ? 1 : 0;
}
