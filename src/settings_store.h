#pragma once
#include "runner_options.h"
#include <filesystem>
#include <utility>
#include <vector>

// Values copied from the live UI. No references to the session or its resources.
// Core options retain their declaration order, including renderer-specific
// resolution preferences, so serialization keeps the existing precedence.
struct SettingsSnapshot
{
    // Scalar preferences are serialized from values; all advertised core
    // options are persisted through the catalog snapshot below.
    RunnerOptions values;
    bool game_config = false;
    std::vector<std::pair<std::string, std::string>> core_options;
    std::string resolution_key, resolution_value;
};

namespace settings_store
{
void read_global(RunnerOptions& options);
void read_system(const std::string& system, RunnerOptions& options);
void read_game(const std::filesystem::path& file, RunnerOptions& options);
void write_global(const std::filesystem::path& config, const SettingsSnapshot& snapshot);
void write_system(const std::filesystem::path& config, const std::string& system, const SettingsSnapshot& snapshot);
void write_game(const std::filesystem::path& directory, const SettingsSnapshot& snapshot);
}
