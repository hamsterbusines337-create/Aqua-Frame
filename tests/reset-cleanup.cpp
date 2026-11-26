#include "../src/reset/cleanup.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <windows.h>

namespace fs = std::filesystem;
namespace {
bool Write(const fs::path& path) {
    fs::create_directories(path.parent_path());
    return static_cast<bool>(std::ofstream(path, std::ios::binary) << "fixture");
}

bool Require(bool passed, const char* message) {
    if (!passed) std::cerr << message << '\n';
    return passed;
}
}

int main() {
    wchar_t temp[MAX_PATH]{};
    wchar_t unique[MAX_PATH]{};
    const auto length = GetTempPathW(MAX_PATH, temp);
    if (!length || length >= MAX_PATH || !GetTempFileNameW(temp, L"aqf", 0, unique) ||
        !DeleteFileW(unique)) return 1;
    const fs::path test(unique);
    const auto profile = test / L"Aqua Frame" / L"User Data";
    const auto downloads = test / L"Downloads";
    const auto other = test / L"Other Browser" / L"Cookies";
    std::error_code error;
    const auto preferences = profile / L"aqua-settings.json";
    const auto credentials = profile / L"Default" / L"Login Data";
    const auto isolated = profile / L"Default" / L"Extensions" / L"fixture";
    const auto fixture = Write(preferences) && Write(credentials) && Write(isolated / L"manifest.json") &&
        Write(profile / L"Default" / L"Network" / L"Cookies") &&
        Write(profile / L"Default" / L"History") &&
        Write(profile / L"Default" / L"Service Worker" / L"test") &&
        Write(downloads / L"saved.zip") && Write(other);
    if (!fixture) return 1;

    const auto pending = profile.parent_path() / L"Reset Pending";
    if (!Require(!aqua::ResetProfile(profile, profile / L"Default", error) &&
            fs::exists(credentials) && !fs::exists(pending), "Unsafe downloads root accepted.")) return 1;
    const auto link = profile / L"outside";
    if (CreateSymbolicLinkW(link.c_str(), downloads.c_str(),
            SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
        if (!Require(fs::exists(link / L"saved.zip"), "The fixture reparse link did not work.")) return 1;
    }

    // Keep a real file open with no sharing so an interrupted cleanup has
    // to leave its quarantine for a later retry.
    const auto locked = profile / L"held-open";
    if (!Write(locked)) return 1;
    const auto handle = CreateFileW(locked.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return 1;
    const auto blocked = !aqua::ResetProfile(profile, downloads, error);
    CloseHandle(handle);
    if (!Require(blocked, "The reset succeeded with a locked fixture.")) return 1;

    if (!Require(aqua::ResetProfile(profile, downloads, error), "Profile deletion or retry failed.")) {
        std::cerr << error.value() << '\n';
        return 1;
    }
    const bool good = Require(!fs::exists(pending) && fs::exists(profile / aqua::first_run_marker) &&
        !fs::exists(preferences) && !fs::exists(profile / L"Default") &&
        fs::exists(downloads / L"saved.zip") && fs::exists(other),
        "Clean profile or unrelated file preservation check failed.");
    fs::remove_all(test, error);
    return good ? 0 : 1;
}
