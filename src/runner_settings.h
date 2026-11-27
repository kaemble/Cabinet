#pragma once

#include "settings_store.h"

struct Session;

void apply_session_settings(Session& session, RunnerOptions& options, const std::filesystem::path& game_config_dir,
                            const std::string& input_system, const char* library_name);
SettingsSnapshot settings_snapshot(const Session& session);
