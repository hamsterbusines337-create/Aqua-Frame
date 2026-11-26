#pragma once
#include "include/cef_app.h"

namespace aqua {
void RegisterAquaScheme(CefRawPtr<CefSchemeRegistrar> registrar);
bool RegisterAquaPages();
}
