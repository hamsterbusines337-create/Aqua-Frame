#pragma once

#include "library.h"
#include "include/cef_browser.h"
#include "include/cef_devtools_message_observer.h"
#include <functional>
#include <optional>

namespace aqua {
struct NavigationTiming {
    double dns = 0;
    double connection = 0;
    double tls = 0;
    double first_byte = 0;
    double document = 0;
    double page_fps = 0;
    double page_frame_ms = 0;
    double low_fps = 0;
    double sample_ms = 0;
    double p95_frame_ms = 0;
    double p99_frame_ms = 0;
    double over_120_percent = 0;
    int intervals = 0;
    int long_animation_frames = -1;
};

class ScriptRun final : public CefDevToolsMessageObserver {
public:
    using TimingCallback = std::function<void(std::optional<NavigationTiming>)>;
    ScriptRun(CefRefPtr<CefBrowserHost> host, std::string url, std::vector<Script> scripts);
    ScriptRun(CefRefPtr<CefBrowserHost> host, std::string url, TimingCallback callback);
    void Start();
    void Cancel();
    void OnDevToolsMethodResult(CefRefPtr<CefBrowser> browser, int message_id,
                                bool success, const void* result, size_t result_size) override;

private:
    enum class Stage { frame_tree, world, execute, finished };
    void Send(const char* method, CefRefPtr<CefDictionaryValue> parameters);
    void Evaluate(int context);
    void Finish(std::optional<NavigationTiming> timing = std::nullopt);

    CefRefPtr<CefBrowserHost> host_;
    CefRefPtr<CefRegistration> registration_;
    std::string url_;
    const std::vector<Script> scripts_;
    TimingCallback timing_;
    Stage stage_ = Stage::frame_tree;
    int request_ = 0;

    IMPLEMENT_REFCOUNTING(ScriptRun);
};
}
