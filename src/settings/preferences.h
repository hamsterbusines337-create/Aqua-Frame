#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

namespace aqua {
enum class SearchEngine { google, bing, duckduckgo };
enum class StartupPage { new_tab, settings };
enum class Theme { ocean, contrast };

struct Preferences {
    SearchEngine search = SearchEngine::google;
    StartupPage startup_page = StartupPage::new_tab;
    Theme theme = Theme::ocean;
    bool restore_session = false;
    bool limit_restored_tabs = false;
    int zoom = 100;
    std::vector<std::wstring> session;
};

std::string_view SearchName(SearchEngine engine);
std::string_view SearchEndpoint(SearchEngine engine);
bool IsSessionAddress(std::wstring_view url);
Preferences ReadPreferences(const std::filesystem::path& profile);
bool WritePreferences(const std::filesystem::path& profile, const Preferences& preferences);
}
