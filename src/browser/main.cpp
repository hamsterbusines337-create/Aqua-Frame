#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include "include/cef_app.h"
#include "include/cef_sandbox_win.h"
#include "../chromium/pages.h"
#include "../ui/window.h"

namespace {
class App final : public CefApp, public CefBrowserProcessHandler {
public:
    App(HINSTANCE instance, int show) : instance_(instance), show_(show) {}
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
    void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override {
        aqua::RegisterAquaScheme(registrar);
    }
    void OnContextInitialized() override {
        if (!aqua::RegisterAquaPages() || !window_.Create(instance_, show_)) {
            failed_ = true;
        }
    }
    bool Failed() const { return failed_; }

private:
    HINSTANCE instance_;
    int show_;
    aqua::Window window_;
    bool failed_ = false;
    IMPLEMENT_REFCOUNTING(App);
};

std::filesystem::path ProfileRoot() {
    PWSTR data = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &data)))
        return {};
    const auto path = std::filesystem::path(data) / L"Aqua Frame" / L"User Data";
    CoTaskMemFree(data);
    return path;
}
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
    const auto root = ProfileRoot();
    if (root.empty()) return 1;
    CefSettings settings;
    settings.command_line_args_disabled = true;
    CefString(&settings.root_cache_path) = root.wstring();
    CefString(&settings.cache_path) = root.wstring();
    CefString(&settings.log_file) = (root / L"cef.log").wstring();
    if (!CefInitialize(args, settings, app, sandbox_info)) return CefGetExitCode();
    if (!app->Failed()) CefRunMessageLoop();
    CefShutdown();
    return app->Failed() ? 1 : 0;
}
