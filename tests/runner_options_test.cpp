#include "runner_options.h"
#include "save_storage.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <filesystem>
#include <fstream>
#include <SDL3/SDL.h>

static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static RunnerOptions parse(std::vector<std::string> args) {
    args.insert(args.begin(), "cabinet-runner");
    std::vector<char*> argv;
    for (auto& arg : args) argv.push_back(arg.data());
    return parse_runner_options(static_cast<int>(argv.size()), argv.data());
}
static void rejects(std::vector<std::string> args) {
    bool rejected = false;
    try { parse(std::move(args)); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Invalid command line was accepted");
}
int main() {
    try {
        const auto test_home = std::filesystem::temp_directory_path() / ("cabinet-options-" + std::to_string(SDL_GetTicksNS()));
        std::filesystem::create_directories(test_home);
        SDL_setenv_unsafe("HOME", test_home.string().c_str(), 1);
        const auto automatic_api = parse({"--rom", "game.iso", "--core", "renamed.so"});
        for (const auto core : {"dolphin-emu", "Dolphin", "dolphin", "Flycast"})
            require(automatic_api.prefers_vulkan(core), "Dolphin/Flycast did not default to Vulkan");
        for (const auto core : {"Azahar", "PPSSPP", "Mupen64Plus-Next", "melonDS DS", "Cabinet test core", ""})
            require(!automatic_api.prefers_vulkan(core), "Unrelated core default changed to Vulkan");
        const auto vulkan_api = parse({"--rom", "game.iso", "--core", "core.so", "--vulkan"});
        require(vulkan_api.prefers_vulkan("Azahar"), "Vulkan preference ignored");
        const auto opengl_api = parse({"--rom", "game.iso", "--core", "core.so", "--opengl"});
        require(!opengl_api.prefers_vulkan("dolphin-emu") && !opengl_api.prefers_vulkan("Flycast"), "OpenGL override ignored");
        require(!parse({"--rom", "game.iso", "--core", "core.so", "--vulkan", "--opengl"}).prefers_vulkan("Flycast"),
            "Last OpenGL flag did not win");
        require(parse({"--rom", "game.iso", "--core", "core.so", "--opengl", "--vulkan"}).prefers_vulkan("Azahar"),
            "Last Vulkan flag did not win");
        auto timing = parse({"--rom", "game.gbc", "--core", "core.so", "--timing-stats", "--no-vsync", "--vrr"});
        require(timing.timing_stats && !timing.vsync && timing.vrr, "Presentation options not parsed");
        require(parse({"--rom", "game.gbc", "--core", "core.so", "--fast-forward"}).fast_forward,
                "Uncapped fast-forward flag ignored");
        require(parse({"--rom", "game.iso", "--core", "core.so", "--game-id", "library-42"}).game_id == "library-42", "Stable game ID lost");
        require(parse({"--rom", "game.iso", "--core", "core.so", "--save-name", "Rayman 2 (USA)"}).save_name == "Rayman 2 (USA)", "Readable save name lost");
        for (const auto* name : {"../escape", "a/b", "a\\b", ".", "..", "trailing.", "bad\nname"})
            rejects({"--rom", "game.iso", "--core", "core.so", "--save-name", name});
        rejects({"--rom", "game.iso", "--core", "core.so", "--game-id", "../escape"});
        rejects({"--rom", "game.iso", "--core", "core.so", "--game-id", ""});
        require(parse({"--help"}).help && parse({"--rom", "missing.gbc", "-h"}).help, "Help must not require ROM/core");
        require(parse({"--rom", "game.gbc", "--core", "core.so", "--show-menu-hint"}).show_menu_hint, "Menu hint flag ignored");
        require(parse({"--rom", "game.gbc", "--core", "core.so", "--playtime", "12345"}).playtime == 12345, "Starting playtime lost");
        for (const auto* value : {"-1", "1.5", "12x", "9223372036854775808"})
            rejects({"--rom", "game.gbc", "--core", "core.so", "--playtime", value});
        require(parse({"--rom", "game.gbc", "--core", "core.so", "--scale", "integer"}).scaling == 3, "Integer scaling CLI ignored");
        auto defaults = parse({"--rom", "game.gbc", "--core", "core.so"});
        require(defaults.scaling == 2, "Default scaling must be Sharp Bilinear");
        require(parse({"--rom", "game.sfc", "--core", "core.so", "--scale", "bilinear"}).scaling == 1, "Bilinear CLI ignored");
        rejects({"--rom", "game.sfc", "--core", "core.so", "--scale", "linear"});
        require(!defaults.show_stats, "Stats overlay must default to off");
        require(defaults.confirm_save_states, "State confirmations must default to enabled");
        require(parse({"--rom", "game.cue", "--core", "core.so", "--aspect-ratio", "8:7"}).aspect_mode == aspect_ratio::SquareSNES, "Explicit aspect ratio ignored");
        require(parse({"--rom", "game.sfc", "--core", "core.so", "--scale", "sharp-bilinear"}).scaling == 2, "Sharp Bilinear CLI ignored");
        require(defaults.aspect_mode == aspect_ratio::Auto, "Stretch enabled by default");
        require(parse({"--rom", "game.cue", "--core", "core.so", "--stretch"}).aspect_mode == aspect_ratio::Stretch, "Stretch CLI ignored");
        require(defaults.core_log_level == 2, "Default core log level must be warn");
        require(parse({"--rom", "game.cue", "--core", "flycast.so", "--core-log-level", "debug"}).core_log_level == 0,
            "Core debug logging cannot be enabled");
        require(parse({"--rom", "game.cue", "--core", "flycast.so", "--core-log-level", "off"}).core_log_level == 4,
            "Core logging cannot be disabled");
        rejects({"--rom", "game.cue", "--core", "flycast.so", "--core-log-level", "verbose"});
        require(defaults.rom == "game.gbc" && defaults.core == "core.so" && defaults.volume == 80 && defaults.fullscreen && !defaults.show_menu_hint, "Default runner settings changed");
        require(!defaults.dual_screen, "Dual-screen presentation became opt-out");
        require(parse({"--core", "core.so", "--rom", "game.nds", "--dual-screen"}).dual_screen, "Dual-screen flag lost");
        auto options = parse({"--core", "/cores/a core.so", "--rom", "/games/a game.gbc",
            "--data-dir", "profile", "--system-dir", "bios", "--save-dir", "saves",
            "--state-file", "slot.state", "--load-state", "start.state", "--title", "A title",
            "--window-size", "800x600", "--scale", "bilinear", "--volume", "0",
            "--no-audio", "--menu-shortcut", "l3-r3", "--swap-confirm-cancel", "--paused", "--frames", "12",
            "--screenshot", "screen.bmp", "--core-option", "key=first", "--core-option", "key=second"});
        require(options.rom == "/games/a game.gbc" && options.core == "/cores/a core.so", "Paths with spaces changed");
        require(options.system_dir == "bios" && options.save_dir == "saves" && options.data_dir == "profile", "Explicit directories lost");
        require(options.state_file == "slot.state" && options.load_state == "start.state", "State paths lost");
        require(options.width == 800 && options.height == 600 && options.fullscreen && options.scaling && options.no_audio && options.volume == 0, "Video/audio flags lost");
        require(options.stick_shortcut && options.swap_confirm_cancel && options.paused && options.frames == 12, "Input/startup flags lost");
        require(options.core_options.at("key") == "second", "Core option overrides must use last value");
        require(parse({"--core", "core.so", "--rom", "-game.gbc"}).rom == "-game.gbc", "--rom must allow leading-dash paths");
        require(!parse({"--rom", "game.gbc", "--core", "core.so", "--windowed"}).fullscreen, "Windowed override lost");
        rejects({"--rom", "game.gbc", "--core", "core.so", "--fullscreen"});
        const auto profile = std::filesystem::temp_directory_path() / ("cabinet-options-" + std::to_string(SDL_GetTicksNS()));
        std::filesystem::create_directories(profile / "config");
        { std::ofstream config(profile / "config/runner.conf"); config << "fullscreen 0\npause_on_focus_loss 0\nhide_cursor 1\nconfirm_save_states 0\nshow_stats 1\n"; }
        require(parse({"--rom", "game.gbc", "--core", "core.so", "--data-dir", profile.string()}).fullscreen,
                "Legacy windowed preference overrode fullscreen default");
        require(!parse({"--rom", "game.gbc", "--core", "core.so", "--data-dir", profile.string(), "--windowed"}).fullscreen,
                "Explicit windowed option ignored");
        const auto interface_options = parse({"--rom", "game.gbc", "--core", "core.so", "--data-dir", profile.string()});
        require(!interface_options.pause_on_focus_loss && interface_options.hide_cursor, "Interface preferences not loaded");
        require(!interface_options.confirm_save_states, "Global confirmation preference not loaded");
        require(interface_options.show_stats, "Global stats preference not loaded");
        std::filesystem::create_directories(profile / "config/video");
        { std::ofstream video(profile / "config/video/n64.conf");
          video << "fullscreen 0\naspect_ratio 4\nscaling 1\nresolution \"mupen64plus-43screensize\" \"1920x1440\"\n"; }
        const auto n64 = parse({"--rom", "game.z64", "--core", "core.so", "--data-dir", profile.string()});
        require(n64.aspect_mode == aspect_ratio::Stretch && !n64.fullscreen && n64.scaling && n64.core_options.at("mupen64plus-43screensize") == "1920x1440", "System video preferences not restored");
        const auto gb = parse({"--rom", "game.gbc", "--core", "core.so", "--data-dir", profile.string()});
        require(gb.aspect_mode == aspect_ratio::Auto && gb.fullscreen && gb.scaling == defaults.scaling && gb.core_options.empty(), "Video preferences leaked across systems");
        { std::ofstream advanced(profile / "config/video/gc.conf");
          advanced << "vsync 0\nvrr 1\ncore_option \"dolphin_main_cpu_thread\" \"disabled\"\n"; }
        const auto gc = parse({"--rom", "game.rvz", "--core", "dolphin_libretro.so", "--data-dir", profile.string()});
        require(!gc.vsync && gc.vrr && gc.core_options.at("dolphin_main_cpu_thread") == "disabled", "Advanced system settings not restored");
        const auto gc_cli = parse({"--rom", "game.rvz", "--core", "dolphin_libretro.so", "--data-dir", profile.string(), "--core-option", "dolphin_main_cpu_thread=enabled"});
        require(gc_cli.core_options.at("dolphin_main_cpu_thread") == "enabled", "Saved Advanced settings overrode CLI");
        const auto overrides = parse({"--rom", "game.z64", "--core", "core.so", "--data-dir", profile.string(), "--keep-aspect", "--scale", "nearest", "--core-option", "mupen64plus-43screensize=640x480"});
        require(overrides.aspect_mode == aspect_ratio::Auto && !overrides.scaling && overrides.core_options.at("mupen64plus-43screensize") == "640x480", "Saved video settings overrode CLI");
        { std::ofstream video(profile / "config/video/n64.conf"); video << "fullscreen 1\naspect_ratio 3\nscaling 2\n"; }
        require(parse({"--rom", "game.z64", "--core", "core.so", "--data-dir", profile.string()}).scaling == 2, "Sharp Bilinear system setting not restored");
        { std::ofstream video(profile / "config/video/n64.conf"); video << "fullscreen 1\naspect_ratio 3\nscaling 3\n"; }
        require(parse({"--rom", "game.z64", "--core", "core.so", "--data-dir", profile.string()}).scaling == 3, "Integer system setting not restored");
        require(parse({"--rom", "game.z64", "--core", "core.so", "--data-dir", profile.string(), "--scale", "nearest"}).scaling == 0, "Integer setting overrode CLI");
        require(parse({"--rom", "game.z64", "--core", "core.so", "--data-dir", profile.string()}).aspect_mode == aspect_ratio::SquareSNES, "Fixed system aspect ratio not restored");
        require(parse({"--rom", "game.z64", "--core", "core.so", "--data-dir", profile.string(), "--aspect-ratio", "16:9"}).aspect_mode == aspect_ratio::Wide, "Saved ratio overrode explicit ratio");
        rejects({"--rom", "game.bin", "--core", "core.so", "--aspect-ratio", "invalid"});
        require(!parse({"--rom", "game.z64", "--core", "core.so", "--data-dir", profile.string(), "--windowed"}).fullscreen, "Saved fullscreen overrode windowed CLI");
        std::filesystem::create_directories(profile / "config/input");
        require(parse({"--rom", "game.z64", "--core", "core.so", "--data-dir", profile.string()}).rumble, "Rumble should default to enabled");
        save_storage::write(profile / "config/input/n64-settings.conf", {{"rumble", "0"}});
        require(!parse({"--rom", "game.z64", "--core", "core.so", "--data-dir", profile.string()}).rumble, "System rumble setting not loaded");
        require(parse({"--rom", "game.gba", "--core", "core.so", "--data-dir", profile.string()}).rumble, "System rumble setting leaked to another system");
        { std::ofstream device(profile / "config/input/wii-device.conf"); device << "wii_controller \"2\"\n"; }
        require(parse({"--rom", "game.iso", "--core", "core.so", "--data-dir", profile.string(), "--system", "wii"}).wii_controller == 2, "Wii controller setting not loaded");
        std::filesystem::remove_all(profile);
        require(parse({"--rom", "game.bin", "--core", "core.so", "--system", "n64"}).system == "n64", "Explicit system profile lost");
        rejects({"--rom", "game.bin", "--core", "core.so", "--system", "../bad"});
        require(parse({"--rom", "game.gbc", "--core", "core.so", "--menu-shortcut", "home"}).home_shortcut, "Home shortcut CLI lost");
        rejects({}); rejects({"--rom", "game.gbc"}); require(parse({"--core", "core.so"}).rom.empty(), "Core-only launch rejected");
        rejects({"--rom", "one.gbc", "--rom", "two.gbc", "--core", "core.so"});
        rejects({"game.gbc", "--core", "core.so"}); rejects({"--core", "core.so", "--", "game.gbc"}); rejects({"--rom", "game.gbc", "other.gbc", "--core", "core.so"});
        rejects({"--rom", "game.gbc", "--core"}); rejects({"--rom", "game.gbc", "--core", ""});
        for (auto value : {"0", "-1", "12garbage", "10000001", "999999999999999"})
            rejects({"--rom", "game.gbc", "--core", "core.so", "--frames", value});
        rejects({"--rom", "game.gbc", "--core", "core.so", "--volume", "101"});
        rejects({"--rom", "game.gbc", "--core", "core.so", "--window-size", "1x2"});
        rejects({"--rom", "game.gbc", "--core", "core.so", "--scale", "unknown"});
        rejects({"--rom", "game.gbc", "--core", "core.so", "--menu-shortcut", "guide"});
        rejects({"--rom", "game.gbc", "--core", "core.so", "--core-option", "invalid"});
        rejects({"--rom", "game.gbc", "--core", "core.so", "--screenshot", "screen.bmp"});
        rejects({"--rom", "game.gbc", "--core", "core.so", "--state-dir", "states", "--state-file", "slot"});
        rejects({"--rom", "game.gbc", "--core", "core.so", "--unknown"});
        std::filesystem::remove_all(test_home);
        std::cout << "Runner options and validation passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
