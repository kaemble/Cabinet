#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
struct BiosInfo
{
    std::string system, digest, filename, region;
};
const std::vector<BiosInfo>& supported_bios();
bool requires_bios(const std::string& system);
std::optional<BiosInfo> identify_bios(const std::string& system, const std::filesystem::path& file);
bool has_bios(const std::string& system, const std::filesystem::path& directory);
std::string install_bios(const std::string& system, const std::filesystem::path& file,
                         const std::filesystem::path& directory);
