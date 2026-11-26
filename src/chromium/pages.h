#pragma once
#include "include/cef_app.h"
#include "../settings/preferences.h"
#include "../scripts/library.h"

namespace aqua {
void RegisterAquaScheme(CefRawPtr<CefSchemeRegistrar> registrar);
bool RegisterAquaPages();
void UpdateAquaPageSettings(const Preferences& settings);
void UpdateAquaScriptList(const std::vector<Script>& scripts);
}
