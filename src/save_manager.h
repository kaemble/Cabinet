#pragma once
#include <cstddef>
#include <libretro.h>
#include "save_storage.h"
#include "playtime.h"
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <optional>

// Non-owning entry points, used synchronously while the core is loaded.
struct SaveCore
{
    decltype(&retro_get_memory_data) get_memory_data;
    decltype(&retro_get_memory_size) get_memory_size;
    decltype(&retro_serialize_size) serialize_size;
    decltype(&retro_serialize) serialize;
    decltype(&retro_unserialize) unserialize;
};

// The runner must pause if restoring the pre-load snapshot also fails.
class StateRecoveryError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

class SaveManager
{
public:
    static constexpr std::size_t max_state_size = 512 * 1024 * 1024;
    std::string directory;
    std::filesystem::path state_path, battery_path;
    save_storage::Metadata metadata;
    Playtime playtime;
    bool battery_ready = false;

    void write_playtime();
    void battery(const SaveCore& core, bool load);
    std::vector<char> capture_state(const SaveCore& core) const;
    void save_state(const SaveCore& core);
    void load_state(const SaveCore& core, const std::filesystem::path& source = {}) const;

private:
    std::map<std::filesystem::path, std::vector<char>> saved_memory;
    std::optional<save_storage::Metadata> written_metadata;
};
