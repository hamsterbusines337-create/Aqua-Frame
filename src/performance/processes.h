#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace aqua {
struct ProcessSample {
    uint32_t id;
    uint64_t created;
    uint64_t cpu;
    uint64_t working_set;
};
struct ProcessSnapshot {
    std::vector<ProcessSample> processes;
    uint64_t system_cpu = 0;
};
ProcessSnapshot SampleBrowserProcesses();
double BrowserCpuPercent(const ProcessSnapshot& before, const ProcessSnapshot& after);
}
