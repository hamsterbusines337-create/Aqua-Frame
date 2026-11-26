#pragma once

#include <filesystem>
#include <windows.h>

namespace aqua {
std::filesystem::path ProfileRoot();
bool EnsureProfileContainer(const std::filesystem::path& profile);
bool IsPlainDirectory(const std::filesystem::path& path);
HANDLE LockProfile(const std::filesystem::path& profile);
}
