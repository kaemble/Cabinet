#include "save_manager.h"
#include <cstddef>
#include <array>
#include <cstring>
#include <ctime>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;

void SaveManager::write_playtime()
{
    metadata["playtime"] = std::to_string(playtime.seconds);
    if (written_metadata && *written_metadata == metadata)
    {
        return;
    }
    const auto path = fs::path(directory) / "cabinet-game.txt";
    if (!written_metadata && fs::is_regular_file(path) && save_storage::read(path) == metadata)
    {
        written_metadata = metadata;
        return;
    }
    save_storage::write(path, metadata, true);
    written_metadata = metadata;
}

void SaveManager::battery(const SaveCore& core, bool load)
{
    if (battery_path.empty())
    {
        return;
    }
    for (auto region : {RETRO_MEMORY_SAVE_RAM, RETRO_MEMORY_RTC})
    {
        const std::size_t size = core.get_memory_size(region);
        auto* memory = static_cast<char*>(core.get_memory_data(region));
        if (!size || !memory)
        {
            continue;
        }
        fs::path path = battery_path;
        path += (region == RETRO_MEMORY_RTC) ? ".rtc" : ".srm";
        if (load)
        {
            if (!fs::exists(path))
            {
                continue;
            }
            const auto saved_size = fs::file_size(path);
            // Auto-detecting cores can expose a maximum SRAM buffer before
            // execution, then a smaller cartridge-specific region at exit.
            // Preserve the core's erased-memory value beyond the saved data.
            if (saved_size > size || !saved_size || (region == RETRO_MEMORY_RTC && saved_size != size))
            {
                throw std::runtime_error("Battery save size differs from core memory: " + path.string());
            }
            std::vector<char> bytes(static_cast<std::size_t>(saved_size));
            std::ifstream input(path, std::ios::binary);
            input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (!input)
            {
                throw std::runtime_error("Cannot read battery save: " + path.string());
            }
            std::memcpy(memory, bytes.data(), bytes.size());
            saved_memory[path] = std::move(bytes);
        }
        else
        {
            const auto previous = saved_memory.find(path);
            if (previous != saved_memory.end() && previous->second.size() == size &&
                std::equal(previous->second.begin(), previous->second.end(), memory))
            {
                continue;
            }
            std::vector<char> bytes(memory, memory + size);
            fs::create_directories(path.parent_path());
            auto temp = path;
            temp += ".tmp";
            std::ofstream output(temp, std::ios::binary | std::ios::trunc);
            output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            output.close();
            if (!output)
            {
                throw std::runtime_error("Cannot write battery save: " + path.string());
            }
            save_storage::commit_with_backup(temp, path);
            saved_memory[path] = std::move(bytes);
        }
    }
    if (load)
    {
        battery_ready = true;
    }
}

std::vector<char> SaveManager::capture_state(const SaveCore& core) const
{
    const std::size_t size = core.serialize_size();
    // Zero can mean the core has not finished starting, rather than that it
    // never supports states. Leave both actions available for a later retry.
    if (!size)
    {
        throw std::runtime_error("Core cannot save state right now; resume the game and try again");
    }
    if (size > max_state_size)
    {
        throw std::runtime_error("Save state exceeds the 512 MiB limit");
    }
    std::vector<char> data(size);
    if (!core.serialize(data.data(), data.size()))
    {
        throw std::runtime_error("Core could not save state");
    }
    return data;
}
void SaveManager::save_state(const SaveCore& core)
{
    auto data = capture_state(core);
    fs::create_directories(state_path.parent_path());
    fs::path temporary = state_path;
    temporary += ".tmp";
    try
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write("CABST001", 8);
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        file.close();
        if (!file)
        {
            throw std::runtime_error("Could not write save state");
        }
        if (fs::is_regular_file(state_path))
        {
            save_storage::backup(state_path.string() + ".txt");
        }
        save_storage::commit_with_backup(temporary, state_path);
    }
    catch (...)
    {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
    if (!metadata.empty())
    {
        auto state_metadata = metadata;
        state_metadata["kind"] = "save-state";
        state_metadata["timestamp"] = std::to_string(std::time(nullptr));
        save_storage::write(state_path.string() + ".txt", state_metadata);
    }
}
void SaveManager::load_state(const SaveCore& core, const fs::path& source) const
{
    const auto path = source.empty() ? state_path : source;
    auto sidecar = fs::path(path.string() + ".txt");
    if (path.extension() == ".bak" && !fs::exists(sidecar))
    {
        auto original = path;
        original.replace_extension();
        sidecar = original.string() + ".txt.bak";
    }
    const auto state_metadata = save_storage::read(sidecar);
    if (state_metadata.contains("core_hash") && metadata.contains("core_hash") &&
        state_metadata.at("core_hash") != metadata.at("core_hash"))
    {
        throw std::runtime_error("Save state belongs to a different core binary");
    }
    std::ifstream file(source.empty() ? state_path : source, std::ios::binary | std::ios::ate);
    if (!file)
    {
        throw std::runtime_error("Save state not found");
    }
    const auto size = file.tellg();
    if (size <= 8 || size > static_cast<std::streamoff>(max_state_size + 8))
    {
        throw std::runtime_error("Invalid save state");
    }
    file.seekg(0);
    std::array<char, 8> magic{};
    file.read(magic.data(), 8);
    if (std::string(magic.data(), 8) != "CABST001")
    {
        throw std::runtime_error("Invalid save-state header");
    }
    std::vector<char> data(static_cast<std::size_t>(size) - 8);
    file.read(data.data(), static_cast<std::streamsize>(data.size()));
    if (!file)
    {
        throw std::runtime_error("Could not read save state");
    }
    // Some cores mutate state before rejecting input; retain a rollback snapshot.
    auto previous = capture_state(core);
    if (!core.unserialize(data.data(), data.size()))
    {
        if (!core.unserialize(previous.data(), previous.size()))
        {
            throw StateRecoveryError("Load failed; reset the game before resuming");
        }
        throw std::runtime_error("Core rejected save state; progress restored");
    }
}
