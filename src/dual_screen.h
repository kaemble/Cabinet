#pragma once
#include <SDL3/SDL_rect.h>
#include <array>
#include <string_view>

// Logical, top-left coordinates in the core's forced top/bottom composite.
// Keep screen extraction separate from placement so a second window can reuse it.
namespace dual_screen
{
struct View
{
    SDL_FRect source; // Normalized composite coordinates, also used for touch.
    SDL_FRect cell;   // Drawable pixels available for this screen.
    float aspect;
};
inline std::array<View, 2> views(std::string_view system, float width, float height)
{
    const bool three_ds = system == "3ds";
    const bool landscape = width > height;
    const float w = landscape ? width / 2 : width;
    const float h = landscape ? height : height / 2;
    return {{{{0, 0, 1, .5f}, {0, 0, w, h}, three_ds ? 5.f / 3 : 4.f / 3},
             {{three_ds ? .1f : 0.f, .5f, three_ds ? .8f : 1.f, .5f},
              {landscape ? w : 0, landscape ? 0 : h, w, h},
              4.f / 3}}};
}
inline View window_view(std::string_view system, unsigned screen, float width, float height)
{
    auto view = views(system, width, height)[screen];
    view.cell = {0, 0, width, height};
    return view;
}
inline SDL_FRect pixels(SDL_FRect normalized, unsigned width, unsigned height)
{
    return {normalized.x * width, normalized.y * height, normalized.w * width, normalized.h * height};
}
// SDL flips within a source rectangle. Move the crop to the corresponding
// framebuffer rows first when an OpenGL core supplies a bottom-left image.
inline SDL_FRect texture_source(SDL_FRect source, unsigned height, bool flipped)
{
    if (flipped)
    {
        source.y = height - source.y - source.h;
    }
    return source;
}
} // namespace dual_screen
