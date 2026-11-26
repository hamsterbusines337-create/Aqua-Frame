#include "profile.h"

#include <shlobj.h>

namespace aqua {
std::filesystem::path ProfileRoot() {
    PWSTR data = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &data)))
        return {};
    auto profile = std::filesystem::path(data) / L"Aqua Frame" / L"User Data";
    CoTaskMemFree(data);
    return profile;
}

bool IsPlainDirectory(const std::filesystem::path& path) {
    const auto flags = GetFileAttributesW(path.c_str());
    return flags != INVALID_FILE_ATTRIBUTES && (flags & FILE_ATTRIBUTE_DIRECTORY) &&
           !(flags & FILE_ATTRIBUTE_REPARSE_POINT);
}

bool EnsureProfileContainer(const std::filesystem::path& profile) {
    if (profile.empty()) return false;
    const auto container = profile.parent_path();
    const auto local = container.parent_path();
    if (!IsPlainDirectory(local)) return false;
    if (!CreateDirectoryW(container.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return false;
    return IsPlainDirectory(container);
}

HANDLE LockProfile(const std::filesystem::path& profile) {
    const auto path = profile.parent_path() / L"Profile.lock";
    HANDLE gate = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (gate == INVALID_HANDLE_VALUE) return gate;
    BY_HANDLE_FILE_INFORMATION file{};
    if (!GetFileInformationByHandle(gate, &file) ||
        (file.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        CloseHandle(gate);
        return INVALID_HANDLE_VALUE;
    }
    return gate;
}
}
