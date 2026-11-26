#pragma once
#include "preferences.h"

namespace aqua {
enum class SettingsAction {
    restore_on, restore_off, google, bing, duckduckgo, zoom_90, zoom_100, zoom_110, zoom_125,
    open_extensions, reset_preferences, factory_reset, unknown
};
SettingsAction ParseSettingsAction(std::string_view url);
std::string RenderSettings(const Preferences& settings);
}
