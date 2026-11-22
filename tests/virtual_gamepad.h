#pragma once
#include <SDL3/SDL.h>
#include <stdexcept>

// Keep device discovery deterministic even with real controllers connected.
// Set this before SDL initializes its joystick drivers, and give every test
// controller the matching IDs before attaching it.
inline void isolate_virtual_gamepads() {
    if (!SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,
                                "0xcafe/0xcab1", SDL_HINT_OVERRIDE) ||
        !SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1", SDL_HINT_OVERRIDE))
        throw std::runtime_error(SDL_GetError());
}

inline void identify_virtual_gamepad(SDL_VirtualJoystickDesc& desc) {
    desc.vendor_id = 0xcafe;
    desc.product_id = 0xcab1;
}
