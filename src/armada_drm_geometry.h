#pragma once
#include <cstdint>
#include <cstring>
#include <string_view>
#include <SDL3/SDL_rect.h>

namespace armada_drm
{
// Pure helpers shared with tests. Rotation is clockwise, as reported by the
// connector's panel-orientation enum (the same convention as the emulators).
inline int rotation(std::string_view orientation)
{
    if (orientation == "Normal")
    {
        return 0;
    }
    if (orientation == "Right Side Up")
    {
        return 1;
    }
    if (orientation == "Upside Down")
    {
        return 2;
    }
    if (orientation == "Left Side Up")
    {
        return 3;
    }
    return -1;
}
inline bool eligible(std::string_view os_id, std::string_view display)
{
    return os_id == "armada" && display.starts_with("gamescope-") && display != "gamescope-secondary";
}
inline bool internal(std::string_view connector)
{
    return connector.starts_with("DSI-") || connector.starts_with("eDP-") || connector.starts_with("LVDS-") ||
           connector.starts_with("DPI-");
}
inline void rotate_pixels(const void* src, int src_pitch, void* dst, int dst_pitch, int w, int h, int turns)
{
    const auto* in = static_cast<const std::uint8_t*>(src);
    auto* out = static_cast<std::uint8_t*>(dst);
    for (int y = 0; y < h; ++y)
    {
        if (!turns)
        {
            std::memcpy(out + y * dst_pitch, in + y * src_pitch, w * 4);
            continue;
        }
        for (int x = 0; x < w; ++x)
        {
            const int dx = (turns == 1) ? h - 1 - y : (turns == 2) ? w - 1 - x : y;
            const int dy = (turns == 1) ? x : (turns == 2) ? h - 1 - y : w - 1 - x;
            std::memcpy(out + dy * dst_pitch + dx * 4, in + y * src_pitch + x * 4, 4);
        }
    }
}
inline void unrotate(float& x, float& y, int turns)
{
    const float px = x, py = y;
    if (turns == 1)
    {
        x = py;
        y = 1 - px;
    }
    if (turns == 2)
    {
        x = 1 - px;
        y = 1 - py;
    }
    if (turns == 3)
    {
        x = 1 - py;
        y = px;
    }
}
// SDL rotates around the rectangle's center. Move that center into physical
// panel coordinates, keeping its unrotated dimensions for RenderTextureRotated.
inline SDL_FRect rotated_destination(SDL_FRect rect, int width, int height, int turns)
{
    const float cx = rect.x + rect.w / 2, cy = rect.y + rect.h / 2;
    float px = cx, py = cy;
    if (turns == 1)
    {
        px = height - cy;
        py = cx;
    }
    if (turns == 2)
    {
        px = width - cx;
        py = height - cy;
    }
    if (turns == 3)
    {
        px = cy;
        py = width - cx;
    }
    return {px - rect.w / 2, py - rect.h / 2, rect.w, rect.h};
}
} // namespace armada_drm
