#pragma once
#include "preferences.h"

namespace aqua {
enum class SettingsAction {
    restore_on, restore_off, google, bing, duckduckgo, zoom_90, zoom_100, zoom_110, zoom_125,
    startup_new_tab, startup_settings, theme_ocean, theme_contrast, limit_restore_on, limit_restore_off,
    open_extensions, open_downloads, reset_preferences, factory_reset, unknown
};
SettingsAction ParseSettingsAction(std::string_view url);
std::string RenderSettings(const Preferences& settings);
}
