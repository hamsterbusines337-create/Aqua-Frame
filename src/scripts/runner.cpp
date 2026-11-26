#include "runner.h"

#include "include/cef_parser.h"

namespace aqua {
namespace {
std::string JsString(const std::string& value) {
    auto json = CefValue::Create();
    json->SetString(value);
    return CefWriteJSON(json, JSON_WRITER_DEFAULT).ToString();
}
}

ScriptRun::ScriptRun(CefRefPtr<CefBrowserHost> host, std::string url, std::vector<Script> scripts)
    : host_(host), url_(std::move(url)), scripts_(std::move(scripts)) {}

void ScriptRun::Start() {
    registration_ = host_->AddDevToolsMessageObserver(this);
    if (registration_) Send("Page.getFrameTree", nullptr);
}

void ScriptRun::Cancel() {
    stage_ = Stage::finished;
    registration_ = nullptr;
    host_ = nullptr;
}

void ScriptRun::Send(const char* method, CefRefPtr<CefDictionaryValue> parameters) {
    // Each run is controlled on CEF's browser UI thread. Assign the identifier
    // before dispatch in case a browser-side command returns synchronously.
    static int next_id = 10000;
    if (next_id == 2147483647) next_id = 10000;
    request_ = ++next_id;
    if (!host_->ExecuteDevToolsMethod(request_, method, parameters))
        stage_ = Stage::finished;
}

void ScriptRun::OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int message_id,
                                      bool success, const void* result, size_t result_size) {
    if (stage_ == Stage::finished || message_id != request_) return;
    if (!success || !result || !result_size || result_size > 1024 * 1024) {
        stage_ = Stage::finished;
        return;
    }
    auto value = CefParseJSON(std::string(static_cast<const char*>(result), result_size), JSON_PARSER_RFC);
    if (!value || value->GetType() != VTYPE_DICTIONARY) {
        stage_ = Stage::finished;
        return;
    }
    auto data = value->GetDictionary();
    if (stage_ == Stage::frame_tree) {
        auto tree = data->GetDictionary("frameTree");
        auto frame = tree ? tree->GetDictionary("frame") : nullptr;
        if (!frame || frame->GetString("url").ToString() != url_.substr(0, url_.find('#'))) {
            stage_ = Stage::finished;
            return;
        }
        auto options = CefDictionaryValue::Create();
        options->SetString("frameId", frame->GetString("id"));
        options->SetString("worldName", "Aqua Frame User Scripts");
        stage_ = Stage::world;
        Send("Page.createIsolatedWorld", options);
        return;
    }
    if (stage_ == Stage::world) {
        const int context = data->GetInt("executionContextId");
        if (context > 0) Evaluate(context);
        else stage_ = Stage::finished;
        return;
    }
    stage_ = Stage::finished;
}

void ScriptRun::Evaluate(int context) {
    auto source = "(function(){const page=" + JsString(url_) +
        ";if (self!==top || location.href!==page) return;";
    for (const auto& script : scripts_) {
        // The source is serialized as data. Script syntax cannot alter the URL
        // check, which also guards against a navigation during world creation.
        source += "if(location.href===page){try{(0,eval)(" + JsString(script.source) +
                  ");}catch(error){console.error('Aqua Scripts',error);}}";
    }
    source += "})()";
    auto options = CefDictionaryValue::Create();
    options->SetInt("contextId", context);
    options->SetString("expression", source);
    options->SetBool("returnByValue", true);
    options->SetBool("silent", true);
    stage_ = Stage::execute;
    Send("Runtime.evaluate", options);
}
}
