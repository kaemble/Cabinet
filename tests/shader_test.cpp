#include "frontend.h"
#include "shader_chain.h"
#include "settings_store.h"
#include "ui_text.h"
#include "virtual_gamepad.h"
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>

static void require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
static void key(Frontend& ui, SDL_Scancode code)
{
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = code;
    ui.event(event);
}
static shaders::Stack test_stack()
{
    shaders::Stack result;
    const auto color = std::find_if(result.effects.begin(), result.effects.end(), [](const shaders::Shader& shader)
                                    { return shader.effect == shaders::Effect::ColorCorrection; });
    std::rotate(result.effects.begin(), color, result.effects.end());
    return result;
}

static void controls()
{
    Frontend ui;
    ui.shaders = test_stack();
    require(!ui.shaders.active(), "Shaders must default to disabled");
    ui.page = Page::Video;
    ui.selected = 3;
    key(ui, SDL_SCANCODE_RETURN);
    require(ui.page == Page::Shaders && ui.video_preview(), "Shaders must live under Video and preview the game");
    key(ui, SDL_SCANCODE_RETURN);
    require(ui.page == Page::Shaders, "Inactive shader parameters must not be accessible");
    key(ui, SDL_SCANCODE_RIGHT);
    require(ui.shaders.effects[0].parameters[0] == 0 && !ui.shaders.active(),
            "Inactive shader parameters must not change");
    key(ui, SDL_SCANCODE_DELETE);
    require(ui.shaders.active(), "Shader toggle failed");
    key(ui, SDL_SCANCODE_RETURN);
    require(ui.page == Page::ShaderParameters && ui.video_preview(), "Active shader parameters must be accessible");
    key(ui, SDL_SCANCODE_RIGHT);
    require(ui.shaders.effects[0].parameters[0] == 5, "Active shader parameter adjustment failed");
    key(ui, SDL_SCANCODE_RETURN);
    require(ui.page == Page::ShaderParameterValues && ui.video_preview(),
            "Shader value selection must preview the game");
    key(ui, SDL_SCANCODE_DOWN);
    key(ui, SDL_SCANCODE_RETURN);
    require(ui.page == Page::ShaderParameters && ui.shaders.effects[0].parameters[0] == 10,
            "Parameter value selection failed");
    key(ui, SDL_SCANCODE_ESCAPE);
    // Two color-correction instances make their order visible through parameter values.
    auto second = ui.shaders.effects.front();
    second.parameters[0] = 20;
    ui.shaders.effects.insert(ui.shaders.effects.begin() + 1, second);
    const auto original = ui.shaders;
    key(ui, SDL_SCANCODE_M);
    key(ui, SDL_SCANCODE_DOWN);
    require(ui.shaders.effects[0].parameters[0] == 20, "Move did not reorder effects");
    key(ui, SDL_SCANCODE_DELETE);
    require(ui.shaders.effects[1].enabled, "Toggle changed an effect while moving");
    key(ui, SDL_SCANCODE_ESCAPE);
    require(ui.shaders == original && ui.selected == 0 && ui.page == Page::Shaders,
            "Cancel must restore the move snapshot");
    key(ui, SDL_SCANCODE_M);
    key(ui, SDL_SCANCODE_DOWN);
    key(ui, SDL_SCANCODE_RETURN);
    require(ui.selected == 1 && ui.shaders.effects[0].parameters[0] == 20, "Confirm did not accept the reorder");
    key(ui, SDL_SCANCODE_DELETE);
    require(ui.selected == 1 && !ui.shaders.effects[1].enabled && ui.shaders.effects[0].enabled,
            "Active effects must sort first");
    key(ui, SDL_SCANCODE_M);
    key(ui, SDL_SCANCODE_UP);
    require(ui.selected == 1, "Move crossed the enabled/disabled boundary");
    key(ui, SDL_SCANCODE_M);
    key(ui, SDL_SCANCODE_ESCAPE);
    require(ui.page == Page::Video && ui.selected == 3, "Shaders Back did not return to Video");
    auto* window = SDL_CreateWindow("Shader preview", 640, 480, 0);
    require(window, SDL_GetError());
    auto* renderer = SDL_CreateRenderer(window, "software");
    require(renderer, SDL_GetError());
    {
        TextRenderer font(renderer);
        Frontend preview;
        for (auto page : {Page::Video, Page::Shaders, Page::ShaderParameters, Page::ShaderParameterValues})
        {
            preview.page = page;
            SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
            SDL_RenderClear(renderer);
            preview.draw(renderer, font, 640, 480);
            auto* surface = SDL_RenderReadPixels(renderer, nullptr);
            require(surface, SDL_GetError());
            Uint8 r, g, b, a;
            SDL_ReadSurfacePixel(surface, 0, 0, &r, &g, &b, &a);
            require(r == 100 && g == 100 && b == 100, "Shader menu dimmed the game preview");
            SDL_DestroySurface(surface);
            SDL_RenderPresent(renderer);
        }
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    const auto directory =
        std::filesystem::temp_directory_path() / ("cabinet-shader-" + std::to_string(SDL_GetTicksNS()));
    SettingsSnapshot snapshot;
    snapshot.values.shaders = test_stack();
    snapshot.values.shaders.effects[0].enabled = true;
    snapshot.values.shaders.effects[0].parameters = {15, 120, 80, 150, 110, 95, 105};
    snapshot.values.shaders.effects[1].enabled = true;
    snapshot.values.shaders.effects[1].parameters = {7, 0, 1, 0};
    snapshot.values.shaders.effects[2].enabled = true;
    snapshot.values.shaders.effects[2].parameters = {10, -10, 20, -20, 30, -30};
    std::swap(snapshot.values.shaders.effects[0], snapshot.values.shaders.effects[1]);
    settings_store::write_game(directory, snapshot);
    RunnerOptions restored;
    settings_store::read_game(directory / "settings.conf", restored);
    require(restored.shaders == snapshot.values.shaders, "Shader settings did not round-trip");
    settings_store::write_system(directory / "config", "gb", snapshot);
    restored = RunnerOptions{};
    restored.data_dir = directory.string();
    settings_store::read_system("gb", restored);
    require(restored.shaders == snapshot.values.shaders, "System shader settings did not round-trip");
    std::istringstream invalid("\"color_correction\" 1 0 100 100 0 100 100 100");
    const auto before = restored.shaders;
    restored.shaders.read(invalid);
    require(restored.shaders == before, "Invalid gamma replaced shader settings");
    std::filesystem::remove_all(directory);
    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    identify_virtual_gamepad(desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    const auto id = SDL_AttachVirtualJoystick(&desc);
    require(id != 0, SDL_GetError());
    auto* joystick = SDL_OpenJoystick(id);
    require(joystick, SDL_GetError());
    {
        Frontend pad;
        pad.shaders = test_stack();
        require(pad.discover(), "Virtual shader controller was not found");
        pad.page = Page::Shaders;
        pad.update(true, SDL_GetTicks());
        auto press = [&](SDL_GamepadButton button)
        {
            for (bool down : {true, false})
            {
                require(SDL_SetJoystickVirtualButton(joystick, button, down), SDL_GetError());
                SDL_UpdateJoysticks();
                SDL_UpdateGamepads();
                pad.update(true, SDL_GetTicks());
            }
        };
        press(SDL_GAMEPAD_BUTTON_SOUTH);
        require(pad.page == Page::Shaders, "Controller opened inactive shader parameters");
        press(SDL_GAMEPAD_BUTTON_NORTH);
        require(pad.shaders.active(), "North did not toggle the shader");
        auto next = pad.shaders.effects[0];
        next.parameters[0] = 25;
        pad.shaders.effects.insert(pad.shaders.effects.begin() + 1, next);
        press(SDL_GAMEPAD_BUTTON_WEST);
        press(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        require(pad.selected == 1, "D-pad did not move the shader");
        pad.swap_confirm_cancel = true;
        press(SDL_GAMEPAD_BUTTON_SOUTH);
        require(pad.selected == 0 && pad.page == Page::Shaders && pad.shaders.effects[0].parameters[0] == 0,
                "Swapped cancel did not discard the move");
        press(SDL_GAMEPAD_BUTTON_WEST);
        press(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        press(SDL_GAMEPAD_BUTTON_EAST);
        require(pad.selected == 1 && pad.page == Page::Shaders, "Swapped confirm did not finish the move");
        press(SDL_GAMEPAD_BUTTON_EAST);
        require(pad.page == Page::ShaderParameters, "Confirm after moving did not open parameters");
    }
    SDL_CloseJoystick(joystick);
    require(SDL_DetachVirtualJoystick(id), SDL_GetError());
}
static void film_controls()
{
    Frontend ui;
    ui.shaders = test_stack();
    require(ui.shaders.effects.size() == 3 && ui.shaders.effects[1].effect == shaders::Effect::FilmNoise,
            "Film grain is missing from the catalog");
    const auto defaults = ui.shaders.effects[1].parameters;
    ui.page = Page::Shaders;
    ui.selected = 1;
    key(ui, SDL_SCANCODE_DELETE);
    require(ui.selected == 0 && ui.shaders.effects[0].effect == shaders::Effect::FilmNoise,
            "Enabling film grain did not move it into the active group");
    key(ui, SDL_SCANCODE_RETURN);
    ui.selected = 0;
    key(ui, SDL_SCANCODE_RIGHT);
    require(ui.shaders.effects[0].parameters[0] == defaults[0] + 1, "Film grain did not use its own parameter step");
    key(ui, SDL_SCANCODE_RETURN);
    key(ui, SDL_SCANCODE_DOWN);
    key(ui, SDL_SCANCODE_RETURN);
    require(ui.shaders.effects[0].parameters[0] == defaults[0] + 2, "Film grain value selection failed");
    ui.selected = 1;
    key(ui, SDL_SCANCODE_RIGHT);
    require(ui.shaders.effects[0].parameters[1] == 1, "Hotspot toggle failed");
    ui.selected = 4;
    key(ui, SDL_SCANCODE_RETURN);
    require(ui.shaders.effects[0].parameters == defaults, "Restore defaults used the wrong effect");
    key(ui, SDL_SCANCODE_ESCAPE);
    ui.selected = 1;
    key(ui, SDL_SCANCODE_DELETE);
    key(ui, SDL_SCANCODE_M);
    key(ui, SDL_SCANCODE_UP);
    key(ui, SDL_SCANCODE_RETURN);
    require(ui.shaders.effects[0].effect == shaders::Effect::ColorCorrection && ui.shaders.effects[1].enabled,
            "Mixed effect reorder failed");

    shaders::Stack settings;
    const auto before = settings;
    for (const auto* text : {"\"film_noise\" 1 0", "\"film_noise\" 1 7 1 1 2",
                             "\"film_noise\" 1 7 1 1 0 unexpected",
                             "\"film_noise\" 1 7 1 1 0 999999999999999999999999",
                             "\"film_noise\" 1 17 1 1 1", "\"unknown\" 1 0 0 0"})
    {
        std::istringstream invalid(text);
        settings.read(invalid);
        require(settings == before, "Invalid film grain settings modified the stack");
    }

    Frontend chromatic;
    chromatic.page = Page::Shaders;
    chromatic.selected = 0;
    require(chromatic.shaders.effects[0].effect == shaders::Effect::ChromaticAberration,
            "Chromatic aberration is missing from the catalog");
    const auto chromatic_defaults = chromatic.shaders.effects[0].parameters;
    key(chromatic, SDL_SCANCODE_DELETE);
    key(chromatic, SDL_SCANCODE_RETURN);
    chromatic.selected = 4;
    key(chromatic, SDL_SCANCODE_RIGHT);
    require(chromatic.shaders.effects[0].parameters.size() == 6 &&
                chromatic.shaders.effects[0].parameters[4] == chromatic_defaults[4] + 1,
            "Chromatic aberration did not expose independent RGB offsets");
    chromatic.selected = 6;
    key(chromatic, SDL_SCANCODE_RETURN);
    require(chromatic.shaders.effects[0].parameters == chromatic_defaults && !chromatic.shaders.effects[2].enabled,
            "Chromatic defaults affected film grain");
}

static std::array<int, 3> corrected(std::array<int, 3> input, const shaders::Shader& shader)
{
    std::array<double, 3> value{};
    const auto& p = shader.parameters;
    for (int i = 0; i < 3; ++i)
    {
        value[i] = (input[i] / 255.0 - .5) * (p[1] / 100.0) + .5 + p[0] / 100.0;
    }
    const double luminance = value[0] * .2126 + value[1] * .7152 + value[2] * .0722;
    std::array<int, 3> output{};
    for (int i = 0; i < 3; ++i)
    {
        const double mixed = (luminance + (value[i] - luminance) * p[2] / 100.0) * p[i + 4] / 100.0;
        output[i] = static_cast<int>(std::round(std::pow(std::clamp(mixed, 0.0, 1.0), 100.0 / p[3]) * 255));
    }
    return output;
}
static void pixels(const char* driver)
{
    auto* window = SDL_CreateWindow("Shader pixels", 64, 64, 0);
    require(window, SDL_GetError());
    auto* renderer = SDL_CreateRenderer(window, driver);
    require(renderer, SDL_GetError());
    for (auto format :
         {SDL_PIXELFORMAT_RGBA32, SDL_PIXELFORMAT_XRGB1555, SDL_PIXELFORMAT_RGB565, SDL_PIXELFORMAT_BGRX32})
    {
        ShaderChain chain;
        auto stack = test_stack();
        auto* input = SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_STREAMING, 2, 2);
        require(input, SDL_GetError());
        SDL_SetTextureBlendMode(input, SDL_BLENDMODE_NONE);
        std::array<Uint8, 16> image{35, 90, 160, 255, 180, 75, 20, 255, 220, 160, 70, 255, 40, 210, 125, 255};
        std::array<Uint8, 16> upload{};
        const int pitch = SDL_BYTESPERPIXEL(format) * 2;
        require(SDL_ConvertPixels(2, 2, SDL_PIXELFORMAT_RGBA32, image.data(), 8, format, upload.data(), pitch),
                SDL_GetError());
        require(SDL_ConvertPixels(2, 2, format, upload.data(), pitch, SDL_PIXELFORMAT_RGBA32, image.data(), 8),
                SDL_GetError());
        require(SDL_UpdateTexture(input, nullptr, upload.data(), pitch), SDL_GetError());
        require(chain.draw(renderer, input, 2, 2, stack) == input && !chain.allocated(),
                "Disabled chain allocated resources");
        stack.effects[0].enabled = true;
        auto verify = [&]
        {
            auto* result = chain.draw(renderer, input, 2, 2, stack);
            require(SDL_SetTextureScaleMode(result, SDL_SCALEMODE_NEAREST), SDL_GetError());
            require(SDL_RenderTexture(renderer, result, nullptr, nullptr), SDL_GetError());
            // Overlay drawing must remain unaffected and SDL state must survive native shader draws.
            SDL_SetRenderDrawColor(renderer, 12, 34, 56, 255);
            const SDL_FRect marker{30, 30, 4, 4};
            SDL_RenderFillRect(renderer, &marker);
            auto* surface = SDL_RenderReadPixels(renderer, nullptr);
            require(surface, SDL_GetError());
            for (int y = 0; y < 2; ++y)
            {
                for (int x = 0; x < 2; ++x)
                {
                    const int offset = (y * 2 + x) * 4;
                    std::array<int, 3> expected{image[offset], image[offset + 1], image[offset + 2]};
                    for (const auto& shader : stack.effects)
                    {
                        if (shader.enabled)
                        {
                            expected = corrected(expected, shader);
                        }
                    }
                    Uint8 r, g, b, a;
                    require(SDL_ReadSurfacePixel(surface, x * 32 + 10, y * 32 + 10, &r, &g, &b, &a), SDL_GetError());
                    if (std::abs(int(r) - expected[0]) > 2 || std::abs(int(g) - expected[1]) > 2 ||
                        std::abs(int(b) - expected[2]) > 2)
                    {
                        std::cerr << driver << " at " << x << ',' << y << ": " << int(r) << ',' << int(g) << ','
                                  << int(b) << " expected " << expected[0] << ',' << expected[1] << ',' << expected[2]
                                  << '\n';
                        throw std::runtime_error("Shader pixel mismatch");
                    }
                }
            }
            Uint8 r, g, b, a;
            SDL_ReadSurfacePixel(surface, 31, 31, &r, &g, &b, &a);
            require(r == 12 && g == 34 && b == 56, "Effect changed the overlay or left native state behind");
            SDL_DestroySurface(surface);
            require(SDL_RenderPresent(renderer), SDL_GetError());
        };
        verify();
        stack.effects[0].parameters = {10, 125, 65, 140, 115, 90, 105};
        for (int i = 0; i < 8; ++i)
        {
            verify();
        }
        shaders::Shader second(shaders::Effect::ColorCorrection);
        second.enabled = true;
        second.parameters[0] = -15;
        stack.effects.insert(stack.effects.begin() + 1, second);
        verify();
        std::swap(stack.effects[0], stack.effects[1]);
        verify();
        for (auto& shader : stack.effects)
        {
            shader.enabled = false;
        }
        require(chain.draw(renderer, input, 2, 2, stack) == input && !chain.allocated(),
                "Disabled chain retained resources");
        SDL_DestroyTexture(input);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}
static void film_pixels(const char* driver)
{
    auto* window = SDL_CreateWindow("Film grain pixels", 64, 64, 0);
    require(window, SDL_GetError());
    auto* renderer = SDL_CreateRenderer(window, driver);
    require(renderer, SDL_GetError());
    {
        ShaderChain chain;
        auto stack = test_stack();
        auto* input = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 128, 128);
        require(input, SDL_GetError());
        SDL_SetTextureBlendMode(input, SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(input, SDL_SCALEMODE_NEAREST);
        std::vector<Uint8> image(128 * 128 * 4);
        for (int y = 0; y < 128; ++y)
        {
            for (int x = 0; x < 128; ++x)
            {
                const int offset = (y * 128 + x) * 4;
                image[offset] = (x < 64 && y < 64) ? 40 + x * 2 : 255;
                image[offset + 1] = (x < 64 && y < 64) ? 60 + y * 2 : 0;
                image[offset + 2] = (x < 64 && y < 64) ? 80 + x + y : 255;
                image[offset + 3] = 255;
            }
        }
        require(SDL_UpdateTexture(input, nullptr, image.data(), 128 * 4), SDL_GetError());
        auto capture = [&]
        {
            auto* output = chain.draw(renderer, input, 64, 64, stack);
            SDL_SetTextureScaleMode(output, SDL_SCALEMODE_NEAREST);
            require(SDL_RenderTexture(renderer, output, nullptr, nullptr), SDL_GetError());
            auto* surface = SDL_RenderReadPixels(renderer, nullptr);
            require(surface, SDL_GetError());
            std::vector<int> result(64 * 64 * 3);
            for (int y = 0; y < 64; ++y)
            {
                for (int x = 0; x < 64; ++x)
                {
                    Uint8 r, g, b, a;
                    require(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a), SDL_GetError());
                    const int offset = (y * 64 + x) * 3;
                    result[offset] = r;
                    result[offset + 1] = g;
                    result[offset + 2] = b;
                }
            }
            SDL_DestroySurface(surface);
            SDL_RenderPresent(renderer);
            return result;
        };
        auto& film = stack.effects[1];
        film.enabled = true;
        film.parameters.assign(4, 0);
        auto& chromatic = stack.effects[2];
        const auto neutral = capture();
        for (int y = 0; y < 64; ++y)
        {
            for (int x = 0; x < 64; ++x)
            {
                for (int channel = 0; channel < 3; ++channel)
                {
                    require(std::abs(neutral[(y * 64 + x) * 3 + channel] - image[(y * 128 + x) * 4 + channel]) <= 1,
                            "Film grain with all controls zero must preserve cropped input pixels");
                }
            }
        }
        film.parameters[0] = 16;
        const auto grain = capture();
        const auto animated = capture();
        require(grain != neutral && grain != animated, "Film grain did not animate");
        for (std::size_t pixel = 0; pixel < grain.size(); pixel += 3)
        {
            const int red_delta = grain[pixel] - neutral[pixel];
            for (int channel = 0; channel < 3; ++channel)
            {
                const int delta = grain[pixel + channel] - neutral[pixel + channel];
                require(std::abs(delta) <= 22 && std::abs(delta - red_delta) <= 1,
                        "Grain must be bounded, monochrome noise");
            }
        }
        film.parameters[0] = 0;
        film.enabled = false;
        chromatic.enabled = true;
        chromatic.parameters[4] = 100;
        const auto shifted = capture();
        const int sample = (20 * 64 + 10) * 3;
        require(shifted[sample] == neutral[sample] && shifted[sample + 1] == neutral[sample + 1] &&
                    std::abs(shifted[sample + 2] - image[(20 * 128 + 11) * 4 + 2]) <= 1,
                "Blue offset did not sample independently of red and green");
        require(std::abs(shifted[(20 * 64 + 63) * 3 + 2] - neutral[(20 * 64 + 63) * 3 + 2]) <= 1,
                "Channel offset sampled outside the cropped frame");
        chromatic.parameters[4] = 0;
        chromatic.enabled = false;
        film.enabled = true;
        const double distance = std::hypot(10.5 / 64 - .5, 20.5 / 64 - .5);
        const double t = std::clamp((distance - .25) / .5, 0.0, 1.0);
        const double factor = 1 - t * t * (3 - 2 * t);
        film.parameters[2] = 1;
        const auto vignette = capture();
        require(std::abs(vignette[sample] - std::round(neutral[sample] * factor)) <= 2,
                "Vignette does not match the centered falloff");
        auto flat_image = image;
        for (int y = 0; y < 64; ++y)
        {
            for (int x = 0; x < 64; ++x)
            {
                for (int channel = 0; channel < 3; ++channel)
                {
                    flat_image[(y * 128 + x) * 4 + channel] = 128;
                }
            }
        }
        require(SDL_UpdateTexture(input, nullptr, flat_image.data(), 128 * 4), SDL_GetError());
        const auto symmetric = capture();
        for (int y = 0; y < 64; ++y)
        {
            for (int x = 0; x < 64; ++x)
            {
                const int value = symmetric[(y * 64 + x) * 3];
                require(std::abs(value - symmetric[(y * 64 + 63 - x) * 3]) <= 1 &&
                            std::abs(value - symmetric[((63 - y) * 64 + x) * 3]) <= 1,
                        "Vignette must be symmetric around the frame center");
            }
        }
        const int center = symmetric[(31 * 64 + 31) * 3];
        const int edge = symmetric[(31 * 64) * 3];
        const int corner = symmetric[0];
        require(center >= 127 && edge > 0 && edge < center && corner < edge,
                "Vignette must preserve the center and darken smoothly toward the corners");
        require(SDL_UpdateTexture(input, nullptr, image.data(), 128 * 4), SDL_GetError());
        film.parameters[2] = 0;
        film.parameters[1] = 1;
        const auto hotspot = capture();
        require(std::abs(hotspot[sample] - std::round(neutral[sample] + factor * .2 * 255)) <= 2,
                "Hotspot differs from the reference");
        film.parameters[1] = 0;
        film.parameters[3] = 1;
        chain.reset();
        const auto scratches = capture();
        const auto clear_frame = capture();
        require(scratches != neutral && clear_frame == neutral, "Scratches must be occasional and independently switchable");
        film.parameters[3] = 0;
        film.parameters[2] = 1;
        stack.effects[0].enabled = true;
        stack.effects[0].parameters[0] = 20;
        const auto color_first = capture();
        require(std::abs(color_first[sample] - std::round((neutral[sample] + 51) * factor)) <= 2,
                "Color correction followed by film grain used the wrong program");
        std::swap(stack.effects[0], stack.effects[1]);
        const auto film_first = capture();
        require(std::abs(film_first[sample] - (std::round(neutral[sample] * factor) + 51)) <= 2 &&
                    film_first != color_first,
                "Reordering different effects did not rebuild their programs");
        stack.effects[0].enabled = false;
        const auto color_only = capture();
        require(std::abs(color_only[sample] - (neutral[sample] + 51)) <= 1,
                "Replacing a film pass with color correction used a stale program");
        stack.effects[1].enabled = false;
        require(chain.draw(renderer, input, 64, 64, stack) == input && !chain.allocated(),
                "Disabling the mixed stack retained resources");
        SDL_DestroyTexture(input);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}

int main(int argc, char** argv)
{
    try
    {
        isolate_virtual_gamepads();
        require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), SDL_GetError());
        controls();
        film_controls();
        if (argc > 1)
        {
            pixels(argv[1]);
            film_pixels(argv[1]);
        }
        SDL_Quit();
        std::cout << "Shader controls, persistence, bypass and pixels passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
