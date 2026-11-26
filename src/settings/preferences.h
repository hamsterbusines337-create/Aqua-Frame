#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

namespace aqua {
enum class SearchEngine { google, bing, duckduckgo };

struct Preferences {
    SearchEngine search = SearchEngine::google;
    bool restore_session = false;
    int zoom = 100;
    std::vector<std::wstring> session;
};

std::string_view SearchName(SearchEngine engine);
std::string_view SearchEndpoint(SearchEngine engine);
bool IsSessionAddress(std::wstring_view url);
Preferences ReadPreferences(const std::filesystem::path& profile);
bool WritePreferences(const std::filesystem::path& profile, const Preferences& preferences);
}
