#include "ui_text.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
int main() {
    try {
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        auto* window = SDL_CreateWindow("Native text test", 800, 600, 0);
        require(window != nullptr, SDL_GetError());
        auto* renderer = SDL_CreateRenderer(window, nullptr);
        require(renderer != nullptr, SDL_GetError());
        {
            TextRenderer text(renderer);
            auto clear = [&] { SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); SDL_RenderClear(renderer); };
            auto compare = [&](int logical_w, int logical_h, float scale, float px, float py, int pixel_size) {
                SDL_SetRenderLogicalPresentation(renderer, logical_w, logical_h,
                    logical_w ? SDL_LOGICAL_PRESENTATION_LETTERBOX : SDL_LOGICAL_PRESENTATION_DISABLED);
                SDL_SetRenderScale(renderer, scale, scale);
                clear();
                float logical_width = text.width("Native 0123", 14);
                text.draw(10, 20, "Native 0123", 14, {255, 255, 255, 255});
                int w, h; SDL_RendererLogicalPresentation mode;
                float sx, sy;
                SDL_GetRenderLogicalPresentation(renderer, &w, &h, &mode);
                SDL_GetRenderScale(renderer, &sx, &sy);
                require(w == logical_w && h == logical_h && sx == scale && sy == scale,
                    "Text drawing must restore UI scale and logical presentation");
                SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
                SDL_SetRenderScale(renderer, 1, 1);
                Surface actual(SDL_RenderReadPixels(renderer, nullptr), SDL_DestroySurface);
                require(actual != nullptr, SDL_GetError());
                clear();
                text.draw(px, py, "Native 0123", pixel_size, {255, 255, 255, 255});
                Surface expected(SDL_RenderReadPixels(renderer, nullptr), SDL_DestroySurface);
                require(expected != nullptr, SDL_GetError());
                require(actual->w == expected->w && actual->h == expected->h && actual->format == expected->format,
                    "Text captures must use the same native output format");
                for (int y = 0; y < actual->h; ++y)
                    require(std::memcmp(static_cast<char*>(actual->pixels) + y * actual->pitch,
                        static_cast<char*>(expected->pixels) + y * expected->pitch, actual->w * 4) == 0,
                        "Scaled UI text must exactly match text rasterized directly at native resolution");
                const float effective_scale = pixel_size / 14.0f;
                require(std::abs(logical_width * effective_scale - text.width("Native 0123", pixel_size)) < 0.01f,
                    "Text measurement must use the native rasterized glyph metrics");
            };
            compare(400, 300, 1, 20, 40, 28); // Home layout at twice its logical size.
            compare(400, 400, 1, 115, 30, 21); // Letterboxed home at fractional scale.
            compare(0, 0, 1.5f, 15, 30, 21); // Runner's scaled overlay.
            compare(0, 0, 0.5f, 5, 10, 7); // Downscaling rasterizes at a smaller size too.
        }
        SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
        std::cout << "Native-resolution text rendering and UI state checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; SDL_Quit(); return 1; }
}
