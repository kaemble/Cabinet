#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
namespace rom_archive
{
bool is_zip(const std::filesystem::path& path);
std::vector<std::string> members(const std::filesystem::path& path);
// Returns a private directory containing only the selected ROM.
std::filesystem::path extract(const std::filesystem::path& path, const std::string& member);
// The caller verifies that the selected member's extension matches the system.
void validate_core(const std::filesystem::path& core, const std::filesystem::path& cache, std::string_view system_id);
int probe_core(const char* path);
}
