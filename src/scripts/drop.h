#pragma once

#include "include/cef_v8.h"

namespace aqua {
inline constexpr auto script_drop_message = "AquaScripts.Drop";
void BindScriptDrop(CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context);
}
