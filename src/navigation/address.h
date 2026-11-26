#pragma once
#include "../settings/preferences.h"

#include <string>
#include <string_view>

namespace aqua {
std::wstring ResolveAddress(std::wstring_view input, SearchEngine engine);
}
