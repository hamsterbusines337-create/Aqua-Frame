#include "library.h"

#include "include/cef_parser.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <objbase.h>
#include <windows.h>

namespace aqua {
namespace {
constexpr size_t max_script = 512 * 1024;
constexpr size_t max_scripts = 16;
constexpr size_t max_archive = 64 * 1024 * 1024;

std::string ReadBounded(const std::filesystem::path& file, size_t limit) {
    const auto attributes = GetFileAttributesW(file.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return {};

    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    if (error || !size || size > limit) return {};
    std::ifstream input(file, std::ios::binary);
    std::string text(static_cast<size_t>(size), '\0');
    if (!input.read(text.data(), static_cast<std::streamsize>(text.size()))) return {};
    return text;
}

bool ValidEncoding(const std::string& text) {
    return !text.empty() && text.find('\0') == std::string::npos &&
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), nullptr, 0) > 0;
}

bool ValidId(std::string_view id) {
    return id.size() == 36 &&
        id.find_first_not_of("0123456789abcdefABCDEF-") == std::string_view::npos;
}

std::string_view Trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}

template<class Callback>
bool ReadMetadata(std::string_view source, Callback callback) {
    constexpr auto start = "// ==UserScript==";
    constexpr auto end = "// ==/UserScript==";
    if (source.starts_with("\xef\xbb\xbf")) source.remove_prefix(3);
    const auto first = source.find_first_not_of(" \t\r\n");
    if (first != std::string_view::npos) source.remove_prefix(first);
    const auto first_line = source.find('\n');
    if (Trim(source.substr(0, first_line)) != start) return source.find(start) == std::string_view::npos;
    if (first_line == std::string_view::npos) return false;
    source.remove_prefix(first_line + 1);
    for (int i = 0; !source.empty() && i < 100; ++i) {
        const auto n = source.find('\n');
        auto line = Trim(source.substr(0, n));
        if (line == end) return true;
        if (line.starts_with("//")) {
            line = Trim(line.substr(2));
            if (line.starts_with('@')) {
                const auto split = line.find_first_of(" \t");
                if (!callback(line.substr(0, split), split == std::string_view::npos
                    ? std::string_view{} : Trim(line.substr(split + 1)))) return false;
            }
        }
        if (n == std::string_view::npos) break;
        source.remove_prefix(n + 1);
    }
    return false;
}

