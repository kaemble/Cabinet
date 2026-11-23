// Drive the real runner loop and persistence through SDL events. The synthetic
// core provides deterministic content and observations, not replacement host logic.

#include <SDL3/SDL.h>
#include <atomic>
static std::atomic<unsigned> presented_frames{0};
static std::atomic<SDL_WindowID> presented_window{0};
static bool tracked_present(SDL_Renderer* renderer) {
    const bool result = SDL_RenderPresent(renderer);
    if (result) {
        presented_window = SDL_GetWindowID(SDL_GetRenderWindow(renderer));
        ++presented_frames;
    }
    return result;
}
// Count the real loop's presents without adding production test hooks.
#define SDL_RenderPresent tracked_present
#include "../src/runner.cpp"
#undef SDL_RenderPresent
#include <cstring>
#include "test_core_api.h"
#include "test_support.h"
#include "virtual_gamepad.h"
#include <atomic>
#include <thread>
#include <functional>

#ifdef CABINET_ARMADA_DRM
static bool allow_armada_probe = false;
static unsigned armada_probes = 0;
static unsigned armada_video_configurations = 0;
extern "C" bool __wrap__ZN9ArmadaDrm15configure_videoEv() {
    ++armada_video_configurations;
    if (!allow_armada_probe) throw std::runtime_error("Runner without --dual-screen configured Armada video");
    return false;
}
extern "C" std::unique_ptr<ArmadaDrm> __wrap__ZN9ArmadaDrm10try_createEP12SDL_Renderer(SDL_Renderer*) {
    ++armada_probes;
    if (!allow_armada_probe) throw std::runtime_error("Runner without --dual-screen entered Armada code");
    return nullptr; // Model an environment where no Armada lease is available.
}
#endif

static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static TestCoreRead core_read;
static std::atomic<bool> cancelled{false};
static void wait_for(const std::function<bool()>& condition, const char* message) {
    const auto deadline = SDL_GetTicks() + 5000;
    while (!condition()) {
        if (cancelled || SDL_GetTicks() >= deadline) throw std::runtime_error(message);
        SDL_Delay(2);
    }
}
static void key(SDL_Scancode code, bool down = true, bool repeat = false) {
    SDL_Event e{}; e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = code; e.key.repeat = repeat;
    require(SDL_PushEvent(&e), SDL_GetError());
}
static void press(SDL_Scancode code) { key(code); key(code, false); }
static void down(int count) { for (int i = 0; i < count; ++i) press(SDL_SCANCODE_DOWN); }
static void enter() { press(SDL_SCANCODE_RETURN); }
static void back() { press(SDL_SCANCODE_ESCAPE); }
static void configuration() { back(); down(4); enter(); }
static void restart_from_configuration() { back(); press(SDL_SCANCODE_UP); enter(); down(1); enter(); }
static void quit_game() { back(); down(6); enter(); down(1); enter(); }
static void frames(unsigned count = 4) {
    const auto target = core_read().runs + count;
    wait_for([&] { return core_read().runs >= target; }, "Runner did not advance emulation");
}
static RunnerOptions options(const fs::path& root, const std::string& core, const std::vector<std::string>& extra = {}) {
    std::vector<std::string> args{"runner", "--rom", (root / "game.bin").string(), "--core", core,
        "--system", "gc", "--game-id", "fixture", "--save-name", "Fixture game", "--data-dir", root.string(),
        "--windowed", "--window-size", "480x420", "--frames", "120000"};
    args.insert(args.end(), extra.begin(), extra.end());
    std::vector<char*> argv; for (auto& arg : args) argv.push_back(arg.data());
    return parse_runner_options(argv.size(), argv.data());
}
static void run(const RunnerOptions& opts, const std::function<void()>& script) {
    cancelled = false;
    const auto boot = core_read().boots;
    std::exception_ptr driver_error;
    std::thread driver([&] {
        try {
            wait_for([&] { return core_read().boots > boot; }, "Core did not initialize");
            frames(); script();
        } catch (...) {
            driver_error = std::current_exception();
            SDL_Event e{}; e.type = SDL_EVENT_QUIT; SDL_PushEvent(&e);
            try { press(SDL_SCANCODE_DOWN); enter(); } catch (...) {}
        }
    });
    std::exception_ptr runner_error;
    int result = -1;
    try { result = run_game(opts); } catch (...) { runner_error = std::current_exception(); }
    cancelled = true; driver.join(); SDL_Quit();
    if (driver_error) std::rethrow_exception(driver_error);
    if (runner_error) std::rethrow_exception(runner_error);
    require(result == 0, "Runner exited with an error");
}
static void runahead_settings(const fs::path& root, const std::string& core)
{
    const auto supported = root / "gambatte_libretro.so";
    fs::create_symlink(fs::absolute(core), supported);
    run(options(root, supported.string(), {"--no-audio"}), [&]
    {
        configuration(); down(3); enter(); down(2);
        press(SDL_SCANCODE_RIGHT); press(SDL_SCANCODE_RIGHT);
        back(); back(); back();
        wait_for([&] { return core_read().loads > 0; }, "Run-ahead menu did not activate rollback");
        quit_game();
    });
    RunnerOptions persisted;
    persisted.data_dir = root.string();
    settings_store::read_system("gc", persisted);
    require(persisted.runahead_frames == 2, "Run-ahead did not persist in system settings");
    const auto saved = core_read().saves;
    run(options(root, core, {"--no-audio"}), [&]
    {
        frames(8);
        require(core_read().saves == saved, "Unlisted core inherited active frontend run-ahead");
        quit_game();
    });
    const auto loaded = core_read().loads;
    run(options(root, supported.string(), {"--no-audio"}), [&]
    {
        wait_for([&] { return core_read().loads > loaded; }, "Relaunch lost the run-ahead setting");
        configuration(); down(3); enter(); down(2);
        press(SDL_SCANCODE_LEFT); press(SDL_SCANCODE_LEFT);
        back(); back(); back();
        frames();
        quit_game();
    });
    persisted.runahead_frames = 3;
    settings_store::read_system("gc", persisted);
    require(persisted.runahead_frames == 0, "Turning off run-ahead did not persist");
}

