#include "runner_options.h"
#include "playtime.h"
#include "settings_store.h"
#include "save_storage.h"
#include "input_bindings.h"
#include <set>
#include <charconv>
#include <stdexcept>
#include <string_view>

static int number(const std::string& value, int minimum, int maximum, const std::string& option)
{
    int result = 0;
    auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result < minimum || result > maximum)
    {
        throw std::runtime_error(option + " requires an integer from " + std::to_string(minimum) + " to " +
                                 std::to_string(maximum));
    }
    return result;
}

bool RunnerOptions::prefers_vulkan(std::string_view core_name) const
{
    if (hardware_api != HardwareApi::Automatic)
    {
        return hardware_api == HardwareApi::Vulkan;
    }
    // Use the core's reported identity so custom filenames and Wii launches
    // select the same default as Home's bundled cores.
    return core_name == "dolphin-emu" || core_name == "Dolphin" || core_name == "dolphin" || core_name == "Flycast" ||
           core_name == "PCEE2" || core_name == "ARMSX2";
}

RunnerOptions parse_runner_options(int argc, char** argv)
{
    RunnerOptions result;
    std::set<std::string> supplied;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h")
        {
            result.help = true;
            return result;
        }
        supplied.insert(arg);
        if (arg.empty() || arg[0] != '-')
        {
            throw std::runtime_error("Supply game content with --rom PATH: " + arg);
        }
        if (arg == "--show-menu-hint")
        {
            result.show_menu_hint = true;
            continue;
        }
        if (arg == "--paused")
        {
            result.paused = true;
            continue;
        }
        if (arg == "--stretch")
        {
            result.aspect_mode = aspect_ratio::Stretch;
            continue;
        }
        if (arg == "--keep-aspect")
        {
            result.aspect_mode = aspect_ratio::Auto;
            continue;
        }
        if (arg == "--windowed")
        {
            result.fullscreen = false;
            continue;
        }
        if (arg == "--timing-stats")
        {
            result.timing_stats = true;
            continue;
        }
        if (arg == "--fast-forward")
        {
            result.fast_forward = true;
            continue;
        }
        if (arg == "--dual-screen")
        {
            result.dual_screen = true;
            continue;
        }
        if (arg == "--vulkan")
        {
            result.hardware_api = HardwareApi::Vulkan;
            continue;
        }
        if (arg == "--opengl")
        {
            result.hardware_api = HardwareApi::OpenGL;
            continue;
        }
        if (arg == "--no-vsync")
        {
            result.vsync = false;
            continue;
        }
        if (arg == "--vrr")
        {
            result.vrr = true;
            continue;
        }
        if (arg == "--no-audio")
        {
            result.no_audio = true;
            continue;
        }
        if (arg == "--swap-confirm-cancel")
        {
            result.swap_confirm_cancel = true;
            continue;
        }
        const bool takes_value = arg == "--rom" || arg == "--core" || arg == "--data-dir" || arg == "--system-dir" ||
                                 arg == "--playtime" || arg == "--save-dir" || arg == "--state-dir" ||
                                 arg == "--state-file" || arg == "--load-state" || arg == "--aspect-ratio" ||
                                 arg == "--save-name" || arg == "--game-id" || arg == "--system" || arg == "--title" ||
                                 arg == "--screenshot" || arg == "--frames" || arg == "--volume" ||
                                 arg == "--window-size" || arg == "--scale" || arg == "--menu-shortcut" ||
                                 arg == "--core-option" || arg == "--core-log-level";
        if (!takes_value)
        {
            throw std::runtime_error("Unknown option: " + arg);
        }
        if (i + 1 >= argc)
        {
            throw std::runtime_error("Missing value for " + arg);
        }
        std::string value = argv[++i];
        if (value.empty())
        {
            throw std::runtime_error("Empty value for " + arg);
        }
        if (arg == "--rom")
        {
            if (!result.rom.empty())
            {
                throw std::runtime_error("Specify exactly one ROM file");
            }
            result.rom = value;
        }
        else if (arg == "--core")
        {
            result.core = value;
        }
        else if (arg == "--core-log-level")
        {
            if (value == "debug")
            {
                result.core_log_level = 0;
            }
            else if (value == "info")
            {
                result.core_log_level = 1;
            }
            else if (value == "warn")
            {
                result.core_log_level = 2;
            }
            else if (value == "error")
            {
                result.core_log_level = 3;
            }
            else if (value == "off")
            {
                result.core_log_level = 4;
            }
            else
            {
                throw std::runtime_error("--core-log-level must be debug, info, warn, error, or off");
            }
        }
        else if (arg == "--playtime")
        {
            const auto seconds = parse_playtime(value);
            if (!seconds)
            {
                throw std::runtime_error("--playtime requires a nonnegative integer number of seconds");
            }
            result.playtime = *seconds;
        }
        else if (arg == "--data-dir")
        {
            result.data_dir = value;
        }
        else if (arg == "--system-dir")
        {
            result.system_dir = value;
        }
        else if (arg == "--save-dir")
        {
            result.save_dir = value;
        }
        else if (arg == "--state-dir")
        {
            result.state_dir = value;
        }
        else if (arg == "--state-file")
        {
            result.state_file = value;
        }
        else if (arg == "--load-state")
        {
            result.load_state = value;
        }
        else if (arg == "--aspect-ratio")
        {
            result.aspect_mode = aspect_ratio::parse(value);
            if (result.aspect_mode < 0)
            {
                throw std::runtime_error("--aspect-ratio requires auto, 16:9, 4:3, 8:7, or stretch");
            }
        }
        else if (arg == "--save-name")
        {
            if (save_storage::folder_name(value, 200) != value)
            {
                throw std::runtime_error("--save-name must be a safe folder name, at most 200 bytes");
            }
            result.save_name = value;
        }
        else if (arg == "--game-id")
        {
            if (value.empty() || value.size() > 128 ||
                value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") !=
                    std::string::npos)
            {
                throw std::runtime_error("--game-id requires 1..128 letters, digits, underscores or hyphens");
            }
            result.game_id = value;
        }
        else if (arg == "--system")
        {
            if (!InputBindings::valid_system(value))
            {
                throw std::runtime_error("Unknown --system: " + value + ". See --help.");
            }
            result.system = value;
        }
        else if (arg == "--title")
        {
            result.title = value;
        }
        else if (arg == "--screenshot")
        {
            result.screenshot = value;
        }
        else if (arg == "--frames")
        {
            result.frames = number(value, 1, 10000000, arg);
        }
        else if (arg == "--volume")
        {
            result.volume = number(value, 0, 100, arg);
        }
        else if (arg == "--window-size")
        {
            auto x = value.find('x');
            if (x == std::string::npos)
            {
                throw std::runtime_error("--window-size requires WIDTHxHEIGHT");
            }
            result.width = number(value.substr(0, x), 480, 8192, "window width");
            result.height = number(value.substr(x + 1), 420, 8192, "window height");
        }
        else if (arg == "--scale")
        {
            if (value != "nearest" && value != "bilinear" && value != "sharp-bilinear" && value != "integer")
            {
                throw std::runtime_error("--scale must be nearest, bilinear, sharp-bilinear, or integer");
            }
            result.scaling = value == "integer" ? 3 : value == "sharp-bilinear" ? 2 : value == "bilinear" ? 1 : 0;
        }
        else if (arg == "--menu-shortcut")
        {
            if (value != "start-select" && value != "l3-r3" && value != "home")
            {
                throw std::runtime_error("--menu-shortcut must be start-select, l3-r3 or home");
            }
            result.stick_shortcut = value == "l3-r3";
            result.home_shortcut = value == "home";
        }
        else if (arg == "--core-option")
        {
            auto equals = value.find('=');
            if (equals == std::string::npos || !equals || equals + 1 == value.size())
            {
                throw std::runtime_error("--core-option requires KEY=VALUE");
            }
            result.core_options[value.substr(0, equals)] = value.substr(equals + 1);
        }
    }
    if (result.core.empty())
    {
        throw std::runtime_error("--core PATH is required. See cabinet-runner --help.");
    }
    if (!result.screenshot.empty() && !result.frames)
    {
        throw std::runtime_error("--screenshot requires --frames");
    }
    if (!result.state_dir.empty() && !result.state_file.empty())
    {
        throw std::runtime_error("Use either --state-dir or --state-file, not both");
    }
    result.explicit_flags = supplied;
    settings_store::read_global(result);
    for (const auto& [key, value] : result.core_options)
    {
        result.explicit_core_options.insert(key);
    }
    const auto system = result.system.empty() ? InputBindings::identify(result.rom, result.core) : result.system;
    settings_store::read_system(system, result);
    return result;
}

