#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace aqua {
struct Script {
    std::string id;
    std::string name;
    std::string origin;
    std::string source;
    bool enabled = true;
};

std::string WebOrigin(std::string_view url);
bool IsSupportedScript(const std::string& source);
bool RunsOn(const Script& script, std::string_view url);

class ScriptLibrary {
public:
    const std::vector<Script>& Items() const { return items_; }
    void Load(const std::filesystem::path& profile);
    bool Add(const std::filesystem::path& path, std::string origin, Script& item) const;
    bool Save(std::vector<Script> next);

private:
    std::filesystem::path profile_;
    std::vector<Script> items_;
};
}
