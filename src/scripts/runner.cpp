#include "runner.h"

#include "include/cef_parser.h"
#include <utility>

namespace aqua {
namespace {
std::string JsString(const std::string& value) {
    auto json = CefValue::Create();
    json->SetString(value);
    return CefWriteJSON(json, JSON_WRITER_DEFAULT).ToString();
}
double ReadNumber(CefRefPtr<CefDictionaryValue> values, const char* key) {
    return values->GetType(key) == VTYPE_INT ? values->GetInt(key) : values->GetDouble(key);
}
}

ScriptRun::ScriptRun(CefRefPtr<CefBrowserHost> host, std::string url, std::vector<Script> scripts)
    : host_(host), url_(std::move(url)), scripts_(std::move(scripts)) {}

ScriptRun::ScriptRun(CefRefPtr<CefBrowserHost> host, std::string url, TimingCallback callback)
    : host_(host), url_(std::move(url)), timing_(std::move(callback)) {}

void ScriptRun::Start() {
    registration_ = host_->AddDevToolsMessageObserver(this);
    if (registration_) Send("Page.getFrameTree", nullptr);
    else Finish();
}

void ScriptRun::Cancel() {
    stage_ = Stage::finished;
    timing_ = nullptr;
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
        Finish();
}

void ScriptRun::OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int message_id,
                                      bool success, const void* result, size_t result_size) {
    if (stage_ == Stage::finished || message_id != request_) return;
    if (!success || !result || !result_size || result_size > 1024 * 1024) {
        Finish();
        return;
    }
    auto value = CefParseJSON(std::string(static_cast<const char*>(result), result_size), JSON_PARSER_RFC);
    if (!value || value->GetType() != VTYPE_DICTIONARY) {
        Finish();
        return;
    }
    auto data = value->GetDictionary();
    if (stage_ == Stage::frame_tree) {
        auto tree = data->GetDictionary("frameTree");
        auto frame = tree ? tree->GetDictionary("frame") : nullptr;
        if (!frame || frame->GetString("url").ToString() != url_.substr(0, url_.find('#'))) {
            Finish();
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
        else Finish();
        return;
    }
    auto response = data->GetDictionary("result");
    auto timing = response ? response->GetDictionary("value") : nullptr;
    if (timing_ && timing && timing->GetBool("valid")) {
        Finish(NavigationTiming{ReadNumber(timing, "dns"), ReadNumber(timing, "connection"),
            ReadNumber(timing, "tls"), ReadNumber(timing, "firstByte"), ReadNumber(timing, "document"),
            ReadNumber(timing, "fps"), ReadNumber(timing, "frameMs"), ReadNumber(timing, "lowFps"),
            ReadNumber(timing, "sampleMs"), ReadNumber(timing, "p95FrameMs"),
            ReadNumber(timing, "p99FrameMs"), ReadNumber(timing, "over120Percent"),
            timing->GetInt("intervals"), timing->HasKey("longAnimationFrames") ?
                timing->GetInt("longAnimationFrames") : -1});
    } else {
        Finish();
    }
}

void ScriptRun::Finish(std::optional<NavigationTiming> timing) {
    stage_ = Stage::finished;
    if (auto callback = std::exchange(timing_, nullptr)) callback(timing);
}

void ScriptRun::Evaluate(int context) {
    auto source = "(function(){const page=" + JsString(url_) +
        ";if (self!==top || location.href!==page) return;";
    if (timing_) source += R"JS(
const entry = performance.getEntriesByType('navigation')[0];
if (!entry || !entry.responseStart) return;
const result = {valid:true,dns:entry.domainLookupEnd-entry.domainLookupStart,
connection:entry.connectEnd-entry.connectStart,
tls:entry.secureConnectionStart ? entry.connectEnd-entry.secureConnectionStart : 0,
firstByte:entry.responseStart-entry.requestStart,
document:entry.loadEventEnd};
if (document.visibilityState !== 'visible') return result;
return new Promise(resolve => {
    const deltas = [];
    let longAnimationFrames = 0;
    const observeLongFrames = PerformanceObserver.supportedEntryTypes.includes('long-animation-frame');
    const observer = observeLongFrames ? new PerformanceObserver(list => {
        longAnimationFrames += list.getEntries().length;
    }) : null;
    observer?.observe({type:'long-animation-frame'});
    const start = performance.now();
    let last = 0;
    let animation = 0;
    let timeout = 0;
    const finish = () => {
        clearTimeout(timeout);
        cancelAnimationFrame(animation);
        document.removeEventListener('visibilitychange', finish);
        if (observer) {
            longAnimationFrames += observer.takeRecords().length;
            observer.disconnect();
        }
        const elapsed = performance.now() - start;
        if (deltas.length < 5 || elapsed < 4900 ||
            document.visibilityState !== 'visible' || location.href !== page) {
            resolve(result);
            return;
        }
        result.sampleMs = elapsed;
        result.intervals = deltas.length;
        if (observer) result.longAnimationFrames = longAnimationFrames;
        result.frameMs = deltas.reduce((sum, delta) => sum + delta, 0) / deltas.length;
        result.fps = 1000 / result.frameMs;
        // Allow for small timestamp and scheduler variation around 120 Hz.
        const budget = 1000 / 120 + 0.5;
        result.over120Percent = 100 * deltas.filter(delta => delta > budget).length / deltas.length;
        deltas.sort((a, b) => a - b);
        result.p95FrameMs = deltas[Math.ceil(deltas.length * .95) - 1];
        result.p99FrameMs = deltas[Math.ceil(deltas.length * .99) - 1];
        if (deltas.length >= 100) {
            const slowest = deltas.slice(-Math.ceil(deltas.length / 100));
            result.lowFps = 1000 * slowest.length / slowest.reduce((sum, delta) => sum + delta, 0);
        }
        resolve(result);
    };
    const frame = timestamp => {
        if (last && timestamp > last) deltas.push(timestamp - last);
        last = timestamp;
        animation = requestAnimationFrame(frame);
    };
    document.addEventListener('visibilitychange', finish, {once:true});
    animation = requestAnimationFrame(frame);
    timeout = setTimeout(finish, 5000);
});
)JS";
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
    if (timing_) options->SetBool("awaitPromise", true);
    stage_ = Stage::execute;
    Send("Runtime.evaluate", options);
}
}
