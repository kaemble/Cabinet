#include "../src/runner.cpp"

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main() {
    try {
        require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), SDL_GetError());
        auto* surface = SDL_CreateSurface(1200, 1200, SDL_PIXELFORMAT_RGBA32);
        require(surface, SDL_GetError());
        {
            Session s;
            s.renderer = SDL_CreateSoftwareRenderer(surface);
            require(s.renderer, SDL_GetError());
            for (const auto system : {"nds", "3ds"}) {
                current = &s;
                s.frontend.bindings.configure(system, {});
                InputSnapshot snapshot;
                snapshot.buttons.fill(1);
                s.input.begin_poll(snapshot);
                const auto swap = std::string_view(system) == "nds" ? RETRO_DEVICE_ID_JOYPAD_R3 : RETRO_DEVICE_ID_JOYPAD_L3;
                s.dual_screen = false;
                require(input_state(0, RETRO_DEVICE_JOYPAD, 0, swap) == 1, "Normal screen swap input changed");
                s.dual_screen = true;
                require(input_state(0, RETRO_DEVICE_JOYPAD, 0, swap) == 0 &&
                    !(static_cast<uint16_t>(input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK)) & (1u << swap)) &&
                    input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A) == 1,
                    "Canonical screen layout did not suppress only the swap input");
                s.width = std::string_view(system) == "nds" ? 256 : 400;
                s.height = std::string_view(system) == "nds" ? 384 : 480;
                if (s.texture) SDL_DestroyTexture(s.texture);
                s.texture = SDL_CreateTexture(s.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, s.width, s.height);
                require(s.texture, SDL_GetError());
                std::vector<Uint8> pixels(s.width * s.height * 4);
                const auto native = dual_screen::views(system, 1200, 800);
                const auto bottom_source = dual_screen::pixels(native[1].source, s.width, s.height);
                for (unsigned y = 0; y < s.height; ++y) for (unsigned x = 0; x < s.width; ++x) {
                    const auto i = (y * s.width + x) * 4;
                    pixels[i] = y < s.height / 2 ? 255 : 0;
                    pixels[i + 2] = y >= s.height / 2 && x >= bottom_source.x && x < bottom_source.x + bottom_source.w ? 255 : 0;
                    pixels[i + 3] = 255;
                }
                check(SDL_UpdateTexture(s.texture, nullptr, pixels.data(), s.width * 4));
                for (const auto size : {SDL_FPoint{1200, 800}, SDL_FPoint{800, 1200}}) {
                    const auto views = dual_screen::views(system, size.x, size.y);
                    require(size.x > size.y ? views[1].cell.x > 0 : views[1].cell.y > 0, "Screen placement did not adapt to orientation");
                    for (int scaling : {0, 1, 2, 3}) {
                        s.frontend.scaling = scaling;
                        check(SDL_SetRenderDrawColor(s.renderer, 0, 0, 0, 255));
                        check(SDL_RenderClear(s.renderer));
                        for (size_t i = 0; i < views.size(); ++i) {
                            const auto source = dual_screen::pixels(views[i].source, s.width, s.height);
                            (i == 1 ? s.bottom_presentation : s.presentation).draw(s.renderer, s.presentation_image(),
                                GamePresentation::screen_viewport(s.presentation_image(), views[i]), &source);
                        }
                        auto* result = SDL_RenderReadPixels(s.renderer, nullptr);
                        require(result, SDL_GetError());
                        for (size_t i = 0; i < views.size(); ++i) {
                            const auto dest = GamePresentation::screen_viewport(s.presentation_image(), views[i]);
                            require(std::abs(dest.w / dest.h - views[i].aspect) < .001f, "Screen aspect changed");
                            for (float edge : {.1f, .5f, .9f}) {
                                Uint8 r, g, b, a;
                                check(SDL_ReadSurfacePixel(result, int(dest.x + dest.w * edge), int(dest.y + dest.h * edge), &r, &g, &b, &a));
                                require(g == 0 && r == (i == 0 ? 255 : 0) && b == (i == 1 ? 255 : 0), "Screen crop included padding or the other screen");
                            }
                        }
                        SDL_DestroySurface(result);
                    }
                    const auto dest = GamePresentation::screen_viewport(s.presentation_image(), views[1]);
                    InputRouter input;
                    const GameInputContext context{s.core_options.values, system, false};
                    input.update_pointer(context, dest.x + dest.w / 2, dest.y + dest.h / 2, true, true, dest, &views[1].source);
                    require(input.pointer().pressed && std::abs(input.pointer().x) <= 1 && input.pointer().y == 16384, "Bottom-screen touch did not map to the composite");
                    input.update_pointer(context, dest.x - 100, dest.y + dest.h + 100, true, true, dest, &views[1].source);
                    require(input.pointer().pressed && input.pointer().y == 32767, "Captured touch failed to clamp outside the screen");
                    input.update_pointer(context, dest.x - 100, dest.y, false, true, dest, &views[1].source);
                    input.update_pointer(context, dest.x - 100, dest.y, true, true, dest, &views[1].source);
                    input.update_pointer(context, dest.x + dest.w / 2, dest.y + dest.h / 2, true, true, dest, &views[1].source);
                    require(!input.pointer().pressed, "A drag starting outside the touch screen was captured");
                }
                const auto top_flipped = dual_screen::texture_source(dual_screen::pixels(native[0].source, s.width, s.height), s.height, true);
                require(top_flipped.y == s.height / 2, "OpenGL top crop did not move to the lower framebuffer rows");
            }
            s.core_options.values = {{"melonds_touch_mode", "touch"}, {"melonds_screen_layout1", "top-bottom"},
                {"melonds_screen_layout2", "left-right"}, {"melonds_number_of_screen_layouts", "2"}};
            InputRouter modern;
            const GameInputContext ds{s.core_options.values, "nds", false};
            const SDL_FRect viewport{0, 0, 256, 384};
            modern.update_pointer(ds, 128, 288, true, true, viewport);
            require(modern.pointer().pressed && modern.pointer().x == 0 && modern.pointer().y == 16384, "melonDS DS touch center failed");
            modern.update_pointer(ds, 400, 600, true, true, viewport);
            require(modern.pointer().pressed && modern.pointer().x == 32766 && modern.pointer().y == 32766, "melonDS DS drag reached exclusive edge");
            s.core_options.values["melonds_secondary_screen_scale"] = "50";
            const auto small = modern.touch_region(ds);
            require(small.x == .25f && small.w == .5f && std::abs(small.y - 2.f / 3) < .001f, "Scaled secondary touchscreen region incorrect");
            InputSnapshot cycle;
            cycle.buttons[RETRO_DEVICE_ID_JOYPAD_R3] = 1;
            modern.begin_poll(cycle); modern.update_screen_swap(s.core_options.values);
            const auto side = modern.touch_region(ds);
            require(side.x == .5f && side.w == .5f && side.h == 1, "melonDS DS layout cycling did not follow R3");
            s.frontend.bindings.configure("nds", {}); current = &s; s.dual_screen = true;
            s.core_options.defaults["melonds_screen_layout1"] = "top-bottom";
            cycle.buttons[RETRO_DEVICE_ID_JOYPAD_R2] = 1; s.input.begin_poll(cycle);
            require(input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3) == 0 &&
                input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2) == 1,
                "melonDS DS blocked touch input instead of layout cycling");
            s.core_options.values.clear();
            s.core_options.values["melonds_screen_layout1"] = "left-right";
            s.core_options.runtime_overrides["melonds_screen_layout1"] = "top-bottom";
            retro_variable definitions[] = {{"melonds_screen_layout1", "Layout; top-bottom|left-right"}, {nullptr, nullptr}};
            s.core_options.register_variables(definitions, s.frontend);
            retro_variable value{"melonds_screen_layout1", nullptr};
            require(s.core_options.get_variable(value) && std::string_view(value.value) == "top-bottom", "Temporary layout did not reach the core");
            require(!s.frontend.all_core_options[0].visible, "Temporary layout remained editable");
            require(!s.core_options.set_value({"melonds_screen_layout1", "left-right"}, s.frontend), "Core could replace the required layout");
            const auto saved = s.core_options.persistent_values(s.frontend);
            require(std::find(saved.begin(), saved.end(), std::pair<std::string, std::string>{"melonds_screen_layout1", "left-right"}) != saved.end(), "Temporary layout replaced the saved preference");
        }
        SDL_DestroySurface(surface);
        {
            Session s; current = &s;
            s.window = SDL_CreateWindow("Primary software screen", 640, 480, SDL_WINDOW_RESIZABLE);
            require(s.window, SDL_GetError());
            s.renderer = SDL_CreateRenderer(s.window, "software"); require(s.renderer, SDL_GetError());
            // Cores need not advertise pointer devices. Only an actual pointer
            // query should sample the mouse, once per input poll.
            for (const auto* system : {"nes", "gba", "nds", "3ds", "wii"}) {
                s.frontend.bindings.configure(system, {});
                input_poll();
                input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
                input_state(0, RETRO_DEVICE_ANALOG, 0, RETRO_DEVICE_ID_ANALOG_X);
                input_state(1, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
                input_state(0, RETRO_DEVICE_POINTER, 1, RETRO_DEVICE_ID_POINTER_X);
                require(!s.pointer_sampled, "Non-pointer input or unsupported pointer ports must not sample the mouse");
                input_state(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
                require(s.pointer_sampled, "First pointer query must sample immediately without descriptors or a system allowlist");
                // Inject a distinct sample: subsequent coordinate/button queries
                // in this poll must consume it rather than sampling SDL again.
                s.input.update_pointer(s.input_context(), 25, 75, true, true, {0, 0, 100, 100});
                const auto pointer = s.input.pointer();
                require(input_state(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y) == pointer.y &&
                        input_state(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED) == pointer.pressed,
                        "Pointer coordinates and buttons must share one sample per poll");
                input_poll();
                require(!s.pointer_sampled, "A new input poll must allow a fresh pointer sample");
                input_state(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED);
                require(s.pointer_sampled, "Button-first pointer queries must also sample immediately");
            }
            s.frontend.bindings.configure("nds", {});
            s.secondary = std::make_unique<SecondaryWindow>();
            s.secondary->create(s.window, s.renderer, nullptr, nullptr, "Software screens", 320, 240);
            s.format = SDL_PIXELFORMAT_XRGB8888; s.frontend.scaling = 0;
            for (unsigned size : {8u, 16u, 8u}) {
                std::vector<Uint32> pixels(size * size * 2, 0x00ff0000);
                std::fill(pixels.begin() + size * size, pixels.end(), 0x000000ff);
                video(pixels.data(), size, size * 2, size * 4);
                require(s.callback_error.empty(), s.callback_error.c_str());
                auto* renderer = s.secondary->renderer;
                const auto view = dual_screen::window_view("nds", 1, 320, 240);
                const auto crop = dual_screen::pixels(view.source, s.width, s.height);
                const auto dest = GamePresentation::screen_viewport(s.presentation_image(), view);
                require(dest.w == 320 && dest.h == 240, "Bottom window did not use its full viewport");
                auto Frame = s.presentation_image();
                Frame.texture = s.secondary->texture(s.texture);
                s.bottom_presentation.draw(renderer, Frame, dest, &crop);
                auto* image = SDL_RenderReadPixels(renderer, nullptr); require(image, SDL_GetError());
                Uint8 r, g, b, a; check(SDL_ReadSurfacePixel(image, 160, 120, &r, &g, &b, &a));
                require(r == 0 && g == 0 && b == 255, "Second software window lost its frame after upload/resize");
                SDL_DestroySurface(image); check(SDL_RenderPresent(renderer));
            }
        }
        SDL_Quit();
        std::cout << "Dual-screen crops, scaling, orientation, touch capture and temporary options passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
