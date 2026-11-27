#pragma once

#include <libretro.h>
#include <filesystem>
#include <string>
#include <vector>

struct RunnerOptions;
class SaveManager;

struct GameFiles
{
    std::filesystem::path game_config_dir, save_directory;
    std::string storage_system, save_name, core_name, core_hash, default_title;
};

std::vector<char> read_content(const std::filesystem::path& path);
GameFiles prepare_game_files(SaveManager& saves, const RunnerOptions& options, const std::filesystem::path& data_dir,
                             const std::string& input_system, const std::string& core_path, const std::string& rom_path,
                             const retro_system_info& info, const std::vector<char>& content);
