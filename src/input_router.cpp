#include "input_router.h"
#include <cstdint>
#include "wii_controller.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

void InputRouter::begin_poll(const InputSnapshot& snapshot)
{
    snapshot_ = snapshot;
    pointer_.pressed = false;
    pointer_.offscreen = true;
}

void InputRouter::reset(const GameInputContext& context)
{
    snapshot_ = {};
    pointer_.drag = pointer_.pressed = false;
    // Touch systems require a fresh press after a menu/focus transition.
    // Free pointers resume tracking on the next poll.
    pointer_.down = context.touch();
}

// melonDS DS accepts coordinates over its entire composite image. Clamp a captured
// stylus to the bottom screen, not to the surrounding image or screen gap.
SDL_FRect InputRouter::touch_region(const GameInputContext& context) const
{
    if (!context.options.contains("melonds_screen_layout1"))
    {
        return {0, 0, 1, 1};
    }
    auto option = [&](const char* key, const char* fallback)
    {
        const auto it = context.options.find(key);
        return it == context.options.end() ? std::string(fallback) : it->second;
    };
    const auto count = std::clamp(std::atoi(option("melonds_number_of_screen_layouts", "2").c_str()), 1, 8);
    const auto key = "melonds_screen_layout" + std::to_string(ds_layout_index_ % count + 1);
    const auto layout = option(key.c_str(), "top-bottom");
    const float gap = std::clamp(std::atoi(option("melonds_screen_gap", "0").c_str()), 0, 126);
    const float scale = std::clamp(std::atoi(option("melonds_secondary_screen_scale", "100").c_str()), 50, 100) / 100.f;
    const float height = 192 + gap + 192 * scale;
    if (layout == "top-bottom")
    {
        return {(1 - scale) / 2, (192 + gap) / height, scale, 192 * scale / height};
    }
    if (layout == "bottom-top")
    {
        return {0, 0, 1, 192 / height};
    }
    if (layout == "left-right")
    {
        return {.5f, 0, .5f, 1};
    }
    if (layout == "right-left")
    {
        return {0, 0, .5f, 1};
    }
    if (layout == "top")
    {
        return {0, 0, 0, 0};
    }
    if (layout == "bottom")
    {
        return {0, 0, 1, 1};
    }
    const float ratio = std::clamp(std::atoi(option("melonds_hybrid_ratio", "2").c_str()), 2, 3);
    const bool flipped = layout.starts_with("flipped-");
    if (layout.find("hybrid-") != std::string::npos)
    {
        // Prefer the large duplicate when it displays the bottom screen.
        if (layout.ends_with("bottom"))
        {
            return {flipped ? 1 / (ratio + 1) : 0, 0, ratio / (ratio + 1), 1};
        }
        return {flipped ? 0 : ratio / (ratio + 1), (ratio - 1) / ratio, 1 / (ratio + 1), 1 / ratio};
    }
    if (layout.find("largescreen-") != std::string::npos)
    {
        if (layout.ends_with("bottom"))
        {
            return {flipped ? 1 / (ratio + 1) : 0, 0, ratio / (ratio + 1), 1};
        }
        return {flipped ? 0 : ratio / (ratio + 1), (ratio - 1) / (2 * ratio), 1 / (ratio + 1), 1 / ratio};
    }
    // Rotated layouts retain a vertical framebuffer when the frontend
    // does not advertise RETRO_ENVIRONMENT_SET_ROTATION support.
    return {0, (192 + gap) / (384 + gap), 1, 192 / (384 + gap)};
}