static void idle_menu(const fs::path& root, const std::string& core) {
    auto opts = options(root, core, {"--no-audio"});
    opts.frames = 0; // Exercise the event-driven wait used in normal launches.
    run(opts, [&] {
        back(); SDL_Delay(120);
        const auto idle = presented_frames.load();
        const auto runs = core_read().runs;
        SDL_Delay(200);
        require(presented_frames == idle, "Unchanged menu kept presenting frames");
        require(core_read().runs == runs, "Idle menu advanced emulation");
        press(SDL_SCANCODE_DOWN);
        wait_for([&] { return presented_frames > idle; }, "Menu navigation did not redraw");
        SDL_Delay(80);
        const auto navigated = presented_frames.load();
        SDL_Delay(150);
        require(presented_frames == navigated, "Menu kept redrawing after navigation");
        SDL_Event exposed{}; exposed.type = SDL_EVENT_WINDOW_EXPOSED;
        exposed.window.windowID = presented_window;
        require(SDL_PushEvent(&exposed), SDL_GetError());
        wait_for([&] { return presented_frames > navigated; }, "Window expose did not redraw menu");
        enter(); // Save state: first save resumes without confirmation.
        wait_for([&] { return core_read().runs > runs; }, "Game did not resume from the idle menu");
        frames(); quit_game();
    });
}

static void all_core_options(const fs::path& root, const std::string& core) {
    for (const std::string api : {"v1", "v1-intl", "v2", "v2-intl"}) {
        auto profile = root / api; fs::create_directories(profile);
        fs::copy_file(root / "game.bin", profile / "game.bin");
        auto opts = options(profile, core, {"--no-audio", "--fast-forward", "--core-option", "cabinet_test_options_api=" + api});
        opts.frames = 4;
        require(run_game(opts) == 0, "Modern option registration failed"); SDL_Quit();
        require(std::string(core_read().resolution) == "second", "Modern enum default did not reach core");
        auto restored = options(profile, core, {"--no-audio", "--fast-forward", "--core-option", "cabinet_test_options_api=" + api});
        require(restored.core_options.at("fixture_enum") == "second", "Arbitrary enum option did not persist");
    }
}

static void contentless_core(const fs::path& root, const std::string& core) {
    auto opts = options(root, core, {"--no-audio", "--fast-forward"});
    opts.rom.clear(); opts.system.clear(); opts.game_id.clear(); opts.save_name.clear();
    opts.frames = 4;
    bool rejected = false;
    try { run_game(opts); }
    catch (const std::exception& e) {
        rejected = std::string(e.what()).find("does not support running without content") != std::string::npos;
    }
    SDL_Quit();
    require(rejected, "A core without contentless support accepted an omitted ROM");
    opts.core_options["cabinet_test_contentless"] = "enabled";
    const auto before = core_read().runs;
    require(run_game(opts) == 0 && core_read().runs - before == 4, "Contentless core did not execute");
    SDL_Quit();
    const auto metadata = save_storage::read(root / "saves/Generic" /
        ("core-" + fs::path(core).stem().string()) / "cabinet-game.txt");
    require(metadata.at("title") == "Cabinet test core" && !metadata.contains("rom_hash"),
        "Contentless metadata invented ROM content or lost core title");
}

