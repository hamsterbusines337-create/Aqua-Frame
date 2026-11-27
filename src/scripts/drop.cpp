#include "drop.h"
#include "include/cef_frame.h"
#include "include/cef_process_message.h"

namespace aqua {
namespace {
class DropHandler final : public CefV8Handler {
public:
    bool Execute(const CefString&, CefRefPtr<CefV8Value>, const CefV8ValueList& arguments,
                 CefRefPtr<CefV8Value>& result, CefString&) override {
        auto context = CefV8Context::GetCurrentContext();
        auto frame = context ? context->GetFrame() : nullptr;
        if (!frame || !frame->IsMain() || frame->GetURL() != "aqua://settings/#scripts" ||
            arguments.size() != 2 || !arguments[0]->IsString() || !arguments[1]->IsString()) return false;
        const auto name = arguments[0]->GetStringValue();
        const auto source = arguments[1]->GetStringValue();
        if (name.length() > 255 || source.length() > 512 * 1024) return false;
        auto message = CefProcessMessage::Create(script_drop_message);
        auto values = message->GetArgumentList();
        values->SetString(0, name);
        values->SetString(1, source);
        frame->SendProcessMessage(PID_BROWSER, message);
        result = CefV8Value::CreateBool(true);
        return true;
    }
private:
    IMPLEMENT_REFCOUNTING(DropHandler);
};
}

void BindScriptDrop(CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) {
    if (!frame->IsMain()) return;
    const auto url = frame->GetURL().ToString();
    if (url != "aqua://settings/" && !url.starts_with("aqua://settings/#")) return;
    context->GetGlobal()->SetValue("aquaImportDropped", CefV8Value::CreateFunction("aquaImportDropped", new DropHandler),
                                    static_cast<CefV8Value::PropertyAttribute>(
                                        V8_PROPERTY_ATTRIBUTE_READONLY | V8_PROPERTY_ATTRIBUTE_DONTDELETE));
}
}
