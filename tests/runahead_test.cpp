#include "../src/runner.cpp"
#include <cstring>

static std::uint64_t value = 0;
static unsigned runs = 0, saves = 0, loads = 0, displayed = 0;
static bool unavailable = false, fail_save = false, fail_load = false, duplicate = false;

static void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

static void press(Frontend& ui, SDL_Scancode scancode)
{
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = scancode;
    ui.event(event);
    event.type = SDL_EVENT_KEY_UP;
    ui.event(event);
}

int main()
{
    try
    {
        require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO), SDL_GetError());
        {
            Session session;
            current = &session;
            session.window = SDL_CreateWindow("Run-ahead test", 64, 64, SDL_WINDOW_HIDDEN);
            require(session.window, SDL_GetError());
            session.renderer = SDL_CreateRenderer(session.window, nullptr);
            require(session.renderer, SDL_GetError());
            session.format = SDL_PIXELFORMAT_XRGB8888;
            SDL_AudioSpec format{SDL_AUDIO_S16, 2, 48000};
            session.audio = AudioOutput(SDL_CreateAudioStream(&format, &format));
            require(static_cast<bool>(session.audio), SDL_GetError());
            session.frontend.runahead_available = true;
            session.core.api.serialize_size = []() -> std::size_t
            {
                require(current->runahead.state_operation, "State sizing lost run-ahead context");
                return unavailable ? 0 : sizeof(value);
            };
            session.core.api.serialize = [](void* data, std::size_t size)
            {
                require(current->runahead.state_operation && size == sizeof(value), "Wrong snapshot context or size");
                ++saves;
                std::memcpy(data, &value, size);
                return !fail_save;
            };
            session.core.api.unserialize = [](const void* data, std::size_t size)
            {
                require(current->runahead.state_operation, "Rollback lost run-ahead context");
                ++loads;
                if (fail_load)
                {
                    return false;
                }
                std::memcpy(&value, data, size);
                // A core may redraw and emit audio while loading a state.
                const std::uint32_t pixel = 0;
                video(&pixel, 1, 1, sizeof(pixel));
                const std::int16_t samples[]{0, 0};
                audio_batch(samples, 1);
                return true;
            };
            session.core.api.run = []
            {
                ++runs;
                ++value;
                input_poll();
                const auto input = input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
                if (current->speculative)
                {
                    require(input == 0, "Speculative execution polled changed input");
                    retro_variable option{"fixture", "changed"};
                    environment(RETRO_ENVIRONMENT_SET_VARIABLE, &option);
                    require(current->core_options.values.at("fixture") == "retained", "Speculative option escaped");
                }
                else
                {
                    // Simulate a changed input source after the retained poll.
                    InputSnapshot changed;
                    changed.buttons[RETRO_DEVICE_ID_JOYPAD_A] = 32767;
                    current->input.begin_poll(changed);
                }
                const std::uint32_t pixel = static_cast<std::uint32_t>(value);
                video(duplicate && current->speculative ? nullptr : &pixel, 1, 1, sizeof(pixel));
                const std::int16_t samples[]{0, 0};
                require(audio_batch(samples, 1) == 1, "Muted audio was not consumed");
                if (current->output_enabled)
                {
                    displayed = pixel;
                }
            };
            session.core_options.values["fixture"] = "retained";
            auto blue_pixel = [&]
            {
                require(SDL_RenderTexture(session.renderer, session.texture, nullptr, nullptr), SDL_GetError());
                auto* pixels = SDL_RenderReadPixels(session.renderer, nullptr);
                require(pixels, SDL_GetError());
                std::uint8_t red, green, blue, alpha;
                const bool read = SDL_ReadSurfacePixel(pixels, 0, 0, &red, &green, &blue, &alpha);
                SDL_DestroySurface(pixels);
                require(read, SDL_GetError());
                return blue;
            };
            for (int ahead = 0; ahead <= 3; ++ahead)
            {
                value = runs = saves = loads = displayed = 0;
                session.frames = 0;
                session.frontend.runahead_frames = ahead;
                const auto audio_before = session.audio.produced_ms();
                for (unsigned frame = 0; frame < 10; ++frame)
                {
                    run_core_frame(session, true);
                }
                require(value == 10, "Speculation changed retained progression");
                require(runs == 10u * (ahead + 1), "Wrong speculative frame count");
                require(displayed == 10u + ahead && blue_pixel() == 10u + ahead,
                        "Wrong predicted frame selected or rollback overwrote video");
                require(saves == (ahead ? 10u : 0u) && loads == saves, "Wrong snapshot/rollback count");
                require(session.frames == 10, "Speculative frames inflated video statistics");
                require(std::abs(session.audio.produced_ms() - audio_before - 10000.0 / 44100) < .0001,
                        "Speculative audio leaked or retained audio was dropped");
                require(!session.speculative && session.output_enabled && !session.runahead.state_operation,
                        "Execution flags escaped the frame");
            }
            session.frontend.runahead_available = false;
            const auto before_unsupported = runs;
            run_core_frame(session, true);
            require(runs == before_unsupported + 1, "Unsupported metadata allowed speculative execution");
            session.frontend.runahead_available = true;
            session.serialization_quirks = RETRO_SERIALIZATION_QUIRK_INCOMPLETE;
            const auto before_quirk = runs;
            run_core_frame(session, true);
            require(runs == before_quirk + 1, "Incomplete states allowed run-ahead");
            session.serialization_quirks = 0;
            session.frontend.runahead_option_key = "bsnes_run_ahead_frames";
            const auto before_alias = runs;
            run_core_frame(session, true);
            require(runs == before_alias + 1, "Native alias stacked frontend run-ahead");
            session.frontend.runahead_option_key.clear();
            duplicate = true;
            const auto before = value;
            run_core_frame(session, true);
            require(value == before + 1 && blue_pixel() == before + 1,
                    "Duplicate prediction lost the retained image");
            duplicate = false;
            session.fast_forward = true;
            const auto before_fast = runs;
            run_core_frame(session, true);
            require(runs == before_fast + 1, "Fast-forward still ran speculative frames");
            session.fast_forward = false;
            session.runahead = RunAhead{};
            unavailable = true;
            const auto before_startup = runs;
            run_core_frame(session, true);
            require(runs == before_startup + 1 && !session.runahead.failed, "Unavailable startup state was fatal");
            unavailable = false;
            fail_save = true;
            const auto before_failed_save = value;
            run_core_frame(session, true);
            require(value == before_failed_save + 1 && session.frontend.runahead_frames == 0,
                    "Failed save did not safely disable run-ahead");
            fail_save = false;
            run_core_frame(session, true);
            session.frontend.runahead_frames = 1;
            fail_load = true;
            session.saves.battery_ready = true;
            bool rejected = false;
            try
            {
                run_core_frame(session, true);
            }
            catch (const StateRecoveryError&)
            {
                rejected = true;
            }
            require(rejected && !session.saves.battery_ready, "Failed rollback could persist speculative saves");
            require(!session.speculative && session.output_enabled, "Failed rollback leaked callback modes");
        }
        current = nullptr;
        for (const auto name : {"gambatte", "mgba", "fceumm", "mednafen_pce", "mednafen_saturn"})
        {
            require(deterministic_core(std::string(name) + "_libretro.so"), "Deterministic metadata missing");
        }
        for (const auto name : {"bsnes", "swanstation", "blastem", "melondsds", "dolphin", "custom"})
        {
            require(!deterministic_core(std::string(name) + "_libretro.so"), "Unsupported core enabled frontend run-ahead");
        }
        Frontend ui;
        ui.runahead_available = true;
        ui.page = Page::Advanced;
        ui.selected = 2;
        require(ui.runahead_frames == 0, "Run-ahead must default to off");
        for (int expected : {1, 2, 3, 0})
        {
            press(ui, SDL_SCANCODE_RIGHT);
            require(ui.runahead_frames == expected, "Run-ahead menu did not cycle off through three frames");
        }
        press(ui, SDL_SCANCODE_LEFT);
        require(ui.runahead_frames == 3, "Run-ahead reverse cycling failed");
        press(ui, SDL_SCANCODE_DOWN);
        press(ui, SDL_SCANCODE_RETURN);
        require(ui.page == Page::CoreOptions, "Run-ahead displaced More Core options incorrectly");
        press(ui, SDL_SCANCODE_ESCAPE);
        require(ui.page == Page::Advanced && ui.selected == 3, "Back lost the Advanced selection");
        SDL_Quit();
        std::cout << "Run-ahead progression, callbacks, failures, eligibility and navigation passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
