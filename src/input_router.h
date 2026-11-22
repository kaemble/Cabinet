#pragma once
#include <cstdint>
#include "input_bindings.h"
#include <libretro.h>
#include <map>
#include <string_view>

struct GameInputContext
{
    const std::map<std::string, std::string>& options;
    std::string_view system;
    bool paused;
    bool touch() const
    {
        return options.contains("melonds_screen_layout1") || system == "nds" || system == "3ds";
    }
};

class InputRouter
{
public:
    struct Pointer
    {
        std::int16_t x = 0, y = 0;
        bool pressed = false, offscreen = true;
        bool drag = false, down = false;
    };
    const Pointer& pointer() const
    {
        return pointer_;
    }
    void begin_poll(const InputSnapshot& snapshot);
    void update_pointer(const GameInputContext& context, float x, float y, bool down, bool focused,
                        const SDL_FRect& viewport, const SDL_FRect* source_region = nullptr);
    SDL_FRect touch_region(const GameInputContext& context) const;
    void update_screen_swap(const std::map<std::string, std::string>& options);
    std::int16_t state(unsigned port, unsigned device, unsigned index, unsigned id) const;
    void reset(const GameInputContext& context);

    void register_controllers(const retro_controller_info* info, std::string_view system);
    unsigned device() const
    {
        return controller_device_;
    }
    unsigned wii_device(int selection) const;
    void connect_wii_controller(int selection, decltype(&retro_set_controller_port_device) connect);

private:
    InputSnapshot snapshot_;
    Pointer pointer_;
    bool ds_swap_down_ = false;
    unsigned ds_layout_index_ = 0;
    unsigned controller_device_ = RETRO_DEVICE_JOYPAD;
    std::map<std::string, unsigned> controller_types_;
};