void InputRouter::update_pointer(const GameInputContext& context, float x, float y, bool down, bool focused,
                                 const SDL_FRect& viewport, const SDL_FRect* source_region)
{
    const auto region = source_region ? *source_region : touch_region(context);
    const bool valid = viewport.w > 0 && viewport.h > 0 && region.w > 0 && region.h > 0;
    float nx = valid ? (x - viewport.x) / viewport.w : 0;
    float ny = valid ? (y - viewport.y) / viewport.h : 0;
    if (source_region)
    {
        nx = region.x + nx * region.w;
        ny = region.y + ny * region.h;
    }
    const bool inside =
        valid && nx >= region.x && ny >= region.y && nx < region.x + region.w && ny < region.y + region.h;
    // Touch systems (DS and 3DS) require the press to
    // begin inside the active region so the cursor cannot "click through" from
    // outside the screen. Free-aim pointers (Wii IR, lightguns) track continuously.
    const bool touch = context.touch();
    if (touch)
    {
        if (!down || !focused || context.paused || !valid)
        {
            pointer_.drag = false;
        }
        else if (!pointer_.down && inside)
        {
            pointer_.drag = true;
        }
        pointer_.down = down;
        pointer_.pressed = pointer_.drag;
        pointer_.offscreen = !inside && !pointer_.drag;
    }
    else
    {
        pointer_.drag = false;
        pointer_.down = down;
        pointer_.pressed = down && focused && !context.paused && valid;
        pointer_.offscreen = !inside;
    }
    if (valid)
    {
        // Stay within the core's exclusive upper bound after its integer
        // conversion, including at the top/left edge of a split screen.
        const bool modern_ds = context.options.contains("melonds_screen_layout1");
        auto coordinate = [modern_ds](float value, float start, float extent)
        {
            const float range = modern_ds ? 65534.f : 65536.f;
            const float offset = modern_ds ? 32767.f : 32768.f;
            const int low = std::max(-32767, int(std::ceil(start * range - offset)));
            const int high = std::min(32767, int(std::ceil((start + extent) * range - offset)) - 1);
            return static_cast<std::int16_t>(std::clamp(int(std::lround(value * range - offset)), low, high));
        };
        pointer_.x = coordinate(nx, region.x, region.w);
        pointer_.y = coordinate(ny, region.y, region.h);
    }
}
void InputRouter::update_screen_swap(const std::map<std::string, std::string>& options)
{
    if (options.contains("melonds_screen_layout1"))
    {
        const bool down = snapshot_.state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3);
        const auto configured = options.find("melonds_number_of_screen_layouts");
        const unsigned count =
            std::clamp(configured == options.end() ? 2 : std::atoi(configured->second.c_str()), 1, 8);
        if (ds_layout_index_ >= count)
        {
            ds_layout_index_ = 0;
        }
        if (down && !ds_swap_down_)
        {
            ds_layout_index_ = (ds_layout_index_ + 1) % count;
        }
        ds_swap_down_ = down;
    }
}

std::int16_t InputRouter::state(unsigned port, unsigned device, unsigned index, unsigned id) const
{
    if (device == RETRO_DEVICE_POINTER)
    {
        if (port != 0 || index != 0)
        {
            return 0;
        }
        switch (id)
        {
        case RETRO_DEVICE_ID_POINTER_X:
            return pointer_.x;
        case RETRO_DEVICE_ID_POINTER_Y:
            return pointer_.y;
        case RETRO_DEVICE_ID_POINTER_PRESSED:
            return pointer_.pressed;
        case RETRO_DEVICE_ID_POINTER_COUNT:
            return pointer_.pressed ? 1 : 0;
        case RETRO_DEVICE_ID_POINTER_IS_OFFSCREEN:
            return pointer_.offscreen;
        default:
            return 0;
        }
    }
    return snapshot_.state(port, device, index, id);
}

void InputRouter::register_controllers(const retro_controller_info* info, std::string_view system)
{
    controller_device_ = RETRO_DEVICE_JOYPAD;
    if (!info)
    {
        return;
    }
    const auto& port = *info;
    for (unsigned i = 0; port.types && i < port.num_types && port.types[i].desc; ++i)
    {
        std::string name = port.types[i].desc;
        controller_types_[name] = port.types[i].id;
        if ((system == "psx" && name == "Analog Controller (DualShock)") ||
            (system == "md" && name == "MD Joypad 6 Button") || (system == "dc" && name == "Full Controller"))
        {
            controller_device_ = port.types[i].id;
            break;
        }
    }
}

unsigned InputRouter::wii_device(int selection) const
{
    const auto name = wii_controller::descriptors[selection];
    const auto found = controller_types_.find(name);
    if (found == controller_types_.end())
    {
        throw std::runtime_error(std::string("Core does not support controller: ") + name);
    }
    return found->second;
}

void InputRouter::connect_wii_controller(int selection, decltype(&retro_set_controller_port_device) connect)
{
    const auto device = wii_device(selection);
    // Reset the shared player-one interface before choosing its device type.
    for (unsigned port = 0; port < 8; ++port)
    {
        connect(port, RETRO_DEVICE_NONE);
    }
    connect(0, device);
    controller_device_ = device;
    snapshot_ = {};
}
