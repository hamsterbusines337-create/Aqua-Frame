#include "restart.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace aqua {
std::filesystem::path ExecutablePath() {
    std::wstring result(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, result.data(), static_cast<DWORD>(result.size()));
    if (!length || length >= result.size()) return {};
    result.resize(length);
    return result;
}

bool StartResetHelper(HANDLE profile_gate) {
    const auto binary = ExecutablePath();
    if (binary.empty()) return false;
    const auto helper = binary.parent_path() / L"AquaFrameReset.exe";
    std::array<HANDLE, 2> handles{};
    const HANDLE self = GetCurrentProcess();
    if (!DuplicateHandle(self, self, self, &handles[0],
                         SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, 0)) return false;
    if (!DuplicateHandle(self, profile_gate, self, &handles[1], 0, TRUE, DUPLICATE_SAME_ACCESS)) {
        CloseHandle(handles[0]);
        return false;
    }

    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<unsigned char> storage(size);
    auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    bool started = false;
    if (size && InitializeProcThreadAttributeList(attributes, 1, 0, &size)) {
        if (UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                      handles.data(), sizeof(handles), nullptr, nullptr)) {
            STARTUPINFOEXW startup{};
            startup.StartupInfo.cb = sizeof(startup);
            startup.lpAttributeList = attributes;
            PROCESS_INFORMATION process{};
            auto command = L"\"" + helper.wstring() + L"\" " +
                std::to_wstring(reinterpret_cast<uintptr_t>(handles[0])) + L" " +
                std::to_wstring(reinterpret_cast<uintptr_t>(handles[1]));
            if (CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, TRUE,
                               EXTENDED_STARTUPINFO_PRESENT, nullptr, helper.parent_path().c_str(),
                               &startup.StartupInfo, &process)) {
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
                started = true;
            }
        }
        DeleteProcThreadAttributeList(attributes);
    }
    for (auto handle : handles) CloseHandle(handle);
    return started;
}
}
