#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace aqua {
struct PageEntry {
    std::wstring url;
    std::wstring title;
};
std::string HtmlEscape(std::string_view text);
bool IsLibraryUrl(std::wstring_view url);
std::string RenderLibrary(const std::vector<PageEntry>& history, const std::vector<PageEntry>& bookmarks,
                          const std::vector<std::wstring>& downloads, std::string_view url);
}
