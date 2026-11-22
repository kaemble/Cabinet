#pragma once
#include "wii_controller.h"
#include "aspect_ratio.h"
#include <map>
#include <cstdint>
#include <set>
#include <ostream>
#include <string>
#include <string_view>

enum class HardwareApi
{
    Automatic,
    OpenGL,
    Vulkan
};

struct RunnerOptions
{
    std::string rom, core, data_dir, system_dir, save_dir, state_dir, state_file, load_state;
    std::string title, screenshot, system, game_id, save_name;
    std::map<std::string, std::string> core_options;
    std::set<std::string> explicit_flags, explicit_core_options;
    unsigned frames = 0;
    std::int64_t playtime = 0;
    int width = 640, height = 576, volume = 80;
    int core_log_level = 2; // libretro: debug=0, info=1, warn=2, error=3; off=4
    bool show_menu_hint = false;
    bool timing_stats = false, vsync = true, vrr = false;
    bool fast_forward = false;
    bool dual_screen = false; // Present the DS/3DS bottom screen in a separate window.
    int wii_controller = 0;
    int aspect_mode = aspect_ratio::Auto;
    int scaling = 2; // nearest, bilinear, Sharp Bilinear, integer
    HardwareApi hardware_api = HardwareApi::Automatic;
    bool prefers_vulkan(std::string_view core_name) const;
    bool help = false, paused = false, fullscreen = true;
    bool pause_on_focus_loss = true, hide_cursor = true;
    bool confirm_save_states = true;
    bool show_stats = false;
    bool rumble = true;
    int stick_deadzone = 5, trigger_deadzone = 5;
    bool no_audio = false, swap_confirm_cancel = false, stick_shortcut = false, home_shortcut = false;
};
RunnerOptions parse_runner_options(int argc, char** argv);
void print_runner_help(std::ostream& out);
int run_game(const RunnerOptions& options);