static void frame_time_callbacks(const fs::path& root, const std::string& core) {
    {
        Session host; current = &host;
        static std::vector<retro_usec_t> times;
        times.clear();
        retro_frame_time_callback timing{[](retro_usec_t usec) { times.push_back(usec); }, 16667};
        require(!environment(RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK, nullptr), "Null frame time registration accepted");
        require(environment(RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK, &timing), "Frame time registration rejected");
        host.notify_frame_time(1000000000);
        host.notify_frame_time(1020000000);
        set_fast_forward(host, true);
        host.notify_frame_time(1021000000);
        host.notify_frame_time(1022000000);
        set_fast_forward(host, false);
        host.notify_frame_time(9000000000);
        require(times == std::vector<retro_usec_t>{16667, 20000, 16667, 16667, 16667},
            "Frame time did not use elapsed time or reset around fast-forward");
        timing.callback = nullptr;
        require(environment(RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK, &timing), "Callback removal rejected");
        host.notify_frame_time(9020000000);
        require(times.size() == 5, "Removed callback was invoked");
        current = nullptr;
    }
    for (const bool fast : {false, true}) {
        auto opts = options(root, core, {"--no-audio", "--core-option", "cabinet_test_frame_time=enabled"});
        opts.fast_forward = fast; opts.frames = 8;
        const auto before = core_read();
        require(run_game(opts) == 0, "Core requiring frame time failed to launch");
        SDL_Quit();
        const auto after = core_read();
        require(after.frame_time_calls - before.frame_time_calls == after.runs - before.runs &&
                after.frame_time_calls - before.frame_time_calls == 8, "Frame time callback missing from runner loop");
        require(after.frame_time > 0 && (!fast || after.frame_time == 16667), "Incorrect frame duration");
    }
}

static void async_audio_callbacks(const fs::path& root, const std::string& core)
{
    const auto supported = root / "gambatte_libretro.so";
    fs::create_symlink(fs::absolute(core), supported);
    auto opts = options(root, supported.string(), {"--core-option", "cabinet_test_async_audio=enabled",
                                   "--core-option", "cabinet_test_slow_video=enabled"});
    opts.runahead_frames = 2;
    const auto original_saves = core_read().saves;
    run(opts, [&]
    {
        require(core_read().async_audio_accepted && core_read().async_audio_active,
                "Callback audio registration did not activate");
        require(core_read().saves == original_saves, "Frontend run-ahead ran with callback audio");
        frames(1);
        const auto before = core_read();
        SDL_Delay(50);
        const auto between = core_read();
        require(between.runs == before.runs && between.async_audio_calls > before.async_audio_calls,
                "Audio production waited for the next video frame");
        back();
        wait_for([] { return !core_read().async_audio_active; }, "Menu did not deactivate callback audio");
        const auto paused = core_read();
        SDL_Delay(100);
        require(core_read().async_audio_calls == paused.async_audio_calls && core_read().runs == paused.runs,
                "Paused core continued producing audio or video");
        back();
        wait_for([&] { return core_read().async_audio_calls > paused.async_audio_calls; },
                 "Closing the menu did not resume callback audio");
        const auto saves = core_read().saves;
        press(SDL_SCANCODE_F5);
        wait_for([&] { return core_read().saves > saves; }, "Async core could not save state");
        frames(1);
        const auto loads = core_read().loads;
        press(SDL_SCANCODE_F7);
        wait_for([&] { return core_read().loads > loads; }, "Async core could not load state");
        frames(1);
        key(SDL_SCANCODE_TAB);
        wait_for([] { return core_read().fast_forward && !core_read().async_audio_active; },
                 "Fast-forward did not deactivate callback audio");
        const auto fast = core_read().async_audio_calls;
        SDL_Delay(50);
        require(core_read().async_audio_calls == fast, "Fast-forward requested callback audio");
        key(SDL_SCANCODE_TAB, false);
        wait_for([&] { return core_read().async_audio_calls > fast; },
                 "Leaving fast-forward did not resume callback audio");
        quit_game();
    });
    auto after = core_read();
    require(!after.async_audio_active && !after.async_audio_after_unload && !after.async_audio_during_state,
            "Callback audio remained active during state operations or core shutdown");
    require(after.async_audio_enables == after.async_audio_disables, "Callback lifecycle was unbalanced");
    for (const bool no_audio : {false, true})
    {
        opts.no_audio = no_audio;
        opts.fast_forward = !no_audio;
        opts.frames = 8;
        const auto before = core_read();
        require(run_game(opts) == 0, "Muted async core failed to launch");
        SDL_Quit();
        after = core_read();
        require(after.async_audio_accepted && after.async_audio_calls == before.async_audio_calls &&
                after.async_audio_enables == before.async_audio_enables && !after.async_audio_after_unload,
                "Muted output activated callback audio");
    }
}

