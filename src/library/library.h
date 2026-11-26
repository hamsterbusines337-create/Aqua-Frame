#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace aqua {
struct PageEntry {
    std::wstring url;
    std::wstring title;
};
struct DownloadEntry {
    unsigned id;
    std::wstring label;
    bool active;
    bool paused;
};
std::string HtmlEscape(std::string_view text);
bool IsLibraryUrl(std::wstring_view url);
std::string RenderLibrary(const std::vector<PageEntry>& history, const std::vector<PageEntry>& bookmarks,
                          const std::vector<DownloadEntry>& downloads, std::string_view url);
}
