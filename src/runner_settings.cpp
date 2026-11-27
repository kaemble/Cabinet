#include "runner_settings.h"
#include "runner_session.h"
#include "app_paths.h"
#include <iostream>
#include <string_view>

namespace fs = std::filesystem;

void apply_session_settings(Session& session, RunnerOptions& options, const std::filesystem::path& game_config_dir,
                            const std::string& input_system, const char* library_name)
{
    session.frontend.game_config = fs::is_regular_file(game_config_dir / "settings.conf");
    if (session.frontend.game_config)
    {
        settings_store::read_game(game_config_dir / "settings.conf", options);
    }
    session.core_options.values = options.core_options;
    if (session.dual_screen)
    {
        session.core_options.runtime_overrides =
            input_system == "nds" ? std::map<std::string, std::string>{{"melonds_screen_layout1", "top-bottom"},
                                                                       {"melonds_number_of_screen_layouts", "1"},
                                                                       {"melonds_screen_gap", "0"},
                                                                       {"melonds_secondary_screen_scale", "100"},
                                                                       {"melonds_touch_mode", "touch"}}
                                  : std::map<std::string, std::string>{{"citra_layout_option", "default"},
                                                                       {"citra_swap_screen", "Top"},
                                                                       {"citra_render_3d", "off"}};
    }
    session.frontend.fullscreen = options.fullscreen;
    session.frontend.scaling = options.scaling;
    session.frontend.shaders = options.shaders;
    session.frontend.wii_controller = options.wii_controller;
    session.frontend.aspect_mode = options.aspect_mode;
    session.frontend.volume = options.volume;
    session.frontend.rumble_enabled = options.rumble;
    session.frontend.stick_deadzone = options.stick_deadzone;
    session.frontend.trigger_deadzone = options.trigger_deadzone;
    session.frontend.vsync = options.vsync;
    session.frontend.vrr = options.vrr;
    session.frontend.runahead_frames = options.runahead_frames;
    session.frontend.swap_confirm_cancel = options.swap_confirm_cancel;
    try
    {
        const auto input_dir =
            session.frontend.game_config ? game_config_dir / "input" : application_config(options.data_dir) / "input";
        session.frontend.bindings.configure(input_system, input_dir, options.wii_controller);
        session.frontend.hotkeys.configure_hotkeys(input_system, input_dir);
    }
    catch (const std::exception& error)
    {
        // configure leaves the usable defaults in place if a saved file is malformed.
        std::cerr << "Cabinet: " << error.what() << '\n';
        session.frontend.notify("Could not read saved controls; using defaults");
    }
    session.frontend.stick_shortcut = options.stick_shortcut;
    session.frontend.home_shortcut = options.home_shortcut;
    session.frontend.pause_on_focus_loss = options.pause_on_focus_loss;
    session.frontend.hide_cursor = options.hide_cursor;
    session.frontend.confirm_save_states = options.confirm_save_states;
    session.frontend.show_stats = options.show_stats;
    fs::create_directories(session.system_dir);
    fs::create_directories(session.saves.directory);
    if (input_system == "wii")
    {
        session.core_options.values["dolphin_alt_gc_ports_on_wii"] = "disabled";
        // dolphin_ir_mode values are numeric:
        //   "0" = Right Stick (relative)
        //   "1" = Right Stick (absolute)
        //   "2" = Mouse controls pointer, required for RETRO_DEVICE_POINTER IR
        if (!options.explicit_core_options.contains("dolphin_ir_mode"))
        {
            session.core_options.values["dolphin_ir_mode"] = "2";
            session.core_options.values["dolphin_ir_yaw"] = "19";
            session.core_options.values["dolphin_ir_pitch"] = "19";
            session.core_options.values["dolphin_ir_offset"] = "15";
        }
    }
    if (input_system == "md" && library_name && std::string_view(library_name) == "BlastEm")
    {
        session.core_options.values.try_emplace("blastem_io_1", "gamepad6.1");
    }
    session.prefer_vulkan = options.prefers_vulkan(library_name ? library_name : "");
}

SettingsSnapshot settings_snapshot(const Session& s)
{
    SettingsSnapshot snapshot;
    const auto& ui = s.frontend;
    auto& values = snapshot.values;
    values.volume = ui.volume;
    values.wii_controller = ui.wii_controller;
    values.fullscreen = ui.fullscreen;
    values.aspect_mode = ui.aspect_mode;
    values.scaling = ui.scaling;
    values.shaders = ui.shaders;
    values.vsync = ui.vsync;
    values.vrr = ui.vrr;
    values.runahead_frames = ui.runahead_option_key.empty() ? ui.runahead_frames : 0;
    values.swap_confirm_cancel = ui.swap_confirm_cancel;
    values.stick_shortcut = ui.stick_shortcut;
    values.home_shortcut = ui.home_shortcut;
    values.pause_on_focus_loss = ui.pause_on_focus_loss;
    values.hide_cursor = ui.hide_cursor;
    values.confirm_save_states = ui.confirm_save_states;
    values.show_stats = ui.show_stats;
    values.rumble = ui.rumble_enabled;
    values.stick_deadzone = ui.stick_deadzone;
    values.trigger_deadzone = ui.trigger_deadzone;
    snapshot.game_config = ui.game_config;
    snapshot.core_options = s.core_options.persistent_values(ui);
    snapshot.resolution_key = s.core_options.resolution_key;
    if (!s.core_options.resolution_key.empty())
    {
        snapshot.resolution_value = ui.resolution_values[ui.resolution_index];
    }
    return snapshot;
}