static void timing_stats(const fs::path& Root, const std::string& Core)
{
    for (const bool EnableTiming : {false, true})
    {
        for (const bool EnableOverlay : {false, true})
        {
            auto Options = options(Root, Core, {"--no-audio", "--fast-forward"});
            Options.frames = 16;
            Options.timing_stats = EnableTiming;
            Options.show_stats = EnableOverlay;
            const auto Before = core_read().runs;
            std::ostringstream Output;
            auto* Previous = std::cout.rdbuf(Output.rdbuf());
            int Result;
            try
            {
                Result = run_game(Options);
            }
            catch (...)
            {
                std::cout.rdbuf(Previous);
                SDL_Quit();
                throw;
            }
            std::cout.rdbuf(Previous);
            SDL_Quit();
            require(Result == 0 && core_read().runs - Before == 16,
                "Timing instrumentation changed the number of core executions");
            const auto Log = Output.str();
            require((Log.find("[timing]") != std::string::npos) == EnableTiming,
                "Timing reports did not respect --timing-stats independently of the overlay");
            if (EnableTiming)
            {
                require(Log.find("presents=16 runs=16") != std::string::npos &&
                    Log.find("core_ms(avg/max)=") != std::string::npos &&
                    Log.find("present_ms(avg/max)=") != std::string::npos,
                    "Enabled timing report lost execution or presentation measurements");
            }
        }
    }
}

static std::atomic<int> overlay_status{-1};
static void observe_overlay() {
    const auto& ui = current->frontend;
    overlay_status = !ui.show_stats ? (ui.stats_text.empty() ? 0 : -1) :
        ui.stats_text.starts_with("FPS: --") ? 1 : ui.stats_text.starts_with("FPS: ") ? 2 : -1;
}
static void toggle_stats() {
    back(); down(5); enter(); down(5); enter(); back(); back();
}
static void stats_overlay(const fs::path& root, const std::string& core, TestCoreObserve observe) {
    observe(observe_overlay);
    run(options(root, core, {"--no-audio"}), [&] {
        require(overlay_status == 0, "Stats were enabled by default");
        toggle_stats();
        wait_for([&] { return overlay_status == 2; }, "Enabled overlay did not produce statistics");
        toggle_stats();
        wait_for([&] { return overlay_status == 0; }, "Disabling stats retained the overlay");
        toggle_stats(); frames(); quit_game();
    });
    require(save_storage::read(root / "config/runner.conf").at("show_stats") == "1",
        "Stats preference did not persist globally");
    const auto restored = options(root, core, {"--no-audio"});
    require(restored.show_stats, "Stats preference not restored at launch");
    run(restored, [&] {
        require(overlay_status > 0, "Relaunch lost the stats overlay");
        toggle_stats(); frames(); quit_game();
    });
    require(save_storage::read(root / "config/runner.conf").at("show_stats") == "0",
        "Disabled stats preference did not persist");
    observe(nullptr);
}

static fs::path saved_state(const fs::path& root) {
    std::error_code error;
    if (fs::exists(root / "states")) for (const auto& entry : fs::recursive_directory_iterator(root / "states", error))
        if (entry.path().extension() == ".state") return entry.path();
    return {};
}
static std::string read_text(const fs::path& file) {
    std::ifstream in(file); return {std::istreambuf_iterator<char>(in), {}};
}

