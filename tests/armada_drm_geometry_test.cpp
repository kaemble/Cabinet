#include "armada_drm_geometry.h"
#include "armada_drm_input.h"
#include <SDL3/SDL.h>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static void verify_render_rotation() {
    require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    const std::array<uint32_t, 6> colors{0xffff0000, 0xff00ff00, 0xff0000ff, 0xff00ffff, 0xffff00ff, 0xffffff00};
    std::array<uint32_t, 20> padded; padded.fill(0xff888888);
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 3; ++x) padded[(y+1)*5+x+1] = colors[y*3+x];
    for (int turns = 0; turns < 4; ++turns) for (bool flipped : {false, true}) {
        constexpr int w = 10, h = 8;
        auto* surface = SDL_CreateSurface(turns % 2 ? h : w, turns % 2 ? w : h, SDL_PIXELFORMAT_ARGB8888);
        require(surface, SDL_GetError());
        auto* renderer = SDL_CreateSoftwareRenderer(surface); require(renderer, SDL_GetError());
        auto* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 5, 4);
        require(texture, SDL_GetError());
        require(SDL_UpdateTexture(texture, nullptr, padded.data(), 20), SDL_GetError());
        require(SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST), SDL_GetError());
        require(SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE), SDL_GetError());
        require(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) && SDL_RenderClear(renderer), SDL_GetError());
        const auto dest = armada_drm::rotated_destination({2, 2, 6, 4}, w, h, turns);
        const SDL_FRect source{1, 1, 3, 2};
        require(SDL_RenderTextureRotated(renderer, texture, &source, &dest, turns * 90, nullptr,
            flipped ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE), SDL_GetError());
        auto* image = SDL_RenderReadPixels(renderer, nullptr); require(image, SDL_GetError());
        std::array<uint32_t, w*h> logical{}, physical{};
        for (int y = 2; y < 6; ++y) for (int x = 2; x < 8; ++x)
            logical[y*w+x] = colors[(flipped ? 1-(y-2)/2 : (y-2)/2)*3+(x-2)/2];
        armada_drm::rotate_pixels(logical.data(), w*4, physical.data(), image->w*4, w, h, turns);
        for (int y = 0; y < image->h; ++y) for (int x = 0; x < image->w; ++x) {
            Uint8 r, g, b, a; require(SDL_ReadSurfacePixel(image, x, y, &r, &g, &b, &a), SDL_GetError());
            const auto expected = physical[y*image->w+x];
            require(r == ((expected >> 16) & 255) && g == ((expected >> 8) & 255) && b == (expected & 255),
                "GPU draw transform disagrees with CPU rotation/crop/letterbox/flip behavior");
        }
        SDL_DestroySurface(image); SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface);
    }
    SDL_Quit();
}
int main() {
    try {
        using namespace armada_drm;
        require(eligible("armada", "gamescope-0"), "Armada Game Mode was rejected");
        require(!eligible("debian", "gamescope-0") && !eligible("fedora", "gamescope-0") &&
            !eligible("armada", "wayland-0") && !eligible("armada", "") &&
            !eligible("armada", "gamescope-secondary"), "Non-Armada/desktop/bottom sessions were accepted");
        require(internal("DSI-1") && internal("eDP-1") && !internal("HDMI-A-1") &&
            !internal("DP-1"), "Lease selection could take an external monitor");
        require(rotation("Normal") == 0 && rotation("Right Side Up") == 1 &&
            rotation("Upside Down") == 2 && rotation("Left Side Up") == 3 &&
            rotation("unknown") == -1, "Panel-orientation convention changed");
        // A 3x2 image with padding in both source and destination catches stride
        // errors, inversion and clockwise/counterclockwise confusion.
        const std::array<uint32_t, 8> source{1, 2, 3, 99, 4, 5, 6, 99};
        const std::array<std::array<uint32_t, 6>, 4> expected{{
            {1,2,3,4,5,6}, {4,1,5,2,6,3}, {6,5,4,3,2,1}, {3,6,2,5,1,4}}};
        for (int turns = 0; turns < 4; ++turns) {
            const int w = turns % 2 ? 2 : 3, h = turns % 2 ? 3 : 2;
            std::array<uint32_t, 12> dest; dest.fill(0xdeadbeef);
            rotate_pixels(source.data(), 16, dest.data(), 16, 3, 2, turns);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const auto value = dest[y * 4 + x];
                    require(value == expected[turns][y * w + x], "Rotated scanout image is incorrect");
                    // Each physical pixel center must map back to its source
                    // pixel center through the touchscreen transform.
                    float tx = (x + .5f) / w, ty = (y + .5f) / h;
                    unrotate(tx, ty, turns);
                    const int sx = int(tx * 3), sy = int(ty * 2);
                    require(source[sy * 4 + sx] == value, "Touch and output rotation disagree");
                }
                for (int x = w; x < 4; ++x) require(dest[y * 4 + x] == 0xdeadbeef, "Scanout pitch padding overwritten");
            }
        }
        TouchState touch;
        touch.axis_x.minimum = 100; touch.axis_x.maximum = 1100;
        touch.axis_y.minimum = 50; touch.axis_y.maximum = 550;
        auto event = [&](unsigned type, unsigned code, int value) {
            input_event e{}; e.type = type; e.code = code; e.value = value;
            return touch.event(e);
        };
        event(EV_ABS, ABS_MT_TRACKING_ID, 42);
        event(EV_ABS, ABS_MT_POSITION_X, 350); event(EV_ABS, ABS_MT_POSITION_Y, 425);
        require(event(EV_SYN, SYN_REPORT, 0) == TouchState::Report && touch.down() &&
            touch.x() == .25f && touch.y() == .75f, "Touch axes were not normalized from evdev ranges");
        event(EV_ABS, ABS_MT_SLOT, 1); event(EV_ABS, ABS_MT_TRACKING_ID, 9);
        event(EV_ABS, ABS_MT_POSITION_X, 1000); event(EV_ABS, ABS_MT_POSITION_Y, 50);
        event(EV_SYN, SYN_REPORT, 0);
        require(touch.x() == .25f && touch.y() == .75f, "Second finger moved the emulated stylus");
        event(EV_ABS, ABS_MT_SLOT, 0); event(EV_ABS, ABS_MT_TRACKING_ID, -1);
        event(EV_SYN, SYN_REPORT, 0); require(!touch.down(), "Touch release was lost");
        event(EV_ABS, ABS_MT_TRACKING_ID, 43); event(EV_ABS, ABS_MT_POSITION_X, -10);
        event(EV_ABS, ABS_MT_POSITION_Y, 600); event(EV_SYN, SYN_REPORT, 0);
        require(touch.x() == 0 && touch.y() == 1, "Out-of-range digitizer coordinates were not clamped");
        event(EV_SYN, SYN_DROPPED, 0);
        require(!touch.down(), "Input overflow left the stylus held");
        event(EV_ABS, ABS_MT_TRACKING_ID, 44);
        require(event(EV_SYN, SYN_REPORT, 0) == TouchState::Reset && !touch.down(), "Overflow did not request slot reset");
        verify_render_rotation();
        std::cout << "Armada detection, scanout/touch rotation and evdev slot/overflow handling passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
