#include "runner_game_files.h"
#include "app_paths.h"
#include "runner_options.h"
#include "save_manager.h"
#include "save_storage.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace fs = std::filesystem;

class Fingerprint
{
public:
    void update(std::string_view bytes)
    {
        for (unsigned char byte : bytes)
        {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
    }

    std::string hex() const
    {
        std::ostringstream result;
        result << std::hex << std::setw(16) << std::setfill('0') << hash;
        return result.str();
    }

private:
    std::uint64_t hash = 14695981039346656037ull;
};

static std::string fingerprint(std::string_view bytes)
{
    Fingerprint result;
    result.update(bytes);
    return result.hex();
}

static std::string file_fingerprint(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Cannot open file: " + path.string());
    }
    if (file.peek() == std::ifstream::traits_type::eof())
    {
        throw std::runtime_error("File is empty or unreadable: " + path.string());
    }
    Fingerprint result;
    std::array<char, 64 * 1024> buffer;
    while (file.read(buffer.data(), buffer.size()) || file.gcount())
    {
        result.update({buffer.data(), static_cast<std::size_t>(file.gcount())});
    }
    if (file.bad() || !file.eof())
    {
        throw std::runtime_error("Cannot read file: " + path.string());
    }
    return result.hex();
}

static std::string fingerprint(const std::vector<char>& bytes)
{
    return fingerprint(std::string_view(bytes.data(), bytes.size()));
}

std::vector<char> read_content(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        throw std::runtime_error("Cannot open file: " + path.string());
    }
    const auto size = file.tellg();
    if (size <= 0)
    {
        throw std::runtime_error("File is empty or unreadable: " + path.string());
    }
    std::vector<char> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
    {
        throw std::runtime_error("Cannot read file: " + path.string());
    }
    return bytes;
}

GameFiles prepare_game_files(SaveManager& saves, const RunnerOptions& options, const std::filesystem::path& data_dir,
                             const std::string& input_system, const std::string& core_path, const std::string& rom_path,
                             const retro_system_info& info, const std::vector<char>& content)
{
    GameFiles files;
    const bool contentless = rom_path.empty();
    std::string game_id;
    if (!options.game_id.empty())
    {
        game_id = options.game_id;
    }
    else if (contentless)
    {
        game_id = "core-" + save_storage::folder_name(fs::path(core_path).stem().string(), 100);
    }
    else if (info.need_fullpath)
    {
        // Standalone fallback; Cabinet supplies an ID independent of the path.
        game_id = "path-" + fingerprint(fs::canonical(rom_path).generic_string());
    }
    else
    {
        game_id = fingerprint(content);
    }
    files.storage_system =
        (options.game_id.empty() || input_system == "generic") ? "Generic" : input_system;
    files.save_name = options.save_name.empty() ? game_id : options.save_name;
    const auto save_root = options.save_dir.empty() ? data_dir / "saves" : fs::absolute(options.save_dir);
    files.save_directory = save_root / files.storage_system / files.save_name;
    saves.directory = files.save_directory.string();
    fs::create_directories(files.save_directory);
    const auto old_metadata = save_storage::read(files.save_directory / "cabinet-game.txt");
    if (old_metadata.contains("game_id") && old_metadata.at("game_id") != game_id)
    {
        throw std::runtime_error("Save folder belongs to another game: " + files.save_directory.string());
    }
    files.core_name = fs::path(core_path).stem().string();
    const auto rom_stem = contentless ? files.core_name : fs::path(rom_path).stem().string();
    files.default_title =
        (contentless && info.library_name && *info.library_name) ? std::string(info.library_name) : rom_stem;
    if (old_metadata.contains("rom_stem"))
    {
        save_storage::relocate_native_saves(files.save_directory, old_metadata.at("rom_stem"), rom_stem, files.core_name);
    }
    files.core_hash = file_fingerprint(core_path);
    saves.metadata = {{"format", "1"},
                      {"game_id", game_id},
                      {"system", input_system},
                      {"title", options.title.empty() ? files.default_title : options.title},
                      {"save_name", files.save_name},
                      {"rom_stem", rom_stem},
                      {"core", files.core_name},
                      {"core_name", info.library_name ? info.library_name : ""},
                      {"core_version", info.library_version ? info.library_version : ""},
                      {"core_hash", files.core_hash},
                      {"core_hash_algorithm", "fnv1a64"}};
    if (!contentless && !info.need_fullpath)
    {
        saves.metadata["rom_hash"] = options.game_id.empty() ? game_id : fingerprint(content);
        saves.metadata["rom_hash_algorithm"] = "fnv1a64";
    }
    saves.playtime.seconds = options.playtime;
    if (old_metadata.contains("playtime"))
    {
        const auto previous = parse_playtime(old_metadata.at("playtime"));
        if (previous)
        {
            saves.playtime.seconds = std::max(saves.playtime.seconds, *previous);
        }
    }
    saves.write_playtime();
    files.game_config_dir = application_config(options.data_dir) / "games" / files.storage_system / game_id;
    return files;
}
