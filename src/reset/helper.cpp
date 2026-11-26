#include "cleanup.h"
#include "restart.h"
#include "../profiles/profile.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <shellapi.h>
#include <shlobj.h>
#include <string>

namespace {
HANDLE ParseHandle(const wchar_t* token) {
    if (*token < L'0' || *token > L'9') return nullptr;
    wchar_t* end = nullptr;
    errno = 0;
    const auto value = std::wcstoull(token, &end, 10);
    return errno || *end != L'\0' ? nullptr : reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value));
}

std::wstring HandlePath(HANDLE handle) {
    std::wstring path(32768, L'\0');
    const auto size = GetFinalPathNameByHandleW(handle, path.data(), static_cast<DWORD>(path.size()),
                                                FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!size || size >= path.size()) return {};
    path.resize(size);
    if (path.starts_with(L"\\\\?\\")) path.erase(0, 4);
    return path;
}

bool SamePath(const std::wstring& a, const std::wstring& b) {
    return !a.empty() && !b.empty() &&
        CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}

int Failed(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), L"Сброс Aqua Frame", MB_OK | MB_ICONERROR);
    return 1;
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int count = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) return 1;
    const auto parent = count == 3 ? ParseHandle(args[1]) : nullptr;
    const auto gate = count == 3 ? ParseHandle(args[2]) : nullptr;
    LocalFree(args);
    if (!parent || !gate) return 1;

    // Do not pass the exclusive profile handle to any further child process.
    if (!SetHandleInformation(gate, HANDLE_FLAG_INHERIT, 0)) return 1;
    const auto helper = aqua::ExecutablePath();
    if (helper.empty()) return 1;
    const auto browser = helper.parent_path() / L"AquaFrame.exe";
    std::wstring actual(32768, L'\0');
    DWORD length = static_cast<DWORD>(actual.size());
    if (!QueryFullProcessImageNameW(parent, 0, actual.data(), &length)) return 1;
    actual.resize(length);
    if (!SamePath(actual, browser.wstring())) return 1;

    const auto profile = aqua::ProfileRoot();
    if (!aqua::EnsureProfileContainer(profile) ||
        !SamePath(HandlePath(gate), (profile.parent_path() / L"Profile.lock").wstring())) {
        return Failed(L"Папку профиля не удалось проверить. Сброс остановлен.");
    }
    if (WaitForSingleObject(parent, 60000) != WAIT_OBJECT_0)
        return Failed(L"Aqua Frame не завершился. Очистка остановлена.");
    CloseHandle(parent);

    PWSTR download_path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_DONT_VERIFY, nullptr, &download_path))) {
        return Failed(L"Не удалось проверить расположение папки загрузок. Очистка остановлена.");
    }
    const std::filesystem::path downloads(download_path);
    CoTaskMemFree(download_path);

    std::error_code error;
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (aqua::ResetProfile(profile, downloads, error)) break;
        if (error.value() != ERROR_SHARING_VIOLATION && error.value() != ERROR_LOCK_VIOLATION) break;
        Sleep(500);
    }
    if (error) {
        return Failed(L"Не удалось полностью очистить профиль Aqua Frame (код " +
            std::to_wstring(error.value()) + L"). Закройте другие окна Aqua Frame и повторите запуск.");
    }

    CloseHandle(gate);
    auto command = L"\"" + browser.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(browser.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, browser.parent_path().c_str(), &startup, &process)) {
        return Failed(L"Профиль очищен. Aqua Frame не удалось запустить автоматически.");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
