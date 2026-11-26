#pragma once

#include "library.h"
#include "include/cef_browser.h"
#include "include/cef_devtools_message_observer.h"

namespace aqua {
class ScriptRun final : public CefDevToolsMessageObserver {
public:
    ScriptRun(CefRefPtr<CefBrowserHost> host, std::string url, std::vector<Script> scripts);
    void Start();
    void Cancel();
    void OnDevToolsMethodResult(CefRefPtr<CefBrowser> browser, int message_id,
                                bool success, const void* result, size_t result_size) override;

private:
    enum class Stage { frame_tree, world, execute, finished };
    void Send(const char* method, CefRefPtr<CefDictionaryValue> parameters);
    void Evaluate(int context);

    CefRefPtr<CefBrowserHost> host_;
    CefRefPtr<CefRegistration> registration_;
    std::string url_;
    const std::vector<Script> scripts_;
    Stage stage_ = Stage::frame_tree;
    int request_ = 0;

    IMPLEMENT_REFCOUNTING(ScriptRun);
};
}
