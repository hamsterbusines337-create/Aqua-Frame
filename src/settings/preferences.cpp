#include "preferences.h"

#include "include/cef_parser.h"

#include <filesystem>
#include <fstream>
#include <windows.h>

namespace aqua {
namespace {
constexpr auto file_name = L"aqua-settings.json";
constexpr size_t max_size = 4 * 1024 * 1024;
constexpr size_t max_entries = 128;

std::vector<PageEntry> ReadEntries(CefRefPtr<CefDictionaryValue> dictionary, const char* key) {
    std::vector<PageEntry> entries;
    auto list = dictionary->GetList(key);
    if (!list) return entries;
    for (size_t i = 0; i < list->GetSize() && entries.size() < max_entries; ++i) {
        auto item = list->GetDictionary(i);
        if (!item) continue;
        auto url = item->GetString("url").ToWString();
        if (IsLibraryUrl(url)) entries.push_back({std::move(url), item->GetString("title").ToWString().substr(0, 160)});
    }
    return entries;
}

CefRefPtr<CefListValue> WriteEntries(const std::vector<PageEntry>& entries) {
    auto list = CefListValue::Create();
    for (const auto& entry : entries) {
        auto item = CefDictionaryValue::Create();
        item->SetString("url", entry.url);
        item->SetString("title", entry.title);
        list->SetDictionary(list->GetSize(), item);
    }
    return list;
}
}

std::string_view SearchName(SearchEngine engine) {
    switch (engine) {
    case SearchEngine::bing: return "Bing";
    case SearchEngine::duckduckgo: return "DuckDuckGo";
    default: return "Google";
    }
}

std::string_view SearchEndpoint(SearchEngine engine) {
    switch (engine) {
    case SearchEngine::bing: return "https://www.bing.com/search";
    case SearchEngine::duckduckgo: return "https://duckduckgo.com/";
    default: return "https://www.google.com/search";
    }
}

bool IsSessionAddress(std::wstring_view url) {
    return url.empty() || (url.size() <= 8192 &&
        (url == L"aqua://newtab/" || url.starts_with(L"https://") || url.starts_with(L"http://")));
}

Preferences ReadPreferences(const std::filesystem::path& profile) {
    Preferences result;
    const auto file = profile / file_name;
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    if (error || size == 0 || size > max_size) return result;
    std::ifstream input(file, std::ios::binary);
    std::string json(static_cast<size_t>(size), '\0');
    if (!input.read(json.data(), static_cast<std::streamsize>(json.size()))) return result;

    auto root = CefParseJSON(json, JSON_PARSER_RFC);
    if (!root || root->GetType() != VTYPE_DICTIONARY) return result;

    auto dictionary = root->GetDictionary();
    auto search = dictionary->GetString("search").ToString();
    if (search == "Bing") result.search = SearchEngine::bing;
    if (search == "DuckDuckGo") result.search = SearchEngine::duckduckgo;
    if (dictionary->GetString("startup_page") == "settings") result.startup_page = StartupPage::settings;
    if (dictionary->GetString("theme") == "contrast") result.theme = Theme::contrast;
    result.restore_session = dictionary->GetBool("restore_session");
    result.limit_restored_tabs = dictionary->GetBool("limit_restored_tabs");
    result.warm_restored_tab = dictionary->GetBool("warm_restored_tab");
    result.allow_tab_unload = !dictionary->HasKey("allow_tab_unload") ||
        dictionary->GetBool("allow_tab_unload");
    result.animations = !dictionary->HasKey("animations") || dictionary->GetBool("animations");
    result.https_only = dictionary->GetBool("https_only");
    result.history = ReadEntries(dictionary, "history");
    result.bookmarks = ReadEntries(dictionary, "bookmarks");
    const int zoom = dictionary->GetInt("zoom");
    if (zoom == 90 || zoom == 100 || zoom == 110 || zoom == 125) result.zoom = zoom;

    if (result.restore_session) {
        if (auto restored = dictionary->GetList("session")) {
            for (size_t i = 0; i < restored->GetSize() && i < 64; ++i) {
                auto url = restored->GetString(i).ToWString();
                if (!url.empty() && IsSessionAddress(url)) result.session.push_back(std::move(url));
            }
        }
    }
    return result;
}

bool WritePreferences(const std::filesystem::path& profile, const Preferences& preferences) {
    auto root = CefValue::Create();
    auto dictionary = CefDictionaryValue::Create();
    dictionary->SetString("search", std::string(SearchName(preferences.search)));
    dictionary->SetString("startup_page", preferences.startup_page == StartupPage::settings ? "settings" : "new_tab");
    dictionary->SetString("theme", preferences.theme == Theme::contrast ? "contrast" : "ocean");
    dictionary->SetBool("restore_session", preferences.restore_session);
    dictionary->SetBool("limit_restored_tabs", preferences.limit_restored_tabs);
    dictionary->SetBool("warm_restored_tab", preferences.warm_restored_tab);
    dictionary->SetBool("allow_tab_unload", preferences.allow_tab_unload);
    dictionary->SetInt("zoom", preferences.zoom);
    dictionary->SetBool("animations", preferences.animations);
    dictionary->SetBool("https_only", preferences.https_only);
    dictionary->SetList("history", WriteEntries(preferences.history));
    dictionary->SetList("bookmarks", WriteEntries(preferences.bookmarks));
    auto session = CefListValue::Create();
    if (preferences.restore_session) {
        for (const auto& url : preferences.session) {
            if (!url.empty() && IsSessionAddress(url)) session->SetString(session->GetSize(), url);
        }
    }
    dictionary->SetList("session", session);
    root->SetDictionary(dictionary);
    const auto bytes = CefWriteJSON(root, JSON_WRITER_DEFAULT).ToString();
    if (bytes.empty() || bytes.size() > max_size) return false;
    const auto staged = profile / L"aqua-settings.tmp";
    const auto file = profile / file_name;
    // The locked profile can contain a staging file from an interrupted write.
    // Remove the directory entry before creating a new file.
    if (!DeleteFileW(staged.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) return false;
    const HANDLE handle = CreateFileW(staged.c_str(), GENERIC_WRITE, 0, nullptr,
                                      CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
        && written == bytes.size() && FlushFileBuffers(handle);
    CloseHandle(handle);
    if (ok && MoveFileExW(staged.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    DeleteFileW(staged.c_str());
    return false;
}
}
