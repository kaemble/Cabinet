#include "../src/runner.cpp"

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static void rectangle(SDL_FRect r, float x, float y, float w, float h) {
    require(r.x == x && r.y == y && r.w == w && r.h == h, "Incorrect integer viewport");
}
int main() {
    try {
        rectangle(GamePresentation::viewport(641, 481, 4.f / 3, aspect_ratio::Auto, 160, 144, 3), 80, 24, 480, 432);
        rectangle(GamePresentation::viewport(1080, 720, 4.f / 3, aspect_ratio::Standard, 160, 144, 3), 140, 0, 800, 720);
        rectangle(GamePresentation::viewport(480, 800, 1, aspect_ratio::Stretch, 160, 144, 3), 0, 184, 480, 432);
        rectangle(GamePresentation::viewport(1280, 960, 1, aspect_ratio::Auto, 160, 144, 3), 160, 48, 960, 864);
        rectangle(GamePresentation::viewport(320, 240, 1, aspect_ratio::Auto, 640, 480, 3), 0, 0, 320, 240);
        rectangle(GamePresentation::viewport(1280, 720, 4.f / 3, aspect_ratio::Auto), 160, 0, 960, 720);
        require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), SDL_GetError());
        auto* surface = SDL_CreateSurface(641, 481, SDL_PIXELFORMAT_RGBA32);
        require(surface, SDL_GetError());
        {
            Session s;
            s.renderer = SDL_CreateSoftwareRenderer(surface);
            require(s.renderer, SDL_GetError());
            s.width = 160; s.height = 144; s.frontend.scaling = 3;
            s.texture = SDL_CreateTexture(s.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, s.width, s.height);
            require(s.texture, SDL_GetError());
            std::vector<Uint8> pixels(s.width * s.height * 4);
            for (unsigned y = 0; y < s.height; ++y) for (unsigned x = 0; x < s.width; ++x) {
                const auto offset = (y * s.width + x) * 4;
                pixels[offset] = x % 2 ? 0 : 255;
                pixels[offset + 2] = x % 2 ? 255 : 0;
                pixels[offset + 3] = 255;
            }
            check(SDL_UpdateTexture(s.texture, nullptr, pixels.data(), s.width * 4));
            check(SDL_SetRenderDrawColor(s.renderer, 0, 0, 0, 255));
            check(SDL_RenderClear(s.renderer));
            const auto viewport = GamePresentation::viewport(641, 481, s.aspect, s.frontend.aspect_mode, s.width, s.height, s.frontend.scaling);
            s.presentation.draw(s.renderer, s.presentation_image(), viewport);
            auto* result = SDL_RenderReadPixels(s.renderer, nullptr);
            require(result, SDL_GetError());
            auto pixel = [&](int x, int y, Uint8 red, Uint8 blue) {
                Uint8 r, g, b, a;
                check(SDL_ReadSurfacePixel(result, x, y, &r, &g, &b, &a));
                require(r == red && g == 0 && b == blue, "Integer scaling blurred pixels or misplaced borders");
            };
            pixel(79, 24, 0, 0); pixel(80, 23, 0, 0);
            pixel(80, 24, 255, 0); pixel(82, 24, 255, 0); pixel(83, 24, 0, 255);
            pixel(559, 455, 0, 255); pixel(560, 455, 0, 0); pixel(559, 456, 0, 0);
            SDL_DestroySurface(result);
            require(s.presentation.intermediate_size().x == 0, "Integer scaling allocated an intermediate texture");
            s.frontend.scaling = 2;
            for (float factor : {0.5f, 1.0f, 1.5f}) {
                s.presentation.draw(s.renderer, s.presentation_image(), {0, 0, s.width * 2.0f, s.height * 2.0f});
                require(s.presentation.intermediate_size().x > 0, "Sharp Bilinear lost its integer enlargement pass");
                s.presentation.draw(s.renderer, s.presentation_image(), {0, 0, s.width * factor, s.height * factor});
                require(s.presentation.intermediate_size().x == 0, "Sharp Bilinear retained an unnecessary 1x intermediate");
                SDL_ScaleMode mode;
                check(SDL_GetTextureScaleMode(s.texture, &mode));
                require(mode == SDL_SCALEMODE_LINEAR, "Sharp Bilinear fallback did not use bilinear filtering");
            }
            s.frontend.scaling = 3;
            const auto directory = fs::temp_directory_path() / ("cabinet-integer-" + std::to_string(SDL_GetTicksNS()));
            settings_store::write_game(directory, settings_snapshot(s));
            RunnerOptions restored;
            settings_store::read_game(directory / "settings.conf", restored);
            require(restored.scaling == 3, "Integer game setting did not persist");
            restored.scaling = 0; restored.explicit_flags.insert("--scale");
            settings_store::read_game(directory / "settings.conf", restored);
            require(restored.scaling == 0, "Game setting overrode explicit scale");
            fs::remove_all(directory);

            s.core_options.values["melonds_touch_mode"] = "touch";
            s.core_options.values["melonds_screen_layout1"] = "bottom";
            s.input.update_pointer(s.input_context(), 81, 25, true, true, viewport);
            require(s.input.pointer().pressed, "Integer image pointer hit missed");
            s.input.update_pointer(s.input_context(), 79, 25, false, true, viewport);
            require(!s.input.pointer().pressed, "Pointer release failed");
            s.input.update_pointer(s.input_context(), 79, 25, true, true, viewport);
            require(!s.input.pointer().pressed, "Integer letterbox accepted touch");
        }
        SDL_DestroySurface(surface); SDL_Quit();
        std::cout << "Integer viewport, pixel replication, letterboxing and pointer mapping passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
