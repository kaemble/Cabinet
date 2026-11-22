#include "../src/runner.cpp"
#include "virtual_gamepad.h"
#include <atomic>
#include <array>
#include <iostream>
#include <thread>

static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Motors {
    std::atomic<Uint16> low{0}, high{0};
    void set(Uint16 l, Uint16 h) { low = l; high = h; }
    Uint16 operator[](size_t index) const { return index ? high.load() : low.load(); }
    bool operator==(const std::array<Uint16,2>& other) const { return low == other[0] && high == other[1]; }
};
static SDL_JoystickID attach(Motors& motors, bool capable = true) {
    SDL_VirtualJoystickDesc desc{}; SDL_INIT_INTERFACE(&desc); identify_virtual_gamepad(desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD; desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1; desc.userdata = &motors;
    if (capable) desc.Rumble = [](void* data, Uint16 low, Uint16 high) {
        static_cast<Motors*>(data)->set(low,high); return true;
    };
    const auto id = SDL_AttachVirtualJoystick(&desc); require(id != 0, SDL_GetError()); return id;
}
int main() {
    try {
        isolate_virtual_gamepads(); require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), SDL_GetError());
        {
            require(SDL_InitSubSystem(SDL_INIT_GAMEPAD), SDL_GetError());
            Motors motors;
            SDL_VirtualJoystickDesc desc{}; SDL_INIT_INTERFACE(&desc);
            identify_virtual_gamepad(desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
            desc.name = "Cabinet rumble test"; desc.userdata = &motors;
            desc.Rumble = [](void* data, Uint16 low, Uint16 high) {
                static_cast<Motors*>(data)->set(low, high); return true;
            };
            const auto id = SDL_AttachVirtualJoystick(&desc); require(id != 0, SDL_GetError());
            {
                Session rumble; current = &rumble;
                retro_rumble_interface api{};
                require(environment(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &api) && api.set_rumble_state, "Libretro rumble interface missing");
                require(!api.set_rumble_state(0, RETRO_RUMBLE_STRONG, 1), "Rumble accepted without a controller");
                auto& ui = rumble.frontend;
                require(ui.rumble_enabled && ui.discover(), "Rumble did not default to enabled");
                require(!api.set_rumble_state(1, RETRO_RUMBLE_STRONG, 1) &&
                    !api.set_rumble_state(0, static_cast<retro_rumble_effect>(99), 1), "Invalid rumble request accepted");
                require(api.set_rumble_state(0, RETRO_RUMBLE_STRONG, 12345) &&
                    api.set_rumble_state(0, RETRO_RUMBLE_WEAK, 23456), "Supported rumble rejected");
                ui.update(true, 1);
                require(motors == std::array<Uint16,2>{12345,23456}, "Rumble strengths did not reach SDL motors");
                api.set_rumble_state(0, RETRO_RUMBLE_STRONG, 0); ui.update(true, 2);
                require(motors == std::array<Uint16,2>{0,23456}, "Stopping strong motor also stopped weak motor");
                ui.open_menu(); require(motors == std::array<Uint16,2>{}, "Pausing did not stop rumble");
                api.set_rumble_state(0, RETRO_RUMBLE_STRONG, 30000); ui.update(true, 3);
                require(motors == std::array<Uint16,2>{}, "Paused core reactivated rumble");
                ui.page = Page::Controls; ui.selected = 3;
                SDL_Event toggle{}; toggle.type = SDL_EVENT_KEY_DOWN; toggle.key.scancode = SDL_SCANCODE_RETURN;
                ui.event(toggle); require(!ui.rumble_enabled, "Input rumble toggle did not turn off");
                ui.resume_with_message("test"); api.set_rumble_state(0, RETRO_RUMBLE_STRONG, 30000); ui.update(true, 4);
                require(motors == std::array<Uint16,2>{}, "Disabled rumble reached controller");
                ui.rumble_enabled = true;
                api.set_rumble_state(0, RETRO_RUMBLE_STRONG, 30000); ui.update(true, 5);
                require(motors[0] == 30000, "Rumble could not be re-enabled");
                current = nullptr;
            }
            require(motors == std::array<Uint16,2>{}, "Session shutdown left rumble running");
            SDL_DetachVirtualJoystick(id);
        }
        Motors first, second, unsupported;
        const auto a = attach(first), b = attach(second), c = attach(unsupported, false);
        {
            Session host; current = &host;
            auto& ui = host.frontend;
            retro_rumble_interface api{};
            require(environment(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &api), "Rumble interface missing");
            auto rumble = [&](bool strong, Uint16 strength) { return api.set_rumble_state(0, strong ? RETRO_RUMBLE_STRONG : RETRO_RUMBLE_WEAK, strength); };
            require(ui.discover() && ui.controller_id() == a, "Wrong initial test pad");
            rumble(true, 20000); ui.update(true, SDL_GetTicks());
            require(first[0] == 20000, "Initial rumble missing");
            SDL_Delay(1100); ui.update(true, SDL_GetTicks());
            SDL_Delay(1100); SDL_UpdateJoysticks();
            require(first[0] == 20000, "Sustained rumble expired despite renewal");
            std::atomic<bool> done{false};
            std::thread core([&] {
                for (unsigned i = 0; i < 100000; ++i) rumble(true, i % 32000);
                rumble(true, 31000); done = true;
            });
            while (!done) { ui.update(true, SDL_GetTicks()); SDL_Delay(1); }
            core.join(); ui.update(true, SDL_GetTicks());
            require(first[0] == 31000, "Core-thread rumble request lost");
            require(SDL_DetachVirtualJoystick(a), SDL_GetError());
            SDL_Event removed{}; removed.type = SDL_EVENT_GAMEPAD_REMOVED; removed.gdevice.which = a; ui.event(removed);
            require(ui.controller_id() == b, "Replacement controller was not selected");
            ui.update(true, SDL_GetTicks());
            require(second == std::array<Uint16,2>{}, "Old rumble leaked to replacement controller");
            require(rumble(false, 12000), "Replacement controller rejected rumble");
            ui.update(true, SDL_GetTicks()); require(second[1] == 12000, "Replacement controller did not rumble");
            ui.open_menu(); require(second == std::array<Uint16,2>{}, "Pause did not stop replacement controller");
            require(SDL_DetachVirtualJoystick(b), SDL_GetError()); removed.gdevice.which = b; ui.event(removed);
            require(ui.controller_id() == c && !rumble(true, 10000), "Unsupported replacement advertised rumble");
        }
        current = nullptr;
        SDL_DetachVirtualJoystick(c); SDL_Quit();
        std::cout << "Sustained, threaded, disconnected, replacement, and unsupported rumble passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; SDL_Quit(); return 1; }
}
