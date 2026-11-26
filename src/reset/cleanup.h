#pragma once

#include <filesystem>
#include <system_error>

namespace aqua {
inline constexpr auto first_run_marker = L".aqua-first-run";
bool ResetProfile(const std::filesystem::path& profile, const std::filesystem::path& downloads,
                  std::error_code& error);
}
