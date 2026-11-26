#pragma once
#include "preferences.h"
#include "../scripts/library.h"

namespace aqua {
enum class SettingsAction {
    restore_on, restore_off, google, bing, duckduckgo, zoom_90, zoom_100, zoom_110, zoom_125,
    startup_new_tab, startup_settings, theme_ocean, theme_contrast, limit_restore_on, limit_restore_off,
    open_extensions, open_downloads, reset_preferences, factory_reset, clear_history,
    animations_on, animations_off, https_on, https_off,
    warm_restore_on, warm_restore_off, tab_unload_on, tab_unload_off,
    high_gpu_on, high_gpu_off, open_gpu_report, unknown
};
SettingsAction ParseSettingsAction(std::string_view url);
std::string RenderSettings(const Preferences& settings, const std::vector<Script>& scripts = {});
}
