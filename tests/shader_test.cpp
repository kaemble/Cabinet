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
static void controls()
{
    Frontend ui;
    require(!ui.shaders.active(), "Shaders must default to disabled");
    ui.page = Page::Video;
    ui.selected = 3;
    key(ui, SDL_SCANCODE_RETURN);
    require(ui.page == Page::Shaders && ui.video_preview(), "Shaders must live under Video and preview the game");
    key(ui, SDL_SCANCODE_RETURN);
    require(ui.page == Page::ShaderParameters && ui.video_preview(), "Disabled shader parameters must be accessible");
    key(ui, SDL_SCANCODE_RIGHT);
    require(ui.shaders.effects[0].parameters[0] == 5 && !ui.shaders.active(),
            "Parameter adjustment must not enable effects");
    key(ui, SDL_SCANCODE_RETURN);
    require(ui.page == Page::ShaderParameterValues && ui.video_preview(),
            "Shader value selection must preview the game");
    key(ui, SDL_SCANCODE_DOWN);
    key(ui, SDL_SCANCODE_RETURN);
    require(ui.page == Page::ShaderParameters && ui.shaders.effects[0].parameters[0] == 10,
            "Parameter value selection failed");
    key(ui, SDL_SCANCODE_ESCAPE);
    key(ui, SDL_SCANCODE_DELETE);
    require(ui.shaders.active(), "Shader toggle failed");
    // Two instances distinguish ordering without adding an unshipped effect to the catalog.
    auto second = ui.shaders.effects.front();
    second.parameters[0] = 20;
    ui.shaders.effects.push_back(second);
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
    snapshot.values.shaders.effects[0].enabled = true;
    snapshot.values.shaders.effects[0].parameters = {15, 120, 80, 150, 110, 95, 105};
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
        press(SDL_GAMEPAD_BUTTON_NORTH);
        require(pad.shaders.active(), "North did not toggle the shader");
        auto next = pad.shaders.effects[0];
        next.parameters[0] = 25;
        pad.shaders.effects.push_back(next);
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
        shaders::Stack stack;
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
        shaders::Shader second;
        second.enabled = true;
        second.parameters[0] = -15;
        stack.effects.push_back(second);
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
int main(int argc, char** argv)
{
    try
    {
        isolate_virtual_gamepads();
        require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), SDL_GetError());
        controls();
        if (argc > 1)
        {
            pixels(argv[1]);
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
