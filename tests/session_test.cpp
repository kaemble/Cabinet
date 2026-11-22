// Exercise the private host and state operations against a real core without
// turning the launcher's internal Session into a public library API yet.
#include "../src/runner.cpp"
#include <cstring>
#include "virtual_gamepad.h"
#include <algorithm>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static uint64_t frame_hash = 0;
static void test_video(const void* pixels, unsigned width, unsigned height, size_t pitch) {
    if (!pixels) return;
    const auto* data = static_cast<const unsigned char*>(pixels);
    const auto bytes = current->format == SDL_PIXELFORMAT_XRGB8888 ? 4u : 2u;
    frame_hash = 14695981039346656037ull;
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width * bytes; ++x) {
            frame_hash ^= data[y * pitch + x]; frame_hash *= 1099511628211ull;
        }
}
int main(int argc, char** argv) {
    try {
        {
            InputRouter input;
            retro_controller_description types[]{{"Controller",RETRO_DEVICE_JOYPAD},
                {"Full Controller",RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD,9)}};
            retro_controller_info ports[]{{types,2},{nullptr,0}};
            input.register_controllers(ports,"dc");
            require(input.device() == types[1].id, "Dreamcast did not select the advertised Full Controller");
            input.register_controllers(ports,"generic");
            require(input.device() == RETRO_DEVICE_JOYPAD, "Full Controller leaked into another platform");
            ports[0].num_types = 1; input.register_controllers(ports,"dc");
            require(input.device() == RETRO_DEVICE_JOYPAD, "Dreamcast fallback failed when Full Controller absent");
            InputBindings bindings; bindings.configure("dc",{});
            for (int target : {RETRO_DEVICE_ID_JOYPAD_L,RETRO_DEVICE_ID_JOYPAD_R,RETRO_DEVICE_ID_JOYPAD_SELECT,20,21,22,23})
                require(std::any_of(bindings.rows().begin(),bindings.rows().end(),[&](const auto& row) { return row.target == target; }),
                    "Full Controller input missing from Dreamcast bindings");
        }
        {
            Playtime clock;
            clock.seconds = 120;
            clock.update(1000, true);
            clock.update(1600, false);
            clock.update(10000, true);
            clock.update(10500, false);
            require(clock.seconds == 120, "Partial seconds should be discarded when pausing");
            const auto paused_tick = clock.last_tick;
            clock.update(20000, false);
            require(clock.seconds == 120, "Paused timer advanced");
            require(clock.last_tick == paused_tick && !clock.active, "Paused timer kept updating its timestamp");
            clock.update(21000, true);
            clock.update(21500, true);
            clock.update(22000, true);
            clock.update(23000, false);
            require(clock.seconds == 122, "Whole active seconds not counted");
        }
        require(argc == 3 || argc == 4, "Expected ROM, temporary data directory, and optional core path");
        std::ifstream rom(argv[1], std::ios::binary);
        std::vector<char> content{std::istreambuf_iterator<char>(rom), {}};
        require(!content.empty(), "Test ROM cannot be read");
        fs::create_directories(argv[2]);
        isolate_virtual_gamepads();
        require(SDL_Init(0), SDL_GetError());
        {
            Session starting;
            starting.frontend.can_save = true;
            starting.core.api.serialize_size = []() -> size_t { return 0; };
            starting.frontend.open_menu();
            bool rejected = false;
            try { starting.saves.capture_state(starting.save_core()); } catch (const std::exception&) { rejected = true; }
            require(rejected && starting.frontend.paused(), "Unavailable state must fail without resuming");
            require(starting.frontend.can_save, "Temporary unavailability permanently disabled save states");
            starting.core.api.serialize_size = []() -> size_t { return 1; };
            starting.core.api.serialize = [](void* data, size_t size) {
                if (size != 1) return false;
                *static_cast<char*>(data) = 42; return true;
            };
            starting.saves.state_path = fs::path(argv[2]) / "deferred-state.state";
            execute_action(starting, Action::Save);
            require(starting.frontend.has_save && !starting.frontend.paused(), "State save could not recover after startup");
            starting.frontend.open_menu();
            starting.frontend.selected = 2;
            SDL_Event enter{}; enter.type = SDL_EVENT_KEY_DOWN; enter.key.scancode = SDL_SCANCODE_RETURN;
            starting.frontend.event(enter);
            require(starting.frontend.page == Page::Confirm, "Temporary unavailability disabled the load action");
        }
        {
            for (int mode = aspect_ratio::Auto; mode < aspect_ratio::Count; ++mode) {
                for (const auto size : {SDL_FPoint{1280, 720}, SDL_FPoint{600, 900}}) {
                    const auto rect = GamePresentation::viewport(size.x, size.y, 1.5f, mode);
                    require(rect.w <= size.x + .01f && rect.h <= size.y + .01f, "Viewport escaped window");
                    const float expected = mode == aspect_ratio::Stretch ? size.x / size.y : aspect_ratio::value(mode, 1.5f);
                    require(std::abs(rect.w / rect.h - expected) < .0001f, "Selected aspect ratio not applied");
                }
            }
        }
        {
            Session wii; current = &wii;
            const retro_controller_description types[]{
                {"WiiMote + Nunchuk", 769}, {"WiiMote (sideways)", 513},
                {"WiiMote + Classic Controller", 1025}, {"GameCube Controller", 1}};
            const retro_controller_info ports[]{{types, 4}, {nullptr, 0}};
            wii.frontend.bindings.configure("wii", {});
            environment(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, const_cast<retro_controller_info*>(ports));
            for (int i = 0; i < 4; ++i) {
                wii.frontend.wii_controller = i;
                require(wii.input.wii_device(wii.frontend.wii_controller) == types[i].id, "Wrong Wii controller device selected");
                static std::vector<std::pair<unsigned,unsigned>> connections;
                connections.clear();
                wii.core.api.set_controller_port_device = [](unsigned port, unsigned device) { connections.emplace_back(port,device); };
                wii.input.connect_wii_controller(wii.frontend.wii_controller, wii.core.api.set_controller_port_device);
                require(connections.size() == 9, "Wii switch did not disconnect old controllers");
                for (unsigned p=0;p<8;++p) require(connections[p] == std::pair{p, unsigned(RETRO_DEVICE_NONE)}, "Controller left connected during switch");
                require(connections.back() == std::pair{0u,types[i].id}, "Selected controller not connected exclusively to player one");

            }
            current = nullptr;
        }
        {
            InputRouter input;
            const std::map<std::string,std::string> options;
            GameInputContext context{options,"3ds",false};
            SDL_FRect viewport{0,0,400,480};
            input.update_pointer(context,200,360,true,true,viewport);
            require(input.pointer().pressed && input.pointer().x == 0 && input.pointer().y == 16384,
                "3DS pointer was not mapped over the composite framebuffer");
            input.update_pointer(context,500,600,true,true,viewport);
            require(input.pointer().pressed && input.pointer().x == 32767 && input.pointer().y == 32767,
                "3DS dragging stopped outside the viewport");
            input.reset(context);
            input.update_pointer(context,200,360,true,true,viewport);
            require(!input.pointer().pressed, "3DS pointer clicked through a menu transition");
        }
        {
            Session touch; current = &touch;
            retro_variable variables[]{{"melonds_touch_mode", "Touch mode; auto|touch|joystick"},
                {"melonds_screen_layout1", "Layout; top-bottom|left-right"}, {nullptr, nullptr}};
            environment(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
            require(touch.core_options.values.at("melonds_touch_mode") == "touch", "DS absolute touch not enabled");
            touch.core_options.values["melonds_touch_mode"] = "joystick";
            environment(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
            require(touch.core_options.values.at("melonds_touch_mode") == "joystick", "Explicit touch mode overridden");
            // The default DS image is 256x384. Its bottom-screen centre maps
            // to (128, 96) after the core subtracts the top screen's 192 rows.
            const auto viewport = GamePresentation::viewport(1280, 720, 256.0f / 384, aspect_ratio::Auto);
            touch.input.update_pointer(touch.input_context(), 640, 540, true, true, viewport);
            require(input_state(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED), "Click not held");
            const auto ds_x = (int(touch.input.pointer().x) + 32767) * 256 / 65534;
            const auto ds_y = (int(touch.input.pointer().y) + 32767) * 384 / 65534 - 192;
            require(ds_x == 128 && ds_y == 96, "Bottom-screen coordinates incorrect");
            touch.input.update_pointer(touch.input_context(), 760, 630, true, true, viewport);
            require(touch.input.pointer().pressed && touch.input.pointer().x > 0 && touch.input.pointer().y > 16384, "Dragging not tracked");
            touch.input.update_pointer(touch.input_context(), 100, 540, true, true, viewport);
            require(touch.input.pointer().pressed && touch.input.pointer().x == -32767, "Drag did not clamp to left edge");
            touch.input.update_pointer(touch.input_context(), 1500, 1000, true, true, viewport);
            require(touch.input.pointer().pressed && touch.input.pointer().x == 32766 && touch.input.pointer().y == 32766, "Drag did not clamp to bottom-right corner");
            touch.input.update_pointer(touch.input_context(), 640, 100, true, true, viewport);
            require(touch.input.pointer().pressed && touch.input.pointer().y == 0, "Drag into top screen did not clamp to touchscreen top");
            touch.input.update_pointer(touch.input_context(), 640, 540, false, true, viewport);
            require(!touch.input.pointer().pressed, "Mouse release not reported");
            touch.input.update_pointer(touch.input_context(), 100, 540, true, true, viewport);
            require(!touch.input.pointer().pressed, "Click starting in black bar captured a drag");
            touch.input.update_pointer(touch.input_context(), 640, 540, true, true, viewport);
            require(!touch.input.pointer().pressed, "Outside click dragged into touchscreen activated it");
            touch.input.update_pointer(touch.input_context(), 640, 100, false, true, viewport);
            touch.input.update_pointer(touch.input_context(), 640, 100, true, true, viewport);
            require(!touch.input.pointer().pressed, "Click starting on top screen captured a drag");
            touch.input.update_pointer(touch.input_context(), 640, 540, true, false, viewport);
            require(!touch.input.pointer().pressed, "Unfocused mouse reached core");
            touch.frontend.open_menu();
            touch.input.update_pointer(touch.input_context(), 640, 540, true, true, viewport);
            require(!touch.input.pointer().pressed, "Menu click reached core");
            touch.frontend.resume_with_message("");
            touch.input.update_pointer(touch.input_context(), 300, 600, false, true, viewport);
            touch.input.update_pointer(touch.input_context(), 300, 600, true, true, GamePresentation::viewport(600, 800, 256.0f / 384, aspect_ratio::Stretch));
            require(touch.input.pointer().x == 0 && touch.input.pointer().y == 16384, "Stretched portrait coordinates incorrect");
            touch.core_options.values["melonds_screen_layout1"] = "left-right";
            touch.input.update_pointer(touch.input_context(), 900, 360, false, true, GamePresentation::viewport(1280, 720, 512.0f / 192, aspect_ratio::Auto));
            touch.input.update_pointer(touch.input_context(), 900, 360, true, true, GamePresentation::viewport(1280, 720, 512.0f / 192, aspect_ratio::Auto));
            touch.input.update_pointer(touch.input_context(), -20, 360, true, true, GamePresentation::viewport(1280, 720, 512.0f / 192, aspect_ratio::Auto));
            require(touch.input.pointer().pressed && touch.input.pointer().x == 0, "Side-by-side drag did not clamp to right screen");
            touch.core_options.values["melonds_screen_layout1"] = "top-bottom";
            touch.core_options.values["melonds_screen_gap"] = "64";
            touch.input.update_pointer(touch.input_context(), 640, 600, false, true, viewport);
            touch.input.update_pointer(touch.input_context(), 640, 600, true, true, viewport);
            touch.input.update_pointer(touch.input_context(), 640, 0, true, true, viewport);
            require(touch.input.pointer().pressed && (int(touch.input.pointer().y) + 32767) * 448 / 65534 == 256, "Screen gap clamping incorrect");
            touch.core_options.values["melonds_screen_layout2"] = "bottom-top";
            touch.core_options.values["melonds_number_of_screen_layouts"] = "2";
            InputSnapshot swap;
            swap.buttons[RETRO_DEVICE_ID_JOYPAD_R3] = 1;
            touch.input.begin_poll(swap);
            touch.input.update_screen_swap(touch.core_options.values);
            require(touch.input.touch_region(touch.input_context()).y == 0, "Swapped touchscreen region incorrect");
            require(!input_state(0, RETRO_DEVICE_POINTER, 1, RETRO_DEVICE_ID_POINTER_PRESSED), "Phantom second pointer");
            current = nullptr;
        }
        {
            const auto dir = fs::path(argv[2]) / "save-layout-test";
            fs::remove_all(dir);
            fs::create_directories(dir);
            require(save_storage::folder_name("Super Mario World 2 - Yoshi's Island") == "Super Mario World 2 - Yoshi's Island", "Readable save name mangled");
            require(save_storage::folder_name("../Bad:Name") == ".._Bad_Name", "Unsafe filename not sanitized");
            require(save_storage::folder_name(std::string(159, 'a') + ".suffix") == std::string(159, 'a'), "Truncation left an unsafe trailing dot");
            const save_storage::Metadata values{{"game_id", "stable-id"}, {"title", "A \"quoted\" game"}, {"core_hash", "1234"}};
            save_storage::write(dir / "cabinet-game.txt", values);
            require(save_storage::read(dir / "cabinet-game.txt") == values, "Save manifest roundtrip failed");
            std::ofstream(dir / "Old.A1.bin") << "memory card";
            save_storage::relocate_native_saves(dir, "Old", "New", "flycast_libretro");
            require(fs::exists(dir / "Old.A1.bin") && fs::file_size(dir / "New.A1.bin") == 11, "Native card relocation failed");
            std::ofstream(dir / "New.A1.bin") << "newer save";
            save_storage::relocate_native_saves(dir, "Old", "New", "flycast_libretro");
            require(fs::file_size(dir / "New.A1.bin") == 10, "Relocation overwrote destination save");
        }
        {
            Session audio; current = &audio;
            SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 44100};
            audio.audio = AudioOutput(SDL_CreateAudioStream(&spec, &spec));
            require(static_cast<bool>(audio.audio), SDL_GetError());
            require(audio.audio.ready(audio.frame_rate, audio.fast_forward), "Empty audio queue blocks emulation");
            std::vector<int16_t> samples(60000 * 2, 123);
            require(audio_batch(samples.data(), 60000) == 60000, "Audio batch not accepted");
            require(!audio.audio.ready(audio.frame_rate, audio.fast_forward), "Full audio queue does not pace emulation");
            require(audio_batch(samples.data(), 60000) == 60000 &&
                audio.audio.queued_bytes() == 480000, "Audio batch silently discarded");
            audio.audio.reset(false);
            require(audio.audio.ready(audio.frame_rate, audio.fast_forward), "Cleared audio queue does not resume emulation");
            bool fast = true;
            require(environment(RETRO_ENVIRONMENT_GET_FASTFORWARDING, &fast) && !fast,
                    "Normal play reported fast-forwarding");
            audio_batch(samples.data(), 60000);
            set_fast_forward(audio, true);
            retro_throttle_state throttle{};
            require(environment(RETRO_ENVIRONMENT_GET_FASTFORWARDING, &fast) && fast &&
                    environment(RETRO_ENVIRONMENT_GET_THROTTLE_STATE, &throttle) &&
                    throttle.mode == RETRO_THROTTLE_FAST_FORWARD && throttle.rate == 0,
                    "Uncapped state was not reported to the core");
            require(audio.audio.ready(audio.frame_rate, audio.fast_forward) && audio.audio.queued_bytes() == 0,
                    "Entering fast-forward retained queued audio");
            require(audio_batch(samples.data(), 60000) == 60000 && audio.audio.queued_bytes() == 0,
                    "Fast-forward queued audio or rejected the batch");
            set_fast_forward(audio, false);
            require(!audio.audio.started() && !audio.audio.throttled(),
                    "Leaving fast-forward did not reset audio priming");
            audio_batch(samples.data(), 60000);
            require(audio.audio.queued_bytes() > 0 && !audio.audio.ready(audio.frame_rate, audio.fast_forward),
                    "Normal audio pacing did not recover after fast-forward");
            current = nullptr;
        }
        {
            Session flycast; current = &flycast;
            retro_variable variables[]{
                {"reicast_internal_resolution", "Internal resolution; 640x480|1280x960"},
                {"reicast_per_content_vmus", "Per-game VMUs; disabled|VMU A1|All VMUs"},
                {nullptr, nullptr}};
            require(environment(RETRO_ENVIRONMENT_SET_VARIABLES, variables), "Flycast options rejected");
            require(flycast.frontend.resolution_needs_restart && flycast.frontend.resolution_values.size() == 2,
                "Flycast resolution menu not configured");
            require(flycast.core_options.values.at("reicast_per_content_vmus") == "All VMUs", "Flycast cards default to shared system folder");
            flycast.core_options.values["reicast_per_content_vmus"] = "VMU A1";
            environment(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
            require(flycast.core_options.values.at("reicast_per_content_vmus") == "VMU A1", "Flycast explicit option overridden");
            current = nullptr;
        }
        {
            Session profile;
            profile.frontend.vsync = false; profile.frontend.vrr = true;
            profile.frontend.rumble_enabled = false;
            profile.core_options.values["dolphin_main_cpu_thread"] = "disabled";
            retro_variable profile_vars[]{{"dolphin_main_cpu_thread", "Dual core; enabled|disabled"}, {nullptr, nullptr}};
            profile.core_options.register_variables(profile_vars, profile.frontend);
            profile.frontend.wii_controller = 2;
            profile.frontend.scaling = 2; profile.frontend.aspect_mode = aspect_ratio::Standard; profile.frontend.volume = 30;
            profile.core_options.resolution_key = "ppsspp_internal_resolution";
            profile.frontend.resolution_values = {"480x272", "960x544"};
            profile.frontend.resolution_index = 1;
            const auto directory = fs::path(argv[2]) / "game-profile-test";
            settings_store::write_game(directory, settings_snapshot(profile));
            // Older profiles may still contain Interface settings.
            std::ofstream(directory / "settings.conf", std::ios::app)
                << "pause_on_focus_loss 0\nhide_cursor 0\nhome_shortcut 1\nstick_shortcut 1\nswap_confirm_cancel 1\nconfirm_save_states 0\n";
            RunnerOptions restored;
            settings_store::read_game(directory / "settings.conf", restored);
            require(!restored.rumble, "Game rumble setting did not persist");
            require(!restored.vsync && restored.vrr && restored.core_options.at("dolphin_main_cpu_thread") == "disabled",
                    "Advanced game settings did not persist");
            require(restored.wii_controller == 2, "Per-game Wii controller not restored");
            require(restored.confirm_save_states && restored.pause_on_focus_loss && restored.hide_cursor && !restored.home_shortcut &&
                    !restored.stick_shortcut && !restored.swap_confirm_cancel, "Game profile overrode global Interface settings");
            require(restored.aspect_mode == aspect_ratio::Standard && restored.scaling == 2 && restored.volume == 30 && restored.core_options.at(profile.core_options.resolution_key) == "960x544", "Game settings roundtrip failed");
            restored.scaling = false; restored.aspect_mode = aspect_ratio::Auto; restored.volume = 80;
            restored.explicit_flags = {"--scale", "--volume", "--keep-aspect"};
            restored.explicit_core_options.insert(profile.core_options.resolution_key);
            restored.core_options[profile.core_options.resolution_key] = "480x272";
            settings_store::read_game(directory / "settings.conf", restored);
            require(restored.aspect_mode == aspect_ratio::Auto && restored.scaling == 0 && restored.volume == 80 && restored.core_options.at(profile.core_options.resolution_key) == "480x272", "Game settings overrode CLI");
        }
        {
            Session dolphin; current = &dolphin;
            retro_variable vars[]{{"dolphin_main_cpu_thread", "Dual core; enabled|disabled"},
                                  {"dolphin_immediate_xfb", "Immediate XFB; disabled|enabled"},
                                  {"dolphin_efb_scale", "Internal resolution; 1|2|3"}, {nullptr,nullptr}};
            environment(RETRO_ENVIRONMENT_SET_VARIABLES, vars);
            require(dolphin.frontend.all_core_options.size() == 3 && dolphin.frontend.core_choices.empty(),
                    "Dolphin options must remain in the full catalog without dedicated Advanced entries");
            require(dolphin.core_options.resolution_key == "dolphin_efb_scale" && dolphin.frontend.resolution_needs_restart &&
                dolphin.frontend.resolution_values == std::vector<std::string>{"1", "2", "3"},
                "Dolphin resolution must use the full core restart path");
            dolphin.core_options.boot_values = dolphin.core_options.values;
            const auto boot_value = dolphin.core_options.values.at("dolphin_main_cpu_thread");
            dolphin.core_options.set_value({"dolphin_main_cpu_thread", boot_value == "enabled" ? "disabled" : "enabled"}, dolphin.frontend);
            dolphin.core_options.sync_pending(dolphin.frontend);
            require(dolphin.core_options.values.at("dolphin_main_cpu_thread") != boot_value &&
                    dolphin.core_options.boot_values.at("dolphin_main_cpu_thread") == boot_value,
                    "Editing Dual core must preserve the recorded boot configuration");
            dolphin.core_options.take_update();
            dolphin.core_options.set_value({"dolphin_immediate_xfb", "enabled"}, dolphin.frontend);
            dolphin.core_options.sync_pending(dolphin.frontend);
            require(dolphin.core_options.changed && dolphin.core_options.values.at("dolphin_immediate_xfb") == "enabled", "Immediate XFB did not update live");
            dolphin.core_options.set_value({"dolphin_main_cpu_thread", boot_value.c_str()}, dolphin.frontend);
            dolphin.core_options.sync_pending(dolphin.frontend);
            require(dolphin.core_options.values.at("dolphin_main_cpu_thread") == boot_value,
                    "Restoring Dual core did not restore its value");
            current = nullptr;
        }
        {
            Session mupen; current = &mupen;
            retro_environment_t clear_waits = nullptr;
            require(environment(0x800003, &clear_waits) && clear_waits && clear_waits(1, nullptr) && clear_waits(0, nullptr),
                "Threaded GLideN64 shutdown callback missing");
            retro_variable vars[]{{"mupen64plus-rdp-plugin", "RDP; gliden64|parallel"},
                {"mupen64plus-ThreadedRenderer", "Threading; False|True"},
                {"mupen64plus-BilinearMode", "Filtering; standard|3point"},
                {"mupen64plus-rsp-plugin", "RSP; hle|parallel"},
                {"mupen64plus-43screensize", "Resolution; 640x480|320x240"},
                {"mupen64plus-parallel-rdp-upscaling", "Upscaling; 1x|2x|4x"}, {nullptr,nullptr}};
            environment(RETRO_ENVIRONMENT_SET_VARIABLES, vars);
            auto& ui = mupen.frontend;
            require(ui.core_choices.size() == 4 && ui.visible_core_choices().size() == 4, "Mupen Advanced settings missing");
            require(ui.core_choices[1].key == "mupen64plus-rsp-plugin", "Mupen selectors are not ordered first");
            require(mupen.core_options.values.at("mupen64plus-BilinearMode") == "3point" && mupen.core_options.defaults.at("mupen64plus-BilinearMode") == "3point",
                "GLideN64 did not default to N64 3-point filtering");
            require(mupen.core_options.resolution_key == "mupen64plus-43screensize" && ui.resolution_values[ui.resolution_index] == "640x480", "GLideN64 resolution lost");
            mupen.core_options.boot_values = mupen.core_options.values;
            ui.cycle_core_choice(2,1); ui.cycle_core_choice(3,1); ui.cycle_core_choice(3,1);
            mupen.core_options.sync_choices(mupen.frontend);
            require(!mupen.core_options.changed && mupen.core_options.values.at("mupen64plus-ThreadedRenderer") == "True" &&
                mupen.core_options.values.at("mupen64plus-BilinearMode") == "3point", "GLideN64 options were not staged for restart");
            ui.cycle_core_choice(0,1);
            mupen.core_options.sync_choices(mupen.frontend);
            require(ui.visible_core_choices().size() == 2 && mupen.core_options.values.at("mupen64plus-rsp-plugin") == "parallel", "ParaLLEl left GLideN64 options or HLE active");
            ui.cycle_core_choice(1,-1);
            require(ui.core_choices[1].values[ui.core_choices[1].index] == "parallel", "ParaLLEl allowed an incompatible HLE RSP");
            require(mupen.core_options.resolution_key == "mupen64plus-parallel-rdp-upscaling" && ui.resolution_values[0] == "1x", "ParaLLEl resolution was not selected");
            const auto directory = fs::path(argv[2]) / "mupen-profile-test";
            settings_store::write_game(directory, settings_snapshot(mupen));
            RunnerOptions restored; settings_store::read_game(directory / "settings.conf",restored);
            require(restored.core_options.at("mupen64plus-rdp-plugin") == "parallel" && restored.core_options.at("mupen64plus-BilinearMode") == "3point", "Mupen settings did not persist");
            require(restored.core_options.at("mupen64plus-43screensize") == "640x480" && restored.core_options.at("mupen64plus-parallel-rdp-upscaling") == "1x", "Renderer resolution preferences did not persist");
            ui.cycle_core_choice(0,-1);
            mupen.core_options.sync_choices(mupen.frontend);
            require(ui.visible_core_choices().size() == 4 && mupen.core_options.resolution_key == "mupen64plus-43screensize", "Returning to GLideN64 did not restore its settings");
            current = nullptr;
        }
        {
            retro_variable vars[]{{"mupen64plus-BilinearMode", "Filtering; standard|3point"},
                {"mupen64plus-pak1", "Player 1 Pak; memory|none|rumble|transfer"},
                {"dolphin_main_cpu_thread", "Dual core; enabled|disabled"}, {nullptr,nullptr}};
            Session defaults; current = &defaults;
            environment(RETRO_ENVIRONMENT_SET_VARIABLES, vars);
            require(defaults.core_options.values.at("mupen64plus-pak1") == "rumble" && defaults.core_options.defaults.at("mupen64plus-pak1") == "rumble",
                "Mupen Player 1 did not default to a Rumble Pak");
#if defined(__aarch64__)
            const std::string dual_core_default = "enabled";
#else
            const std::string dual_core_default = "disabled";
#endif
            require(defaults.core_options.values.at("dolphin_main_cpu_thread") == dual_core_default &&
                defaults.core_options.defaults.at("dolphin_main_cpu_thread") == dual_core_default,
                "Dolphin architecture-specific default was not applied");
            Session overridden; current = &overridden;
            overridden.core_options.values = {{"mupen64plus-BilinearMode", "standard"}, {"mupen64plus-pak1", "memory"}, {"dolphin_main_cpu_thread", "disabled"}};
            environment(RETRO_ENVIRONMENT_SET_VARIABLES, vars);
            require(overridden.core_options.values.at("mupen64plus-BilinearMode") == "standard" &&
                overridden.core_options.values.at("mupen64plus-pak1") == "memory" &&
                overridden.core_options.values.at("dolphin_main_cpu_thread") == "disabled", "Core defaults replaced explicit settings");
            overridden.core_options.values["dolphin_main_cpu_thread"] = "enabled";
            environment(RETRO_ENVIRONMENT_SET_VARIABLES, vars);
            require(overridden.core_options.values.at("dolphin_main_cpu_thread") == "enabled",
                "Dolphin architecture default replaced explicitly enabled dual core");
            current = nullptr;
        }
        {
            Session s;
            current = &s;
            s.system_dir = argv[2]; s.saves.directory = argv[2];
            s.saves.state_path = fs::path(argv[2]) / "roundtrip.state";
            s.core.open(argc == 4 ? argv[3] : CABINET_CORE_PATH);
            s.core.api.set_environment(environment); s.core.api.set_video_refresh(test_video);
            s.core.api.set_audio_sample(audio_sample); s.core.api.set_audio_sample_batch(audio_batch);
            s.core.api.set_input_poll(input_poll); s.core.api.set_input_state(input_state);
            s.frontend.bindings.configure("psx",{});
            const retro_controller_description types[]{{"Digital Controller (Gamepad)",RETRO_DEVICE_JOYPAD},
                {"Analog Controller (DualShock)",RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG,1)}};
            retro_controller_info port{types,2};
            require(environment(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO,&port) && s.input.device() == types[1].id,
                "PlayStation must select the advertised DualShock device");
            s.frontend.bindings.configure("gb",{});
            environment(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO,&port);
            require(s.input.device() == RETRO_DEVICE_JOYPAD,"Digital system selected an analog device");
            s.core.initialize();
            retro_game_info game{argv[1], content.data(), content.size(), nullptr};
            require(s.core.load_game(&game), "Core failed to load test ROM");
            s.saves.battery_path = fs::path(argv[2]) / "battery-roundtrip";
            auto* ram = static_cast<unsigned char*>(s.core.api.get_memory_data(RETRO_MEMORY_SAVE_RAM));
            auto ram_size = s.core.api.get_memory_size(RETRO_MEMORY_SAVE_RAM);
            require(ram && ram_size, "Test ROM must expose battery RAM");
            {
                const auto shorter_size = ram_size / 2;
                require(shorter_size > 0, "Test SRAM must allow a smaller save fixture");
                std::ofstream partial(s.saves.battery_path.string() + ".srm", std::ios::binary | std::ios::trunc);
                const std::string bytes(shorter_size, '\x3c');
                partial.write(bytes.data(), bytes.size()); partial.close();
                std::memset(ram, 0xff, ram_size);
                s.saves.battery(s.save_core(), true);
                require(std::all_of(ram, ram + shorter_size, [](unsigned char v) { return v == 0x3c; }),
                        "Smaller SRAM save was not loaded");
                require(std::all_of(ram + shorter_size, ram + ram_size, [](unsigned char v) { return v == 0xff; }),
                        "Loading smaller SRAM changed the core's initialized remainder");
            }
            std::memset(ram, 0x5a, ram_size);
            s.saves.battery(s.save_core(), false);
            std::memset(ram, 0, ram_size);
            s.saves.battery(s.save_core(), true);
            require(std::all_of(ram, ram + ram_size, [](unsigned char v) { return v == 0x5a; }), "Battery RAM must reload from disk");
            s.saves.battery_ready = false;
            for (int i = 0; i < 90; ++i) s.core.api.run();
            s.frontend.open_menu();
            execute_action(s, Action::Save);
            require(!s.frontend.paused(), "Successful save must automatically resume");
            require(fs::exists(s.saves.state_path), "Save must exist on disk");
            require(s.frontend.has_save, "Save must enable load action");
            for (int i = 0; i < 30; ++i) s.core.api.run();
            uint64_t expected = frame_hash;
            s.frontend.open_menu();
            execute_action(s, Action::Load);
            require(!s.frontend.paused(), "Successful load must automatically resume");
            for (int i = 0; i < 30; ++i) s.core.api.run();
            require(frame_hash == expected, "Loading state must reproduce the same subsequent video");
            s.saves.metadata = {{"game_id", "test-game"}, {"core_hash", "core-build-a"}};
            save_state(s);
            require(save_storage::read(s.saves.state_path.string() + ".txt").at("core_hash") == "core-build-a", "State binary hash metadata missing");
            s.saves.metadata["core_hash"] = "core-build-b";
            bool wrong_core_rejected = false;
            try { load_state(s); } catch (const std::exception&) { wrong_core_rejected = true; }
            require(wrong_core_rejected, "State from a different binary was accepted");
            s.saves.metadata["core_hash"] = "core-build-a";
            require(!fs::exists(s.saves.state_path.string() + ".tmp"), "Successful save must not leave temporary file");
            s.frontend.open_menu();
            auto before = s.saves.capture_state(s.save_core());
            {
                std::ofstream bad(s.saves.state_path, std::ios::binary | std::ios::trunc);
                bad << "BADSTATEbroken";
            }
            bool rejected = false;
            try { execute_action(s, Action::Load); } catch (const std::exception&) { rejected = true; }
            require(rejected, "Invalid save header must be rejected");
            require(s.frontend.paused(), "Failed load must keep the game paused");
            // Some cores serialize unused fields without initializing them.
            // Compare subsequent emulation instead of opaque state bytes.
            for (int i = 0; i < 30; ++i) s.core.api.run();
            const auto after_invalid = frame_hash;
            require(s.core.api.unserialize(before.data(), before.size()), "Cannot restore invalid-load test baseline");
            for (int i = 0; i < 30; ++i) s.core.api.run();
            require(frame_hash == after_invalid, "Invalid save changed subsequent emulation");
            auto original_path = s.saves.state_path;
            s.saves.state_path = original_path / "invalid-parent.state";
            rejected = false;
            try { execute_action(s, Action::Save); } catch (const std::exception&) { rejected = true; }
            require(rejected && s.frontend.paused(), "Failed save must keep the game paused");
            s.saves.state_path = original_path;
            save_state(s); // Replacing an existing file must also succeed.
            load_state(s);
            for (int i = 0; i < 30; ++i) s.core.api.run();
            require(s.callback_error.empty(), "Core reported an error");
        }
        SDL_Quit();
        std::cout << "Real-core save/load roundtrip and invalid-state checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; SDL_Quit(); return 1;
    }
}
