#include "processes.h"

#include <algorithm>
#include <unordered_set>
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

namespace aqua {
namespace {
uint64_t Ticks(const FILETIME& time) {
    return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}
}

ProcessSnapshot SampleBrowserProcesses() {
    ProcessSnapshot sample;
    FILETIME idle{}, kernel{}, user{};
    if (GetSystemTimes(&idle, &kernel, &user)) sample.system_cpu = Ticks(kernel) + Ticks(user);
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return sample;
    std::vector<std::pair<DWORD, DWORD>> parents;
    PROCESSENTRY32W entry{sizeof(PROCESSENTRY32W)};
    if (Process32FirstW(snapshot, &entry)) {
        do {
            parents.emplace_back(entry.th32ProcessID, entry.th32ParentProcessID);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    std::unordered_set<DWORD> children{GetCurrentProcessId()};
    bool found_child;
    do {
        found_child = false;
        for (const auto& [id, parent] : parents) {
            if (children.contains(parent) && children.insert(id).second) found_child = true;
        }
    } while (found_child);

    for (auto id : children) {
        const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, id);
        if (!process) continue;
        FILETIME created{}, exited{};
        PROCESS_MEMORY_COUNTERS working_set{};
        if (GetProcessTimes(process, &created, &exited, &kernel, &user)) {
            GetProcessMemoryInfo(process, &working_set, sizeof(working_set));
            sample.processes.push_back({id, Ticks(created), Ticks(kernel) + Ticks(user), working_set.WorkingSetSize});
        }
        CloseHandle(process);
    }
    return sample;
}

double BrowserCpuPercent(const ProcessSnapshot& before, const ProcessSnapshot& after) {
    if (!before.system_cpu || after.system_cpu <= before.system_cpu) return -1;
    uint64_t browser_ticks = 0;
    for (const auto& current : after.processes) {
        const auto previous = std::ranges::find(before.processes, current.id, &ProcessSample::id);
        if (previous != before.processes.end() && previous->created == current.created && current.cpu >= previous->cpu)
            browser_ticks += current.cpu - previous->cpu;
    }
    return 100.0 * static_cast<double>(browser_ticks) / static_cast<double>(after.system_cpu - before.system_cpu);
}
}
