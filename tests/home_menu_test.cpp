#include "../src/home.cpp"
#include "virtual_gamepad.h"
#include <fstream>
#include <sstream>

static void require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

static void key(Home& home, SDL_Scancode code)
{
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = code;
    home.event(event);
}

static void settle(Home& home)
{
    for (int i = 0; i < 100; ++i)
    {
        home.tick();
        SDL_Delay(1);
    }
}

static bool rejects_custom_at_last_tab(Home& home, const fs::path& file)
{
    for (std::size_t i = 0; i < systems().size() + 3; ++i)
    {
        key(home, SDL_SCANCODE_DOWN);
    }
    try
    {
        home.import_file(file);
    }
    catch (const std::exception&)
    {
        return true;
    }
    return false;
}

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "Expected scratch directory");
        const auto directory = fs::absolute(argv[1]) / ("home-menu-" + std::to_string(SDL_GetPerformanceCounter()));
        const auto profile = directory / "profile", content = directory / "content", cores = directory / "cores";
        fs::create_directories(content);
        fs::create_directories(cores);
        const auto file = content / "Custom.unknown", core = cores / "custom-core.so";
        std::ofstream(file) << "Custom content";
        std::ofstream(core) << "Core chooser fixture";
        save_storage::write(profile / "browser.conf", {{"rom", content.string()}, {"core", cores.string()}});
        isolate_virtual_gamepads();
        require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), SDL_GetError());
        auto* window = SDL_CreateWindow("Home menu test", 1200, 800, 0);
        require(window != nullptr, SDL_GetError());
        auto* renderer = SDL_CreateRenderer(window, nullptr);
        require(renderer != nullptr, SDL_GetError());
        {
            std::ostringstream diagnostics;
            Home home(window, renderer, profile, "/bin/echo", {HomeLogLevel::Info, &diagnostics});
            require(rejects_custom_at_last_tab(home, file), "Others must be hidden before custom import");
            SDL_VirtualJoystickDesc desc{};
            SDL_INIT_INTERFACE(&desc);
            identify_virtual_gamepad(desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
            desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
            desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
            const auto id = SDL_AttachVirtualJoystick(&desc);
            require(id != 0, SDL_GetError());
            auto* joystick = SDL_OpenJoystick(id);
            require(joystick != nullptr, SDL_GetError());
            SDL_Event event{};
            event.type = SDL_EVENT_GAMEPAD_ADDED;
            event.gdevice.which = id;
            home.event(event);
            home.controller_input(true);
            auto press = [&](SDL_GamepadButton button)
            {
                for (bool down : {true, false})
                {
                    require(SDL_SetJoystickVirtualButton(joystick, button, down), SDL_GetError());
                    SDL_UpdateJoysticks();
                    SDL_UpdateGamepads();
                    home.controller_input(true);
                }
            };
            press(SDL_GAMEPAD_BUTTON_START);
            home.draw();
            auto* screen = SDL_RenderReadPixels(renderer, nullptr);
            require(screen && SDL_SaveBMP(screen, (directory / "menu.bmp").c_str()), SDL_GetError());
            SDL_DestroySurface(screen);
            press(SDL_GAMEPAD_BUTTON_SOUTH);
            settle(home);
            press(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
            press(SDL_GAMEPAD_BUTTON_SOUTH);
            Library library(profile / "library.sqlite3");
            require(library.games().empty(), "Choosing custom content must wait for core and confirmation");
            bool rejected = false;
            try
            {
                key(home, SDL_SCANCODE_DOWN);
                key(home, SDL_SCANCODE_DOWN);
                key(home, SDL_SCANCODE_RETURN);
            }
            catch (const std::exception&)
            {
                rejected = true;
            }
            require(rejected && library.games().empty(), "Custom entry without a core must be rejected");
            key(home, SDL_SCANCODE_UP);
            key(home, SDL_SCANCODE_UP);
            press(SDL_GAMEPAD_BUTTON_SOUTH);
            settle(home);
            press(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
            press(SDL_GAMEPAD_BUTTON_SOUTH);
            press(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
            press(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
            press(SDL_GAMEPAD_BUTTON_SOUTH);
            const auto saved = library.games();
            require(saved.size() == 1 && saved[0].system == "generic" && saved[0].path == file && saved[0].core == core,
                    "Custom chooser did not persist content, generic system and core");
            key(home, SDL_SCANCODE_ESCAPE);
            key(home, SDL_SCANCODE_LEFT);
            require(!rejects_custom_at_last_tab(home, file), "Others must appear after custom import");
            key(home, SDL_SCANCODE_ESCAPE);
            press(SDL_GAMEPAD_BUTTON_START);
            press(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
            press(SDL_GAMEPAD_BUTTON_SOUTH);
            press(SDL_GAMEPAD_BUTTON_SOUTH);
            press(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
            press(SDL_GAMEPAD_BUTTON_EAST);
            const auto settings = save_storage::read(profile / "home.conf");
            require(settings.at("swap_confirm_cancel") == "1" && settings.at("dual_screen") == "1",
                    "Settings were not persisted or swapped controller confirm did not work");
            home.draw();
            screen = SDL_RenderReadPixels(renderer, nullptr);
            require(screen && SDL_SaveBMP(screen, (directory / "settings.bmp").c_str()), SDL_GetError());
            SDL_DestroySurface(screen);
            press(SDL_GAMEPAD_BUTTON_SOUTH);
            press(SDL_GAMEPAD_BUTTON_SOUTH);
            home.select_system("generic");
            key(home, SDL_SCANCODE_RIGHT);
            press(SDL_GAMEPAD_BUTTON_EAST);
            press(SDL_GAMEPAD_BUTTON_EAST);
            require(diagnostics.str().find("--system generic") != std::string::npos &&
                    diagnostics.str().find("--swap-confirm-cancel") != std::string::npos &&
                    diagnostics.str().find("--dual-screen") != std::string::npos,
                    "Custom launch did not pass generic system and Home settings");
            press(SDL_GAMEPAD_BUTTON_START);
            press(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
            press(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
            press(SDL_GAMEPAD_BUTTON_EAST);
            require(home.running, "Close Cabinet must wait for the active runner");
            settle(home);
            press(SDL_GAMEPAD_BUTTON_EAST);
            require(!home.running, "Controller could not close Cabinet after runner exit");
            require(SDL_DetachVirtualJoystick(id), SDL_GetError());
            event.type = SDL_EVENT_GAMEPAD_REMOVED;
            home.event(event);
            SDL_CloseJoystick(joystick);
        }
        {
            std::ostringstream diagnostics;
            Home home(window, renderer, profile, "/bin/echo", {HomeLogLevel::Info, &diagnostics});
            key(home, SDL_SCANCODE_F1);
            key(home, SDL_SCANCODE_DOWN);
            key(home, SDL_SCANCODE_RETURN);
            key(home, SDL_SCANCODE_RETURN);
            key(home, SDL_SCANCODE_DOWN);
            key(home, SDL_SCANCODE_RETURN);
            const auto settings = save_storage::read(profile / "home.conf");
            require(settings.at("swap_confirm_cancel") == "0" && settings.at("dual_screen") == "0",
                    "Restart did not restore saved Home settings");
            key(home, SDL_SCANCODE_ESCAPE);
            key(home, SDL_SCANCODE_ESCAPE);
            home.select_system("generic");
            key(home, SDL_SCANCODE_RIGHT);
            key(home, SDL_SCANCODE_RETURN);
            key(home, SDL_SCANCODE_RETURN);
            require(diagnostics.str().find("Launching cabinet-runner") != std::string::npos &&
                    diagnostics.str().find("--swap-confirm-cancel") == std::string::npos &&
                    diagnostics.str().find("--dual-screen") == std::string::npos,
                    "Disabled Home settings must not add runner flags");
            settle(home);
            key(home, SDL_SCANCODE_RETURN);
            for (int i = 0; i < 4; ++i)
            {
                key(home, SDL_SCANCODE_DOWN);
            }
            key(home, SDL_SCANCODE_RETURN);
            key(home, SDL_SCANCODE_DOWN);
            key(home, SDL_SCANCODE_RETURN);
            key(home, SDL_SCANCODE_LEFT);
            require(rejects_custom_at_last_tab(home, file), "Others must disappear after removing the last custom entry");
            require(fs::exists(file) && fs::exists(core), "Removing custom entry changed original files");
        }
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << "Home menu, custom import, settings, launch flags and controller quit passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
