#pragma once
#include "include/cef_app.h"
#include "include/cef_request_context.h"
#include "../settings/preferences.h"
#include "../scripts/library.h"

namespace aqua {
void RegisterAquaScheme(CefRawPtr<CefSchemeRegistrar> registrar);
bool RegisterAquaPages(CefRefPtr<CefRequestContext> context = nullptr);
void UpdateAquaPageSettings(const Preferences& settings);
void UpdateAquaScriptList(const std::vector<Script>& scripts);
void UpdateAquaLibrary(const std::vector<PageEntry>& history, const std::vector<PageEntry>& bookmarks,
                       const std::vector<DownloadEntry>& downloads);
}
