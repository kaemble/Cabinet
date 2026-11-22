#include "settings_store.h"
#include "app_paths.h"
#include "save_storage.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <charconv>

namespace fs = std::filesystem;

void settings_store::read_global(RunnerOptions& result)
{
    const auto& supplied = result.explicit_flags;
    // Interface preferences are global; explicit launch flags take precedence.
    std::ifstream config(application_config(result.data_dir) / "runner.conf");
    std::string key;
    int value;
    while (config >> key >> value)
    {
        if (key == "volume" && !supplied.contains("--volume") && value >= 0 && value <= 100)
        {
            result.volume = value;
        }
        if (value != 0 && value != 1)
        {
            continue;
        }
        if (key == "swap_confirm_cancel" && !supplied.contains("--swap-confirm-cancel"))
        {
            result.swap_confirm_cancel = value;
        }
        if (key == "pause_on_focus_loss")
        {
            result.pause_on_focus_loss = value != 0;
        }
        if (key == "hide_cursor")
        {
            result.hide_cursor = value != 0;
        }
        if (key == "show_stats")
        {
            result.show_stats = value != 0;
        }
        if (key == "confirm_save_states")
        {
            result.confirm_save_states = value != 0;
        }
        if (key == "home_shortcut" && !supplied.contains("--menu-shortcut"))
        {
            result.home_shortcut = value;
        }
        if (key == "stick_shortcut" && !supplied.contains("--menu-shortcut"))
        {
            result.stick_shortcut = value;
        }
    }
}

void settings_store::read_system(const std::string& system, RunnerOptions& result)
{
    const auto& supplied = result.explicit_flags;
    std::string key;
    int value;
    if (system == "wii")
    {
        std::ifstream device(application_config(result.data_dir) / "input" / "wii-device.conf");
        std::string key, value;
        if (device >> key >> std::quoted(value); key == "wii_controller")
        {
            for (int i = 0; i < wii_controller::count; ++i)
            {
                if (value == std::to_string(i))
                {
                    result.wii_controller = i;
                }
            }
        }
    }
    const auto input_settings =
        save_storage::read(application_config(result.data_dir) / "input" / (system + "-settings.conf"));
    if (const auto rumble = input_settings.find("rumble");
        rumble != input_settings.end() && (rumble->second == "0" || rumble->second == "1"))
    {
        result.rumble = rumble->second == "1";
    }
    for (const auto& [key, target] :
         {std::pair{"stick_deadzone", &result.stick_deadzone}, std::pair{"trigger_deadzone", &result.trigger_deadzone}})
    {
        const auto found = input_settings.find(key);
        if (found == input_settings.end())
        {
            continue;
        }
        int percent;
        const auto& text = found->second;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), percent);
        if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && percent >= 0 && percent <= 30)
        {
            *target = percent;
        }
    }
    std::ifstream video(application_config(result.data_dir) / "video" / (system + ".conf"));
    for (std::string line; std::getline(video, line);)
    {
        std::istringstream entry(line);
        if (!(entry >> key))
        {
            continue;
        }
        if (key == "resolution" || key == "core_option")
        {
            std::string option, resolution;
            if (entry >> std::quoted(option) >> std::quoted(resolution))
            {
                result.core_options.try_emplace(option, resolution);
            }
        }
        else if (entry >> value)
        {
            if (key == "aspect_ratio" && value >= 0 && value < aspect_ratio::Count &&
                !supplied.contains("--aspect-ratio") && !supplied.contains("--stretch") &&
                !supplied.contains("--keep-aspect"))
            {
                result.aspect_mode = value;
            }
            if (key == "scaling" && value >= 0 && value <= 3 && !supplied.contains("--scale"))
            {
                result.scaling = value;
            }
            if (value != 0 && value != 1)
            {
                continue;
            }
            if (key == "vsync" && !supplied.contains("--no-vsync"))
            {
                result.vsync = value;
            }
            if (key == "vrr" && !supplied.contains("--vrr"))
            {
                result.vrr = value;
            }
            if (key == "fullscreen" && !supplied.contains("--windowed"))
            {
                result.fullscreen = value;
            }
        }
    }
}