bool Glob(std::string_view pattern, std::string_view value) {
    size_t p = 0, v = 0, star = std::string_view::npos, retry = 0;
    while (v < value.size()) {
        if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            retry = v;
        } else if (p < pattern.size() && pattern[p] == value[v]) {
            ++p;
            ++v;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            v = ++retry;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

bool PatternMatches(std::string_view pattern, std::string_view url, bool extension_match) {
    if (pattern == "<all_urls>") return true;
    if (!extension_match) return Glob(pattern, url);
    const auto marker = pattern.find("://");
    const auto separator = url.find("://");
    if (marker == std::string_view::npos || separator == std::string_view::npos) return false;
    const auto scheme = pattern.substr(0, marker);
    if (scheme != "*" && scheme != url.substr(0, separator)) return false;
    const auto host_start = marker + 3;
    const auto slash = pattern.find('/', host_start);
    if (slash == std::string_view::npos) return false;
    auto host_pattern = pattern.substr(host_start, slash - host_start);
    CefURLParts parts;
    if (!CefParseURL(std::string(url), parts)) return false;
    const auto host = CefString(&parts.host).ToString();
    if (host_pattern.starts_with("*.")) {
        host_pattern.remove_prefix(2);
        if (host != host_pattern &&
            !(host.size() > host_pattern.size() && host.ends_with(host_pattern) &&
              host[host.size() - host_pattern.size() - 1] == '.')) return false;
    } else if (host_pattern != "*" && host != host_pattern) {
        return false;
    }
    const auto requested_path = url.find('/', separator + 3);
    const auto path = requested_path == std::string_view::npos ? std::string_view{"/"} :
        url.substr(requested_path);
    return Glob(pattern.substr(slash), path.substr(0, path.find('#')));
}

bool WriteFileAtomically(const std::filesystem::path& file, const std::string& content) {
    const auto temp = file.parent_path() / L"aqua-scripts.tmp";
    if (!DeleteFileW(temp.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) return false;
    HANDLE handle = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(handle, content.data(), static_cast<DWORD>(content.size()),
                         &written, nullptr) && written == content.size() && FlushFileBuffers(handle);
    CloseHandle(handle);
    if (ok && MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    DeleteFileW(temp.c_str());
    return false;
}
}

std::string WebOrigin(std::string_view url) {
    CefURLParts parts;
    if (!CefParseURL(std::string(url), parts)) return {};
    const auto scheme = CefString(&parts.scheme).ToString();
    if (scheme != "https" && scheme != "http") return {};
    if (CefString(&parts.host).empty() || !CefString(&parts.username).empty() ||
        !CefString(&parts.password).empty()) return {};
    auto origin = CefString(&parts.origin).ToString();
    if (origin.ends_with('/')) origin.pop_back();
    return origin;
}

static ScriptImportError ValidateScript(const std::string& source) {
    using Error = ScriptImportError;
    if (source.size() > max_script) return Error::size;
    if (!ValidEncoding(source)) return Error::encoding;
    auto error = Error::metadata;
    const auto valid = ReadMetadata(source, [&](std::string_view key, std::string_view value) {
            if (key == "@require" || key == "@resource") { error = Error::dependencies; return false; }
            if (key == "@unwrap" || (key == "@grant" && value != "none")) { error = Error::grants; return false; }
            if (key == "@run-at" && value != "document-end" && value != "document-idle") {
                error = Error::execution_time;
                return false;
            }
            if (key == "@include" || key == "@exclude")
                return !value.empty() && value.size() <= 512 && !value.starts_with('/');
            if (key == "@match" || key == "@exclude-match") {
                if (value == "<all_urls>") return true;
                if (value.empty() || value.size() > 512) return false;
                const auto marker = value.find("://");
                if (marker == std::string_view::npos) return false;
                const auto scheme = value.substr(0, marker);
                if (scheme != "http" && scheme != "https" && scheme != "*") return false;
                const auto slash = value.find('/', marker + 3);
                if (slash == std::string_view::npos || slash == marker + 3) return false;
                auto host = value.substr(marker + 3, slash - marker - 3);
                if (host == "*") return true;
                if (host.starts_with("*.")) host.remove_prefix(2);
                return !host.empty() && host.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-") ==
                    std::string_view::npos;
            }
            return true;
    });
    return valid ? Error::none : error;
}

bool IsSupportedScript(const std::string& source) {
    return ValidateScript(source) == ScriptImportError::none;
}

bool IsJavaScriptFilename(std::wstring_view name) {
    return name.size() > 3 && CompareStringOrdinal(name.data() + name.size() - 3, 3, L".js", 3, TRUE) == CSTR_EQUAL;
}

const wchar_t* ScriptImportErrorText(ScriptImportError error) {
    switch (error) {
    case ScriptImportError::capacity: return L"В библиотеке уже 16 скриптов. Удалите один перед импортом.";
    case ScriptImportError::file: return L"Выберите существующий локальный файл .js или .user.js.";
    case ScriptImportError::size: return L"Файл превышает лимит 512 КиБ.";
    case ScriptImportError::encoding: return L"Файл пустой или сохранён в другой кодировке. Нужен JavaScript в UTF-8.";
    case ScriptImportError::metadata: return L"Заголовок UserScript или шаблоны адресов имеют неподдерживаемый формат.";
    case ScriptImportError::grants: return L"Скрипт запрашивает GM API или @unwrap. Aqua Scripts поддерживает @grant none.";
    case ScriptImportError::dependencies: return L"Скрипту нужны внешние зависимости @require или @resource. Aqua Scripts их не загружает.";
    case ScriptImportError::execution_time: return L"Скрипт запрашивает время запуска, которое Aqua Scripts не поддерживает. Доступны document-end и document-idle.";
    default: return L"Не удалось подготовить скрипт к сохранению.";
    }
}

bool RunsOn(const Script& script, std::string_view url) {
    if (!script.enabled || script.origin != WebOrigin(url)) return false;
    bool has_include = false;
    bool included = false;
    bool excluded = false;
    const bool valid = ReadMetadata(script.source, [&](std::string_view key, std::string_view value) {
        if (key == "@match" || key == "@include") {
            has_include = true;
            included = included || PatternMatches(value, url, key == "@match");
        } else if (key == "@exclude" || key == "@exclude-match") {
            excluded = excluded || PatternMatches(value, url, key == "@exclude-match");
        }
        return true;
    });
    return valid && !excluded && (!has_include || included);
}

void ScriptLibrary::Load(const std::filesystem::path& profile) {
    profile_ = profile;
    items_.clear();
    const auto data = ReadBounded(profile_ / L"aqua-scripts.json", max_archive);
    if (data.empty()) return;
    auto root = CefParseJSON(data, JSON_PARSER_RFC);
    if (!root || root->GetType() != VTYPE_LIST) return;
    auto list = root->GetList();
    for (size_t i = 0; i < list->GetSize() && items_.size() < max_scripts; ++i) {
        const auto entry = list->GetDictionary(i);
        if (!entry) continue;
        Script script{entry->GetString("id").ToString(), entry->GetString("name").ToString(),
                      entry->GetString("origin").ToString(), entry->GetString("source").ToString(),
                      entry->GetBool("enabled")};
        if (ValidId(script.id) && !script.name.empty() && script.name.size() <= 1024 &&
            !script.origin.empty() && script.origin == WebOrigin(script.origin) &&
            IsSupportedScript(script.source)) items_.push_back(std::move(script));
    }
}

ScriptImportError ScriptLibrary::PrepareFile(const std::filesystem::path& path, std::string origin, Script& item) const {
    using Error = ScriptImportError;
    const auto name = path.filename().wstring();
    if (!path.is_absolute() || GetDriveTypeW(path.root_path().c_str()) == DRIVE_REMOTE ||
        !IsJavaScriptFilename(name)) return Error::file;
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) return Error::file;
    if (size > max_script) return Error::size;
    if (!size) return Error::encoding;
    auto source = ReadBounded(path, max_script);
    if (source.empty()) return Error::file;
    return PrepareSource(name, std::move(source), std::move(origin), item);
}

ScriptImportError ScriptLibrary::PrepareSource(std::wstring_view name, std::string source,
                                                std::string origin, Script& item) const {
    using Error = ScriptImportError;
    if (items_.size() >= max_scripts) return Error::capacity;
    if (origin.empty() || origin != WebOrigin(origin)) return Error::storage;
    if (name.size() > 255 || name.find_first_of(L"/\\:") != std::wstring_view::npos ||
        !IsJavaScriptFilename(name)) return Error::file;
    const auto validation = ValidateScript(source);
    if (validation != Error::none) return validation;
    GUID guid{};
    wchar_t id[40]{};
    if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, id, static_cast<int>(std::size(id))))
        return Error::storage;
    item = {CefString(id).ToString().substr(1, 36), CefString(std::wstring(name)).ToString(),
            std::move(origin), std::move(source), true};
    return Error::none;
}

bool ScriptLibrary::Save(std::vector<Script> next) {
    auto root = CefValue::Create();
    auto list = CefListValue::Create();
    for (const auto& script : next) {
        auto entry = CefDictionaryValue::Create();
        entry->SetString("id", script.id);
        entry->SetString("name", script.name);
        entry->SetString("origin", script.origin);
        entry->SetString("source", script.source);
        entry->SetBool("enabled", script.enabled);
        list->SetDictionary(list->GetSize(), entry);
    }
    root->SetList(list);
    const auto text = CefWriteJSON(root, JSON_WRITER_DEFAULT).ToString();
    if (text.empty() || text.size() > max_archive ||
        !WriteFileAtomically(profile_ / L"aqua-scripts.json", text)) return false;
    items_ = std::move(next);
    return true;
}
}
