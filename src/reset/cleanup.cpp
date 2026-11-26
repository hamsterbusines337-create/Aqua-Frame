#include "cleanup.h"
#include "../profiles/profile.h"
#include <string_view>

namespace aqua {
namespace {
void CaptureError(std::error_code& error) {
    error = {static_cast<int>(GetLastError()), std::system_category()};
}

bool IsSameOrUnder(const std::filesystem::path& candidate, const std::filesystem::path& parent) {
    const auto path = candidate.lexically_normal().wstring();
    const auto base = parent.lexically_normal().wstring();
    if (path.size() < base.size()) return false;
    return CompareStringOrdinal(path.data(), static_cast<int>(base.size()),
        base.data(), static_cast<int>(base.size()), TRUE) == CSTR_EQUAL &&
        (path.size() == base.size() || path[base.size()] == L'\\' || path[base.size()] == L'/');
}

bool RemoveTree(const std::filesystem::path& path, std::error_code& error) {
    const auto attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        const auto failure = GetLastError();
        if (failure == ERROR_FILE_NOT_FOUND || failure == ERROR_PATH_NOT_FOUND) return true;
        CaptureError(error);
        return false;
    }

    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        if (attrs & FILE_ATTRIBUTE_READONLY) {
            const HANDLE file = CreateFileW(path.c_str(), DELETE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                CaptureError(error);
                return false;
            }
            FILE_DISPOSITION_INFO_EX disposition{};
            disposition.Flags = FILE_DISPOSITION_FLAG_DELETE |
                FILE_DISPOSITION_FLAG_POSIX_SEMANTICS | FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE;
            const bool deleted = SetFileInformationByHandle(file, FileDispositionInfoEx,
                &disposition, sizeof(disposition)) != FALSE;
            if (!deleted) CaptureError(error);
            CloseHandle(file);
            return deleted;
        }
        if (DeleteFileW(path.c_str())) return true;
        CaptureError(error);
        return false;
    }
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
        if (RemoveDirectoryW(path.c_str())) return true;
        CaptureError(error);
        return false;
    }

    // Pin the directory while using its child paths. Opening reparse points
    // themselves keeps a substituted junction from redirecting traversal.
    const HANDLE directory = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (directory == INVALID_HANDLE_VALUE) {
        CaptureError(error);
        return false;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(directory, &info)) {
        CaptureError(error);
        CloseHandle(directory);
        return false;
    }
    if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        CloseHandle(directory);
        error = std::make_error_code(std::errc::operation_not_permitted);
        return false;
    }

    WIN32_FIND_DATAW entry{};
    const HANDLE entries = FindFirstFileW((path / L"*").c_str(), &entry);
    if (entries == INVALID_HANDLE_VALUE) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND) CaptureError(error);
    } else {
        do {
            const std::wstring_view name(entry.cFileName);
            if (name != L"." && name != L".." && !RemoveTree(path / name, error)) break;
        } while (FindNextFileW(entries, &entry));
        if (!error && GetLastError() != ERROR_NO_MORE_FILES) CaptureError(error);
        FindClose(entries);
    }
    CloseHandle(directory);
    if (error) return false;
    if (RemoveDirectoryW(path.c_str())) return true;
    CaptureError(error);
    return false;
}

bool IsInsideDownloads(const std::filesystem::path& downloads,
                       const std::filesystem::path& profile,
                       const std::filesystem::path& pending) {
    if (downloads.empty()) return true;
    if (IsSameOrUnder(downloads, profile) || IsSameOrUnder(downloads, pending)) return true;
    std::error_code error;
    if (!std::filesystem::exists(downloads, error)) return static_cast<bool>(error);
    const auto target = std::filesystem::canonical(downloads, error);
    if (error) return true;
    return IsSameOrUnder(target, profile) || IsSameOrUnder(target, pending);
}
}

bool ResetProfile(const std::filesystem::path& profile, const std::filesystem::path& downloads,
                  std::error_code& error) {
    error.clear();
    if (profile.filename() != L"User Data" || profile.parent_path().filename() != L"Aqua Frame" ||
        !profile.is_absolute() || !IsPlainDirectory(profile.parent_path()) ||
        !IsPlainDirectory(profile.parent_path().parent_path())) {
        error = std::make_error_code(std::errc::operation_not_permitted);
        return false;
    }
    const auto pending = profile.parent_path() / L"Reset Pending";
    if (IsInsideDownloads(downloads, profile, pending)) {
        error = std::make_error_code(std::errc::operation_not_permitted);
        return false;
    }

    const auto flags = GetFileAttributesW(profile.c_str());
    if (flags != INVALID_FILE_ATTRIBUTES) {
        if (!IsPlainDirectory(profile)) {
            error = std::make_error_code(std::errc::operation_not_permitted);
            return false;
        }
        if (!MoveFileExW(profile.c_str(), pending.c_str(), MOVEFILE_WRITE_THROUGH)) {
            CaptureError(error);
            return false;
        }
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) {
        CaptureError(error);
        return false;
    }

    if (GetFileAttributesW(pending.c_str()) != INVALID_FILE_ATTRIBUTES && !IsPlainDirectory(pending)) {
        error = std::make_error_code(std::errc::operation_not_permitted);
        return false;
    }
    if (!RemoveTree(pending, error)) return false;
    if (!CreateDirectoryW(profile.c_str(), nullptr)) {
        CaptureError(error);
        return false;
    }

    const auto marker = profile / first_run_marker;
    const HANDLE first_run = CreateFileW(marker.c_str(), GENERIC_WRITE, 0, nullptr,
                                         CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (first_run == INVALID_HANDLE_VALUE) {
        CaptureError(error);
        return false;
    }
    CloseHandle(first_run);
    return true;
}
}
