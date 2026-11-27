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
bool IsJavaScriptFilename(std::wstring_view name);

enum class ScriptImportError { none, capacity, file, size, encoding, metadata, grants, dependencies, execution_time, storage };
const wchar_t* ScriptImportErrorText(ScriptImportError error);

class ScriptLibrary {
public:
    const std::vector<Script>& Items() const { return items_; }
    void Load(const std::filesystem::path& profile);
    ScriptImportError PrepareFile(const std::filesystem::path& path, std::string origin, Script& item) const;
    ScriptImportError PrepareSource(std::wstring_view name, std::string source, std::string origin, Script& item) const;
    bool Save(std::vector<Script> next);

private:
    std::filesystem::path profile_;
    std::vector<Script> items_;
};
}