void print_runner_help(std::ostream& out)
{
    out << R"(cabinet-runner - standalone libretro game runner

Usage:
  cabinet-runner --core PATH [--rom PATH] [OPTIONS]

Core and content:
  --rom PATH             Optional game file. If omitted, the core must support
                         running without content (retro_load_game(nullptr)).
  --core PATH            Libretro shared library. No core is chosen implicitly.

Storage and firmware:
  --data-dir DIR         Base data directory. Default: ~/.local/share/cabinet/, shared with the home app.
  --system-dir DIR       Firmware/BIOS directory supplied to the core.
                         Default: DATA/system.
  --save-dir DIR         Save root. Default: DATA/saves. Files use SYSTEM/NAME/.
                         Battery RAM and RTC are saved automatically.
  --playtime SECONDS     Starting total active playtime (default: 0).
                         Updated in the save folder cabinet-game.txt every 15 seconds and on exit.
  --game-id ID           Stable save identity, preserved when the ROM moves.
                         Cabinet supplies a permanent library ID. Standalone callers may
                         supply their own (letters, digits, underscores, hyphens).
                         Default: content hash for memory-loaded ROMs, path hash
                         for cores that open files directly, or core filename for
                         contentless cores. Standalone defaults use Generic/.
  --save-name NAME       Readable folder name, independent of --game-id.
                         Cabinet supplies a persistent name; default: game ID.
  --state-dir DIR        Save-state root. Default: DATA/states. Slots are keyed
                         under SYSTEM/NAME/CORE/ by core content fingerprint.
  --state-file FILE      Use this exact menu save/load slot instead of --state-dir.
                         Existing files are not overwritten without confirmation.
  --load-state FILE      Load a Cabinet state at startup; leaves the save slot
                         unchanged. Requires the matching ROM and core build.
  --core-option K=V      Set a libretro core option before initialization.
                         Repeat for multiple options; the last value wins.
                         Option names and values are core-specific.

Window, video, audio, and input:
  --system ID            3ds, dc, gb, gba, gc, wii, gg, nes, n64, nds, md, sms, saturn,
                         psp, psx, ps2, snes, pce, or generic.
  --title TEXT           Window title. Default: ROM stem, or core name without content.
  --windowed             Start windowed, overriding this system's saved mode.
  --window-size WxH      Initial window size. Default: 640x576.
                         Width: 480..8192; height: 420..8192.
  --aspect-ratio MODE    auto (default), 16:9, 4:3, 8:7, or stretch.
  --stretch              Stretch the game image to fill the window.
  --keep-aspect          Preserve the game aspect ratio (default); overrides saved aspect ratio.
  --scale MODE           nearest, bilinear, sharp-bilinear (default), or integer.
                         Overrides this system's saved scaling mode.
  --volume PERCENT       Initial master volume, 0..100. Default: 80.
  --no-audio             Do not initialize or open an audio device.
  --dual-screen          Present DS/3DS bottom screen in a second resizable window.
                         The main window displays the top screen and menus.
                         Uses a temporary top/bottom core layout; ignored on other systems.
  --vulkan               Prefer Vulkan for hardware cores. Default for Dolphin/Flycast.
  --opengl               Prefer OpenGL/OpenGL ES, overriding the core default.
                         Other cores default to OpenGL; the last API flag wins.
  --no-vsync             Use content-rate timer presentation instead of VSync.
  --vrr                  Pace presentation at content rate for an enabled VRR display.
  --timing-stats         Print timing/audio summaries every five seconds and on exit.
  --fast-forward         Run uncapped with audio muted; disables VSync and timing waits.
  --core-log-level LEVEL Core messages: debug, info, warn (default), error, off.
  --menu-shortcut MODE   start-select (default), l3-r3 or home. Esc always opens menu.
  --swap-confirm-cancel  Swap the gamepad buttons for menu confirm and cancel.
  --show-menu-hint       Show a startup OSD hint if Start + Select/View is selected.
  --paused               Start in the menu after the first video frame (or after
                         loading --load-state).

Diagnostics:
  --frames COUNT         Exit after 1..10000000 display-loop iterations. Paused
                         iterations count too; not a count of emulated frames.
  --screenshot FILE.bmp  Write the final window to a BMP. Requires --frames.
                         Overwrites an existing screenshot at that path.
  --help, -h             Print help and exit without opening a window.

Examples:
  cabinet-runner --rom "/games/My Game.gbc" --core "/cores/gambatte_libretro.so"
  cabinet-runner --core 2048_libretro.so
  cabinet-runner --rom game.gbc --core core.so --data-dir ./profile --no-audio
  cabinet-runner --rom game.gbc --core core.so --system-dir /firmware
  cabinet-runner --rom game.gbc --core core.so --frames 120 --screenshot game.bmp

Relative paths are resolved from the working directory. No library database,
home window, external font/icon files, or configuration files are required.
Software video, OpenGL/OpenGL ES, and Vulkan hardware cores are supported.
Dolphin and Flycast prefer Vulkan by default. Direct3D is not supported.)";
}