static void inheritance(const fs::path& root, const std::string& core) {
    const auto system = root / "config/video/gc.conf";
    fs::create_directories(system.parent_path());
    std::ofstream(system) << "fullscreen 0\nscaling 0\nvsync 1\nvrr 0\nresolution \"dolphin_efb_scale\" \"1\"\n"
        << "core_option \"dolphin_main_cpu_thread\" \"enabled\"\n";
    const auto original = read_text(system);
    InputBindings bindings; bindings.configure("gc", root / "config/input");
    bindings.assign(0, true, SDL_SCANCODE_T);
    InputBindings hotkeys; hotkeys.configure_hotkeys("gc", root / "config/input"); hotkeys.assign(0, true, SDL_SCANCODE_F6);
    save_storage::write(root / "config/input/gc-settings.conf", {{"rumble", "0"}, {"stick_deadzone", "7"}, {"trigger_deadzone", "13"}});
    const auto game = root / "config/games/gc/fixture/settings.conf";
    auto opts = options(root, core, {"--no-audio"});
    require(opts.stick_deadzone == 7 && opts.trigger_deadzone == 13, "System input deadzones not loaded");
    run(opts, [&] {
        require(std::string(core_read().resolution) == "1", "System resolution not inherited at boot");
        configuration(); down(4); enter();
        wait_for([&] { return fs::exists(game); }, "Create game config did not persist");
        RunnerOptions copied; settings_store::read_game(game, copied);
        require(!copied.rumble && copied.scaling == 0 && copied.core_options.at("dolphin_efb_scale") == "1", "Game config did not copy effective system settings");
        require(copied.stick_deadzone == 7 && copied.trigger_deadzone == 13, "Game config did not inherit system deadzones");
        InputBindings game_keys; game_keys.configure_hotkeys("gc", game.parent_path() / "input");
        require(game_keys.rows()[0].key == SDL_SCANCODE_F6, "Game config did not copy hotkeys");
        game_keys.assign(0, true, SDL_SCANCODE_F8);
        InputBindings game_buttons; game_buttons.configure("gc", game.parent_path() / "input");
        require(game_buttons.rows()[0].key == SDL_SCANCODE_T, "Game config did not copy bindings");
        game_buttons.assign(0, true, SDL_SCANCODE_Y);
        // Configuration selection remains Create/Reset game config (4).
        for (int i = 0; i < 4; ++i) press(SDL_SCANCODE_UP);
        enter(); down(4); press(SDL_SCANCODE_RIGHT); press(SDL_SCANCODE_RIGHT);
        down(1); press(SDL_SCANCODE_LEFT); back(); // Input: sticks 9%, triggers 12%.
        down(1); enter();
        down(2); press(SDL_SCANCODE_RIGHT); back(); // Video: resolution 2
        down(2); enter(); down(2); enter(); // Advanced: More Core options.
        down(1); enter(); down(1); enter(); back(); back(); // Dual core: disabled.
        const auto boot = core_read().boots;
        restart_from_configuration();
        wait_for([&] { return core_read().boots == boot + 1; }, "Pending settings did not reinitialize core");
        frames();
        require(std::string(core_read().resolution) == "2" && std::string(core_read().dual_core) == "disabled",
                ("Restart did not apply pending settings: resolution=" + std::string(core_read().resolution) +
                 ", dual_core=" + std::string(core_read().dual_core)).c_str());
        require(core_read().resets == 0, "Pending settings used retro_reset instead of a full restart");
        quit_game();
    });
    require(read_text(system) == original, "Game settings overwrote system settings");
    RunnerOptions persisted; settings_store::read_game(game, persisted);
    require(persisted.core_options.at("dolphin_efb_scale") == "2", "Game resolution did not survive exit");
    require(persisted.stick_deadzone == 9 && persisted.trigger_deadzone == 12, "Game deadzones did not survive restart and exit");
    const auto relaunch = options(root, core, {"--no-audio"});
    run(relaunch, [&] {
        require(current->frontend.stick_deadzone == 9 && current->frontend.trigger_deadzone == 12,
                "Relaunch ignored game deadzones");
        require(std::string(core_read().resolution) == "2" && std::string(core_read().dual_core) == "disabled", "Relaunch ignored game settings");
        const auto saved = core_read().saves;
        press(SDL_SCANCODE_F8);
        wait_for([&] { return core_read().saves > saved; }, "Relaunch did not use game-specific hotkeys");
        frames();
        configuration(); down(4); enter(); down(1); enter();
        wait_for([&] { return !fs::exists(game); }, "Reset to inherited did not dereference game settings");
        const auto boot = core_read().boots;
        restart_from_configuration();
        wait_for([&] { return core_read().boots == boot + 1; }, "Inherited settings did not trigger restart");
        frames(); require(std::string(core_read().resolution) == "1" && std::string(core_read().dual_core) == "enabled", "Reset did not restore system/core defaults");
        require(current->frontend.stick_deadzone == 7 && current->frontend.trigger_deadzone == 13,
                "Reset and restart did not restore inherited deadzones");
        const auto inherited_saves = core_read().saves;
        press(SDL_SCANCODE_F8); frames();
        require(core_read().saves == inherited_saves, "Reset retained the game-specific hotkey");
        press(SDL_SCANCODE_F6);
        wait_for([&] { return core_read().saves > inherited_saves; }, "Reset did not restore inherited hotkeys");
        frames();
        quit_game();
    });
    RunnerOptions inherited; settings_store::read_game(system, inherited);
    const auto inherited_input = options(root, core, {"--no-audio"});
    require(inherited_input.stick_deadzone == 7 && inherited_input.trigger_deadzone == 13, "Reset did not restore inherited input deadzones");
    require(!fs::exists(game) && inherited.core_options.at("dolphin_efb_scale") == "1" && inherited.scaling == 0 && inherited.vsync,
        "Reset changed system settings or recreated game config");
    hotkeys.configure_hotkeys("gc", root / "config/input"); bindings.configure("gc", root / "config/input");
    require(hotkeys.rows()[0].key == SDL_SCANCODE_F6 && bindings.rows()[0].key == SDL_SCANCODE_T, "Game binding edits leaked into inherited profiles");
    // A game file deliberately disagrees with both the system and CLI.
    fs::create_directories(game.parent_path());
    std::ofstream(game) << "resolution \"dolphin_efb_scale\" \"2\"\nvolume 10\nscaling 1\nvsync 1\n";
    run(options(root, core, {"--no-audio", "--core-option", "dolphin_efb_scale=3", "--volume", "42", "--scale", "integer", "--no-vsync"}), [&] {
        require(std::string(core_read().resolution) == "3", "Game resolution overrode explicit CLI"); quit_game();
    });
    RunnerOptions cli; settings_store::read_game(game, cli);
    require(cli.volume == 42 && cli.scaling == 3 && !cli.vsync && cli.core_options.at("dolphin_efb_scale") == "3", "Game settings overrode explicit presentation/audio CLI options");
}
static void renderer_restart(const fs::path& root, const std::string& core) {
    run(options(root, core, {"--no-audio"}), [&] {
        for (int i = 0; i < 2; ++i) {
            require(!current->core_options.resolution_restart_pending,
                    "Plain restart fixture unexpectedly has a pending resolution change");
            const auto boot = core_read().boots;
            back(); down(3); enter(); // Restart game, default selection is Cancel.
            back(); back(); frames();
            require(core_read().boots == boot, "Cancelling restart reinitialized the core");
            back(); down(3); enter(); down(1); enter();
            wait_for([&] { return core_read().boots == boot + 1; }, "Plain restart did not reinitialize the core");
            frames(); // Successful restarts must automatically resume emulation.
            require(core_read().resets == 0 && !current->frontend.paused(),
                    "Plain restart called retro_reset or left the game paused");
        }
        quit_game();
    });
    run(options(root, core, {"--no-audio", "--system", "n64", "--core-option", "cabinet_test_mode=mupen"}), [&] {
        require(std::string(core_read().renderer) == "gliden64", "Wrong initial renderer");
        const auto boot = core_read().boots;
        configuration(); down(3); enter(); down(2); press(SDL_SCANCODE_RIGHT); back();
        restart_from_configuration();
        wait_for([&] { return core_read().boots == boot + 1; }, "Renderer selection did not reinitialize core");
        frames();
        require(std::string(core_read().renderer) == "parallel" && std::string(core_read().rsp) == "parallel" && core_read().resets == 0,
                "Renderer restart lost RDP/RSP dependency or only reset the game");
        quit_game();
    });
}