void settings_store::read_game(const fs::path& file, RunnerOptions& options)
{
    std::ifstream input(file);
    for (std::string line; std::getline(input, line);)
    {
        std::istringstream entry(line);
        std::string key;
        int value;
        if (!(entry >> key))
        {
            continue;
        }
        if (key == "resolution" || key == "core_option")
        {
            std::string option, resolution;
            if (entry >> std::quoted(option) >> std::quoted(resolution))
            {
                if (!options.explicit_core_options.contains(option))
                {
                    options.core_options[option] = resolution;
                }
            }
        }
        else if (entry >> value)
        {
            if (key == "volume" && value >= 0 && value <= 100 && !options.explicit_flags.contains("--volume"))
            {
                options.volume = value;
            }
            if (key == "aspect_ratio" && value >= 0 && value < aspect_ratio::Count &&
                !options.explicit_flags.contains("--aspect-ratio") && !options.explicit_flags.contains("--stretch") &&
                !options.explicit_flags.contains("--keep-aspect"))
            {
                options.aspect_mode = value;
            }
            if (key == "wii_controller" && value >= 0 && value < wii_controller::count)
            {
                options.wii_controller = value;
            }
            if (key == "stick_deadzone" && value >= 0 && value <= 30)
            {
                options.stick_deadzone = value;
            }
            if (key == "trigger_deadzone" && value >= 0 && value <= 30)
            {
                options.trigger_deadzone = value;
            }
            if (key == "scaling" && value >= 0 && value <= 3 && !options.explicit_flags.contains("--scale"))
            {
                options.scaling = value;
            }
            if (value != 0 && value != 1)
            {
                continue;
            }
            if (key == "rumble")
            {
                options.rumble = value;
            }
            if (key == "vsync" && !options.explicit_flags.contains("--no-vsync"))
            {
                options.vsync = value;
            }
            if (key == "vrr" && !options.explicit_flags.contains("--vrr"))
            {
                options.vrr = value;
            }
            if (key == "fullscreen" && !options.explicit_flags.contains("--windowed"))
            {
                options.fullscreen = value;
            }
        }
    }
}
static void write_advanced_config(std::ostream& out, const SettingsSnapshot& snapshot)
{
    out << "vsync " << snapshot.values.vsync << "\nvrr " << snapshot.values.vrr << '\n';
    for (const auto& [key, value] : snapshot.core_options)
    {
        out << "core_option " << std::quoted(key) << ' ' << std::quoted(value) << '\n';
    }
}
void settings_store::write_game(const fs::path& directory, const SettingsSnapshot& snapshot)
{
    fs::create_directories(directory);
    const auto temporary = directory / "settings.conf.tmp";
    std::ofstream out(temporary);
    const auto& ui = snapshot.values;
    out << "wii_controller " << ui.wii_controller << '\n';
    out << "rumble " << ui.rumble << '\n';
    out << "stick_deadzone " << ui.stick_deadzone << "\ntrigger_deadzone " << ui.trigger_deadzone << '\n';
    out << "fullscreen " << ui.fullscreen << "\naspect_ratio " << ui.aspect_mode << "\nscaling " << ui.scaling
        << "\nvolume " << ui.volume << '\n';
    write_advanced_config(out, snapshot);
    if (!snapshot.resolution_key.empty())
    {
        out << "resolution " << std::quoted(snapshot.resolution_key) << ' ' << std::quoted(snapshot.resolution_value)
            << '\n';
    }
    out.close();
    if (!out)
    {
        throw std::runtime_error("Cannot save game configuration");
    }
    fs::rename(temporary, directory / "settings.conf");
}

void settings_store::write_global(const fs::path& config, const SettingsSnapshot& snapshot)
{
    fs::create_directories(config);
    {
        const auto temporary = config / "runner.conf.tmp";
        std::ofstream settings(temporary);
        if (snapshot.game_config)
        {
            // Keep the global volume when saving Interface changes from a game profile.
            std::ifstream previous_settings(config / "runner.conf");
            std::string key;
            int value;
            while (previous_settings >> key >> value)
            {
                if (key == "volume" && value >= 0 && value <= 100)
                {
                    settings << "volume " << value << '\n';
                }
            }
        }
        else
        {
            settings << "volume " << snapshot.values.volume << '\n';
        }
        settings << "swap_confirm_cancel " << snapshot.values.swap_confirm_cancel << "\nstick_shortcut "
                 << snapshot.values.stick_shortcut << "\nhome_shortcut " << snapshot.values.home_shortcut
                 << "\npause_on_focus_loss " << snapshot.values.pause_on_focus_loss << "\nhide_cursor "
                 << snapshot.values.hide_cursor << "\nconfirm_save_states " << snapshot.values.confirm_save_states
                 << "\nshow_stats " << snapshot.values.show_stats << '\n';
        settings.close();
        if (!settings)
        {
            throw std::runtime_error("Cannot save runner configuration");
        }
        fs::rename(temporary, config / "runner.conf");
    }
}

void settings_store::write_system(const fs::path& config, const std::string& system, const SettingsSnapshot& snapshot)
{
    save_storage::write(config / "input" / (system + "-settings.conf"),
                        {{"rumble", snapshot.values.rumble ? "1" : "0"},
                         {"stick_deadzone", std::to_string(snapshot.values.stick_deadzone)},
                         {"trigger_deadzone", std::to_string(snapshot.values.trigger_deadzone)}});
    if (system == "wii")
    {
        save_storage::write(config / "input" / "wii-device.conf",
                            {{"wii_controller", std::to_string(snapshot.values.wii_controller)}});
    }
    const auto video_dir = config / "video";
    fs::create_directories(video_dir);
    const auto video_file = video_dir / (system + ".conf");
    const auto video_temporary = video_file.string() + ".tmp";
    std::ofstream video_settings(video_temporary);
    video_settings << "fullscreen " << snapshot.values.fullscreen << "\naspect_ratio " << snapshot.values.aspect_mode
                   << "\nscaling " << snapshot.values.scaling << '\n';
    write_advanced_config(video_settings, snapshot);
    if (!snapshot.resolution_key.empty())
    {
        video_settings << "resolution " << std::quoted(snapshot.resolution_key) << ' '
                       << std::quoted(snapshot.resolution_value) << '\n';
    }
    video_settings.close();
    if (!video_settings)
    {
        throw std::runtime_error("Cannot save system video configuration");
    }
    fs::rename(video_temporary, video_file);
}
