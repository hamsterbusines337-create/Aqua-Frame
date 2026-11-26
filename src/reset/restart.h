#pragma once

#include <filesystem>
#include <windows.h>

namespace aqua {
std::filesystem::path ExecutablePath();
bool StartResetHelper(HANDLE profile_gate);
}
