// Exercise the libretro callbacks and the SDL UI against a real GL driver.
#include "../src/runner.cpp"
#include <cstring>
#define GL_GLEXT_PROTOTYPES
#include <SDL3/SDL_opengl.h>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<typename T> static T proc(const char* name) {
    auto p = reinterpret_cast<T>(current->hw.get_proc_address(name));
    require(p != nullptr, name); return p;
}
static unsigned resets = 0, destroys = 0;
static uint32_t state_value = 0;
static SDL_GLContext expected_context = nullptr;
static size_t state_size() { return sizeof(state_value); }
static bool serialize(void* data, size_t size) {
    require(SDL_GL_GetCurrentContext() == expected_context, "Save without core context");
    if (size != sizeof(state_value)) return false;
    std::memcpy(data, &state_value, size); return true;
}
static bool unserialize(const void* data, size_t size) {
    require(SDL_GL_GetCurrentContext() == expected_context, "Load without core context");
    if (size != sizeof(state_value)) return false;
    std::memcpy(&state_value, data, size); return true;
}
static void context_reset() {
    require(SDL_GL_GetCurrentContext() != nullptr, "Reset without current context");
    require(current->hw.get_current_framebuffer() != 0, "Reset without framebuffer");
    ++resets;
}
static void context_destroy() {
    require(SDL_GL_GetCurrentContext() != nullptr, "Destroy without current context");
    ++destroys;
}
static void unload_game() {
    require(destroys == resets, "Game unloaded before graphics were destroyed");
}
static void pattern(unsigned width, unsigned height) {
    auto bind = proc<decltype(&glBindFramebuffer)>("glBindFramebuffer");
    auto clear_color = proc<decltype(&glClearColor)>("glClearColor");
    auto clear = proc<decltype(&glClear)>("glClear");
    auto enable = proc<decltype(&glEnable)>("glEnable");
    auto disable = proc<decltype(&glDisable)>("glDisable");
    auto scissor = proc<decltype(&glScissor)>("glScissor");
    bind(GL_FRAMEBUFFER, current->hw.get_current_framebuffer());
    disable(GL_SCISSOR_TEST);
    clear_color(1, 0, 0, 1); clear(GL_COLOR_BUFFER_BIT);
    enable(GL_SCISSOR_TEST); scissor(0, 0, width, height / 2);
    clear_color(0, 0, 1, 1); clear(GL_COLOR_BUFFER_BIT);
    disable(GL_SCISSOR_TEST);
}
static void verify_pixels(Session& s, bool flip) {
    check(SDL_SetRenderDrawColor(s.renderer, 0, 0, 0, 255));
    check(SDL_RenderClear(s.renderer));
    SDL_FRect source{0, 0, static_cast<float>(s.width), static_cast<float>(s.height)};
    int w = 0, h = 0; check(SDL_GetRenderOutputSize(s.renderer, &w, &h));
    s.presentation.draw(s.renderer, s.presentation_image(), {0, 0, static_cast<float>(w), static_cast<float>(h)});
    SDL_Surface* surface = SDL_RenderReadPixels(s.renderer, nullptr);
    check(surface != nullptr);
    Uint8 r, g, b, a;
    check(SDL_ReadSurfacePixel(surface, 10, 10, &r, &g, &b, &a));
    require(g == 0 && (flip ? r == 255 && b == 0 : b == 255 && r == 0), "Top edge inverted or corrupted");
    check(SDL_ReadSurfacePixel(surface, 10, surface->h - 10, &r, &g, &b, &a));
    require(g == 0 && (flip ? b == 255 && r == 0 : r == 255 && b == 0), "Bottom edge inverted or corrupted");
    SDL_DestroySurface(surface);
    // Cropping a bottom-left framebuffer must move the source rectangle as
    // well as flip its contents, for both direct and Sharp Bilinear paths.
    const int original_scaling = s.frontend.scaling;
    for (int scaling : {0, 2}) {
        s.frontend.scaling = scaling;
        for (unsigned screen = 0; screen < 2; ++screen) {
            const SDL_FRect crop{0, screen * s.height / 2.f, float(s.width), s.height / 2.f};
            (screen == 1 ? s.bottom_presentation : s.presentation).draw(s.renderer, s.presentation_image(), {0, 0, float(w), float(h)}, &crop);
            surface = SDL_RenderReadPixels(s.renderer, nullptr);
            check(surface != nullptr);
            for (int y : {10, h - 10}) {
                check(SDL_ReadSurfacePixel(surface, 10, y, &r, &g, &b, &a));
                const bool red = flip != (screen != 0);
                require(g == 0 && r == (red ? 255 : 0) && b == (red ? 0 : 255), "Cropped OpenGL screen inverted or wrong half");
            }
            SDL_DestroySurface(surface);
        }
    }
    s.frontend.scaling = original_scaling;
    s.presentation.draw(s.renderer, s.presentation_image(), {0, 0, float(w), float(h)});
    if (s.secondary) {
        auto& bottom = *s.secondary;
        bottom.begin_draw();
        const SDL_FRect crop{0, s.height / 2.f, float(s.width), s.height / 2.f};
        int bw = 0, bh = 0; check(SDL_GetRenderOutputSize(bottom.renderer, &bw, &bh));
        const int original = s.frontend.scaling;
        s.frontend.scaling = 1; // Deliberately use different filtering in the two wrappers.
        auto Frame = s.presentation_image();
        Frame.texture = bottom.texture(s.texture);
        s.bottom_presentation.draw(bottom.renderer, Frame, {0, 0, float(bw), float(bh)}, &crop);
        s.frontend.scaling = original;
        surface = SDL_RenderReadPixels(bottom.renderer, nullptr); check(surface != nullptr);
        check(SDL_ReadSurfacePixel(surface, bw / 2, bh / 2, &r, &g, &b, &a));
        require(g == 0 && r == (flip ? 0 : 255) && b == (flip ? 255 : 0), "Shared OpenGL bottom window displayed the wrong texture/crop");
        SDL_DestroySurface(surface); check(SDL_RenderPresent(bottom.renderer));
        bottom.end_draw();
        require(SDL_GL_GetCurrentContext() == s.hardware->renderer_context(), "Bottom window did not restore the primary renderer context");
        const auto properties = SDL_GetTextureProperties(s.texture);
        const auto native = SDL_GetNumberProperty(properties, SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER,
            SDL_GetNumberProperty(properties, SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER, 0));
        GLint previous = 0, filter = 0;
        auto bind = proc<decltype(&glBindTexture)>("glBindTexture");
        proc<decltype(&glGetIntegerv)>("glGetIntegerv")(GL_TEXTURE_BINDING_2D, &previous);
        bind(GL_TEXTURE_2D, static_cast<GLuint>(native));
        proc<decltype(&glGetTexParameteriv)>("glGetTexParameteriv")(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &filter);
        bind(GL_TEXTURE_2D, previous);
        SDL_ScaleMode expected; check(SDL_GetTextureScaleMode(s.texture, &expected));
        require(filter == (expected == SDL_SCALEMODE_LINEAR ? GL_LINEAR : GL_NEAREST), "Second window changed the primary texture's cached filtering");
    }
}
int main(int argc, char** argv) {
    try {
        require(argc == 2 || argc == 3, "Expected test-data directory and optional API");
        check(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD));
        for (auto type : {RETRO_HW_CONTEXT_OPENGL, RETRO_HW_CONTEXT_OPENGL_CORE,
                          RETRO_HW_CONTEXT_OPENGLES2, RETRO_HW_CONTEXT_OPENGLES3,
                          RETRO_HW_CONTEXT_OPENGLES_VERSION}) {
            if (argc == 3 && type != (std::string(argv[2]) == "gles2" ? RETRO_HW_CONTEXT_OPENGLES2 : RETRO_HW_CONTEXT_OPENGL)) continue;
            for (bool flip : {false, true}) {
                Session s; current = &s;
                s.frontend.scaling = 0; // Pixel checks exercise nearest first.
                retro_hw_render_callback unsupported{};
                unsupported.context_type = RETRO_HW_CONTEXT_D3D11;
                unsupported.context_reset = context_reset;
                require(!environment(RETRO_ENVIRONMENT_SET_HW_RENDER, &unsupported), "Accepted Direct3D without a backend");
                require(s.callback_error.empty(), "Rejected API prevented core fallback");
                retro_hw_render_callback cb{};
                cb.context_type = type; cb.version_major = 3; cb.version_minor = type == RETRO_HW_CONTEXT_OPENGL_CORE ? 3 : 1;
                cb.depth = true; cb.stencil = true; cb.bottom_left_origin = flip;
                cb.context_reset = context_reset; cb.context_destroy = context_destroy;
                require(environment(RETRO_ENVIRONMENT_SET_HW_RENDER, &cb), "GL negotiation failed");
                s.hardware = std::make_unique<HardwareVideo>();
                s.hardware->create_context(s.hw);
                auto early_context = SDL_GL_GetCurrentContext();
                require(early_context != nullptr, "AV query has no current context");
                require(proc<decltype(&glGetString)>("glGetString")(GL_VERSION) != nullptr, "AV query cannot initialize GL");
                require(s.hardware->framebuffer() == 0, "Framebuffer allocated before dimensions known");
                s.window = SDL_CreateWindow("Hardware video test", 640, 480, SDL_WINDOW_RESIZABLE);
                check(s.window != nullptr);
                s.renderer = s.hardware->create_renderer(s.window); check(s.renderer != nullptr);
                s.hardware->create_framebuffer(128, 128);
                require(SDL_GL_GetCurrentContext() == early_context, "Framebuffer allocation replaced context");
                s.hw_ready = true; s.hw.context_reset();
                s.core.api.unload_game = unload_game;
                s.core.api.load_game = [](const retro_game_info*) { return true; };
                require(s.core.load_game(nullptr), "Test core failed to load");
                auto core_context = SDL_GL_GetCurrentContext();
                expected_context = core_context;
                s.core.api.serialize_size = state_size; s.core.api.serialize = serialize; s.core.api.unserialize = unserialize;
                s.saves.state_path = fs::path(argv[1]) / "hardware-test.state";
                s.secondary = std::make_unique<SecondaryWindow>();
                s.secondary->create(s.window, s.renderer, s.hardware.get(), nullptr, "OpenGL screens", 320, 240);
                for (unsigned size : {64u, 128u, 32u}) {
                    s.hardware->make_current();
                    const auto core_window = SDL_GL_GetCurrentWindow();
                    s.hardware->make_current();
                    require(SDL_GL_GetCurrentContext() == core_context && SDL_GL_GetCurrentWindow() == core_window,
                        "Repeated core activation changed the current context or drawable");
                    pattern(size, size);
                    auto store = proc<decltype(&glPixelStorei)>("glPixelStorei");
                    auto get = proc<decltype(&glGetIntegerv)>("glGetIntegerv");
                    store(GL_PACK_ALIGNMENT, 8);
                    const bool separate_buffers = type != RETRO_HW_CONTEXT_OPENGLES2 && type != RETRO_HW_CONTEXT_OPENGL;
                    GLuint pbo = 0;
                    if (separate_buffers) {
                        proc<decltype(&glGenBuffers)>("glGenBuffers")(1, &pbo);
                        proc<decltype(&glBindBuffer)>("glBindBuffer")(GL_PIXEL_PACK_BUFFER, pbo);
                        proc<decltype(&glBufferData)>("glBufferData")(GL_PIXEL_PACK_BUFFER, 16, nullptr, GL_STREAM_READ);
                        store(GL_PACK_ROW_LENGTH, 256); store(GL_PACK_SKIP_ROWS, 3); store(GL_PACK_SKIP_PIXELS, 2);
                        proc<decltype(&glReadBuffer)>("glReadBuffer")(GL_NONE);
                        proc<decltype(&glBindFramebuffer)>("glBindFramebuffer")(GL_READ_FRAMEBUFFER, 0);
                        proc<decltype(&glBindFramebuffer)>("glBindFramebuffer")(GL_DRAW_FRAMEBUFFER, 0);
                    }
                    video(RETRO_HW_FRAME_BUFFER_VALID, size, size, 0);
                    require(s.callback_error.empty(), s.callback_error.c_str());
                    require(SDL_GL_GetCurrentContext() == core_context, "Video callback lost the core context");
                    GLint alignment; get(GL_PACK_ALIGNMENT, &alignment);
                    require(alignment == 8, "Video callback changed core pixel packing");
                    if (separate_buffers) {
                        GLint value;
                        get(GL_PIXEL_PACK_BUFFER_BINDING, &value); require(value == static_cast<GLint>(pbo), "Pack buffer binding changed");
                        get(GL_PACK_ROW_LENGTH, &value); require(value == 256, "Pack row length changed");
                        get(GL_PACK_SKIP_ROWS, &value); require(value == 3, "Pack row offset changed");
                        get(GL_PACK_SKIP_PIXELS, &value); require(value == 2, "Pack pixel offset changed");
                        get(GL_READ_FRAMEBUFFER_BINDING, &value); require(value == 0, "Read framebuffer binding changed");
                        get(GL_DRAW_FRAMEBUFFER_BINDING, &value); require(value == 0, "Draw framebuffer binding changed");
                        proc<decltype(&glBindFramebuffer)>("glBindFramebuffer")(GL_READ_FRAMEBUFFER, s.hardware->framebuffer());
                        get(GL_READ_BUFFER, &value); require(value == GL_NONE, "Read buffer selection changed");
                        proc<decltype(&glDeleteBuffers)>("glDeleteBuffers")(1, &pbo);
                    }
                    auto& color = *std::find_if(s.frontend.shaders.effects.begin(), s.frontend.shaders.effects.end(),
                                               [](const shaders::Shader& shader)
                                               { return shader.effect == shaders::Effect::ColorCorrection; });
                    color.enabled = false;
                    verify_pixels(s, flip);
                    color.enabled = true;
                    verify_pixels(s, flip);
                    s.frontend.scaling = 2;
                    verify_pixels(s, flip);
                    const auto Intermediate = s.presentation.intermediate_size();
                    require(Intermediate.x > 0 && Intermediate.x % s.width == 0 && Intermediate.y % s.height == 0, "Sharp intermediate is not an integer scale");
                    s.frontend.scaling = 0;
                    const auto frames = s.frames;
                    video(nullptr, size, size, 0);
                    require(s.frames == frames, "Duplicate frame was uploaded");
                    TextRenderer text(s.renderer);
                    s.frontend.open_menu(); s.frontend.draw(s.renderer, text, 640, 480);
                    check(SDL_RenderPresent(s.renderer));
                    const auto ui_context = SDL_GL_GetCurrentContext();
                    require(ui_context == s.hardware->renderer_context(), "Menu did not retain its UI context");
                    require(execute_action(s, Action::None), "Idle action stopped the session");
                    require(SDL_GL_GetCurrentContext() == ui_context, "Idle menu unnecessarily activated the core context");
                }
                state_value = 123;
                execute_action(s, Action::Save);
                require(!s.frontend.paused(), "Successful hardware save did not resume");
                state_value = 456;
                s.frontend.open_menu();
                execute_action(s, Action::Load);
                require(!s.frontend.paused() && state_value == 123, "Hardware state load did not restore and resume");
                check(SDL_SetWindowSize(s.window, 480, 640));
                check(SDL_SetWindowFullscreen(s.window, true));
                check(SDL_SetWindowFullscreen(s.window, false));
                s.hardware->make_current();
                require(SDL_GL_GetCurrentContext() == core_context, "Fullscreen replaced core context");
                pattern(64, 64); video(RETRO_HW_FRAME_BUFFER_VALID, 64, 64, 0);
                require(s.callback_error.empty(), s.callback_error.c_str());
                verify_pixels(s, flip);
                retro_game_geometry geometry{64, 32, 128, 128, 0};
                require(environment(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry) && s.aspect == 2, "Geometry update ignored");
                const auto previous_resets = resets, previous_destroys = destroys;
                retro_system_av_info updated{{256, 256, 256, 256, 1}, {60, 44100}};
                require(environment(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &updated), "Resolution AV update rejected");
                require(resets == previous_resets + 1 && destroys == previous_destroys + 1, "Resolution change skipped core graphics lifecycle");
                pattern(256, 256); video(RETRO_HW_FRAME_BUFFER_VALID, 256, 256, 0);
                require(s.callback_error.empty(), "Larger resolution frame rejected");
                verify_pixels(s, flip);
                // PPSSPP resizes live without losing its context or restarting
                // its emulation thread. Exercise growth and shrinkage after a
                // texture has already been imported by the SDL renderer.
                s.core_options.resolution_key = "ppsspp_internal_resolution";
                s.hardware->make_current();
                const auto retained_context = SDL_GL_GetCurrentContext();
                const auto retained_fbo = s.hardware->framebuffer();
                const auto retained_resets = resets, retained_destroys = destroys;
                for (unsigned size : {512u, 128u, 256u}) {
                    retro_system_av_info resized{{size, size, size, size, 1}, {60, 44100}};
                    require(environment(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &resized), "PPSSPP resize rejected");
                    require(SDL_GL_GetCurrentContext() == retained_context && s.hardware->framebuffer() == retained_fbo,
                        "PPSSPP resize replaced graphics context or framebuffer");
                    require(resets == retained_resets && destroys == retained_destroys,
                        "PPSSPP resize invoked destructive lifecycle callbacks");
                    pattern(size, size); video(RETRO_HW_FRAME_BUFFER_VALID, size, size, 0);
                    require(s.callback_error.empty(), "PPSSPP resized frame rejected");
                    verify_pixels(s, flip);
                }
                video(RETRO_HW_FRAME_BUFFER_VALID, 257, 64, 0);
                require(!s.callback_error.empty(), "Oversized frame was accepted");
            }
        }
        require(resets == (argc == 3 ? 4u : 20u) && destroys == resets, "Context lifecycle callbacks missing");
        fs::remove(fs::path(argv[1]) / "hardware-test.state");
        SDL_Quit();
        std::cout << "OpenGL/GLES contexts, framebuffer orientation, resize, UI, fullscreen and lifecycle passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; SDL_Quit(); return 1; }
}