static std::atomic<bool> saw_fast{false}, saw_clean_resume{false}, saw_primed{false}, bad_audio{false};
static bool previous_fast = false;
static void observe_audio() {
    // Called on the runner thread before and after the core produces audio.
    if (current->fast_forward) {
        saw_fast = true;
        if (!current->audio || current->audio.started() || current->audio.queued_bytes() != 0) bad_audio = true;
    } else {
        if (previous_fast) saw_clean_resume = current->audio && !current->audio.started() && current->audio.queued_bytes() == 0;
        if (saw_clean_resume && current->audio.started()) saw_primed = true;
    }
    previous_fast = current->fast_forward;
}
static void hotkeys(const fs::path& root, const std::string& core, TestCoreObserve observe) {
    InputBindings bindings; bindings.configure_hotkeys("gc", root / "config/input");
    bindings.add(0, false, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER + 1);
    bindings.add(1, false, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER + 1);
    bindings.add(2, false, SDL_GAMEPAD_BUTTON_WEST + 1);
    observe(observe_audio);
    run(options(root, core), [&] {
        press(SDL_SCANCODE_F5);
        wait_for([&] { return !saved_state(root).empty(); }, "F5 did not save state");
        frames();
        const auto state = saved_state(root);
        auto bytes = read_content(state); uint64_t expected;
        require(bytes.size() == 16, "Unexpected synthetic state payload"); std::memcpy(&expected, bytes.data() + 8, sizeof(expected));
        const auto loads = core_read().loads;
        press(SDL_SCANCODE_F7);
        wait_for([&] { return core_read().loads > loads; }, "F7 did not load state");
        require(core_read().loaded_value == expected, "F7 restored the wrong progress"); frames();
        const auto before = core_read().saves;
        key(SDL_SCANCODE_F5);
        wait_for([&] { return core_read().saves > before; }, "Held F5 did not save");
        frames(8); const auto saved = core_read().saves;
        key(SDL_SCANCODE_F5, true, true); frames(8);
        require(core_read().saves == saved, "Held/repeating save hotkey repeatedly wrote the state");
        key(SDL_SCANCODE_F5, false); frames();
        key(SDL_SCANCODE_TAB); wait_for([&] { return saw_fast.load() && core_read().fast_forward; }, "Tab did not activate runner fast-forward");
        require(core_read().throttle == RETRO_THROTTLE_FAST_FORWARD, "Core was not told about fast-forward");
        key(SDL_SCANCODE_TAB, false); wait_for([&] { return saw_primed.load(); }, "Audio did not recover after releasing Tab");
        require(saw_clean_resume && !bad_audio, "Fast-forward did not clear/drop/reprime audio correctly");
        require(!core_read().fast_forward && core_read().throttle == RETRO_THROTTLE_NONE, "Core throttle state did not recover");
        SDL_VirtualJoystickDesc desc{}; SDL_INIT_INTERFACE(&desc); identify_virtual_gamepad(desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD; desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT; desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
        const auto id = SDL_AttachVirtualJoystick(&desc); require(id != 0, SDL_GetError());
        auto* joystick = SDL_OpenJoystick(id); require(joystick, SDL_GetError()); frames(8);
        auto button = [&](SDL_GamepadButton which, bool held) { require(SDL_SetJoystickVirtualButton(joystick, which, held), SDL_GetError()); };
        const auto saves = core_read().saves;
        button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true);
        wait_for([&] { return core_read().saves > saves; }, "Gamepad save hotkey did not write state");
        frames(8); require(core_read().saves == saves + 1, "Held gamepad save hotkey repeated");
        button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false); frames(8);
        bytes = read_content(state); std::memcpy(&expected, bytes.data() + 8, sizeof(expected));
        const auto count = core_read().loads;
        button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
        wait_for([&] { return core_read().loads > count; }, "Gamepad load hotkey did not load state");
        require(core_read().loaded_value == expected, "Gamepad restored the wrong state");
        frames(8); require(core_read().loads == count + 1, "Held gamepad load hotkey repeated");
        button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false); frames(8);
        saw_fast = saw_clean_resume = saw_primed = false;
        button(SDL_GAMEPAD_BUTTON_WEST, true); wait_for([&] { return saw_fast.load(); }, "Gamepad fast-forward did not activate");
        button(SDL_GAMEPAD_BUTTON_WEST, false); wait_for([&] { return saw_primed.load(); }, "Gamepad fast-forward did not restore audio");
        require(saw_clean_resume && !bad_audio, "Gamepad fast-forward left audio queued or failed to reprime");
        SDL_CloseJoystick(joystick);
        // Leave the virtual gamepad attached until the runner exits: disconnect
        // intentionally pauses gameplay, which would change the quit navigation.
        quit_game();
    });
    observe(nullptr);
}
static std::atomic<int> observed_confirmation{-1};
static void observe_confirmation() { observed_confirmation = current->frontend.confirm_save_states ? 1 : 0; }
static void toggle_confirmation() {
    back(); down(5); enter(); down(4); enter(); back(); back();
}
static void confirmations(const fs::path& root, const std::string& core, TestCoreObserve observe) {
    observe(observe_confirmation);
    run(options(root, core, {"--no-audio"}), [&] {
        require(observed_confirmation == 1, "Confirmation did not default on in the runner");
        press(SDL_SCANCODE_F5);
        wait_for([&] { return !saved_state(root).empty(); }, "Default confirmation blocked the first save");
        frames(); toggle_confirmation(); frames();
        require(observed_confirmation == 0, "Interface toggle did not affect the running game");
        const auto boot = core_read().boots;
        configuration(); down(1); enter(); down(2); press(SDL_SCANCODE_RIGHT); back();
        restart_from_configuration();
        wait_for([&] { return core_read().boots == boot + 1; }, "Resolution restart failed"); frames();
        require(observed_confirmation == 0, "Restart lost the changed global confirmation preference");
        const auto loads = core_read().loads;
        press(SDL_SCANCODE_F7);
        wait_for([&] { return core_read().loads > loads; }, "Disabled confirmation still blocked the load hotkey"); frames();
        const auto saves = core_read().saves;
        press(SDL_SCANCODE_F5);
        wait_for([&] { return core_read().saves > saves; }, "Disabled confirmation still blocked overwrite"); frames();
        quit_game();
    });
    require(save_storage::read(root / "config/runner.conf").at("confirm_save_states") == "0", "Global confirmation toggle did not persist");
    observed_confirmation = -1;
    const auto game = root / "config/games/gc/fixture/settings.conf";
    run(options(root, core, {"--no-audio"}), [&] {
        require(observed_confirmation == 0, "Relaunch ignored global confirmation preference");
        configuration(); down(4); enter();
        wait_for([&] { return fs::exists(game); }, "Could not create game config");
        back(); down(1); enter(); down(4); enter(); back(); back(); frames();
        require(observed_confirmation == 1, "Interface toggle did not turn confirmation back on");
        quit_game();
    });
    require(save_storage::read(root / "config/runner.conf").at("confirm_save_states") == "1" &&
        read_text(game).find("confirm_save_states") == std::string::npos, "Game configuration captured a global Interface preference");
    std::ofstream(game, std::ios::app) << "confirm_save_states 0\n";
    observed_confirmation = -1;
    run(options(root, core, {"--no-audio"}), [&] {
        require(observed_confirmation == 1, "Game config overrode the global confirmation preference"); quit_game();
    });
    observe(nullptr);
}
int main(int argc, char** argv) {
    SDL_SharedObject* module = nullptr;
    try {
        require(argc == 3, "Expected synthetic core and test case");
        TestDirectory directory(std::string("cabinet-integration-") + argv[2]);
        std::ofstream(directory.path / "game.bin") << "Cabinet synthetic content";
        fs::create_directories(directory.path / "config");
        std::ofstream config(directory.path / "config/runner.conf");
        config << "pause_on_focus_loss 0\n";
        if (std::string_view(argv[2]) != "confirmations") config << "confirm_save_states 0\n";
        config.close();
        isolate_virtual_gamepads();
        module = SDL_LoadObject(argv[1]); require(module, SDL_GetError());
        core_read = reinterpret_cast<TestCoreRead>(SDL_LoadFunction(module, "cabinet_test_read"));
        auto observe = reinterpret_cast<TestCoreObserve>(SDL_LoadFunction(module, "cabinet_test_observe"));
        require(core_read && observe, "Synthetic core observations missing");
        const std::string test = argv[2];
        if (test == "dual_screen") {
            for (const auto system : {"gb", "gba", "gc", "n64", "generic"}) {
                run(options(directory.path, argv[1], {"--system", system, "--dual-screen", "--no-audio"}), [&] {
                    require(!current->dual_screen && !current->secondary && current->core_options.runtime_overrides.empty(),
                        "Unsupported system did not completely ignore dual-screen mode");
                    quit_game();
                });
            }
#ifdef CABINET_ARMADA_DRM
            allow_armada_probe = true;
#endif
            run(options(directory.path, argv[1], {"--system", "nds", "--dual-screen", "--no-audio",
                "--core-option", "cabinet_test_mode=ds"}), [&] {
                require(current->secondary && current->secondary->window && current->secondary->renderer,
                    "Non-Armada dual-screen mode lost its second SDL window");
                require(current->dual_screen, "Dual-screen flag lost in runner");
                quit_game();
            });
#ifdef CABINET_ARMADA_DRM
            require(armada_probes == 1, "Dual-screen runner must probe the Armada backend once");
            require(armada_video_configurations == 1, "Dual-screen runner must configure Armada video once");
#endif
        }
        else if (test == "runahead") runahead_settings(directory.path, argv[1]);
        else if (test == "all_options") all_core_options(directory.path, argv[1]);
        else if (test == "contentless") contentless_core(directory.path, argv[1]);
        else if (test == "inheritance") inheritance(directory.path, argv[1]);
        else if (test == "idle_menu") idle_menu(directory.path, argv[1]);
        else if (test == "timing") timing_stats(directory.path, argv[1]);
        else if (test == "frame_time") frame_time_callbacks(directory.path, argv[1]);
        else if (test == "async_audio") async_audio_callbacks(directory.path, argv[1]);
        else if (test == "stats") stats_overlay(directory.path, argv[1], observe);
        else if (test == "restart") renderer_restart(directory.path, argv[1]);
        else if (test == "hotkeys") hotkeys(directory.path, argv[1], observe);
        else if (test == "confirmations") confirmations(directory.path, argv[1], observe);
        else throw std::runtime_error("Unknown integration case");
        SDL_UnloadObject(module); std::cout << test << " runner integration passed\n"; return 0;
    } catch (const std::exception& e) {
        if (module) SDL_UnloadObject(module);
        std::cerr << e.what() << '\n'; SDL_Quit(); return 1;
    }
}
