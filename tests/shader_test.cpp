#include "frontend.h"
#include "game_presentation.h"
#include "secondary_window.h"
#include "shader_chain.h"
#include "settings_store.h"
#include "ui_text.h"
#include "virtual_gamepad.h"
#include <algorithm>
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
static int effect_index(const shaders::Stack& stack, shaders::Effect effect)
{
    const auto found = std::find_if(stack.effects.begin(), stack.effects.end(),
                                    [&](const shaders::Shader& shader) { return shader.effect == effect; });
    require(found != stack.effects.end(), "Effect is missing from the catalog");
    return static_cast<int>(found - stack.effects.begin());
}

static shaders::Stack test_stack()
{
    shaders::Stack result;
    result.effects = {shaders::Shader(shaders::Effect::ColorCorrection), shaders::Shader(shaders::Effect::FilmNoise),
                      shaders::Shader(shaders::Effect::ChromaticAberration), shaders::Shader(shaders::Effect::Vignette),
                      shaders::Shader(shaders::Effect::Cas), shaders::Shader(shaders::Effect::CrtGeom)};
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
    key(ui, SDL_SCANCODE_DELETE);
    require(!ui.shaders.active(), "Delete must no longer toggle shaders");
    key(ui, SDL_SCANCODE_RIGHT);
    require(ui.shaders.active() && ui.selected == 0 && ui.shaders.effects[0].parameters[0] == 0,
            "Right must toggle the effect in place without changing its parameters");
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
    key(ui, SDL_SCANCODE_RIGHT);
    require(ui.shaders.effects[1].enabled, "Toggle changed an effect while moving");
    key(ui, SDL_SCANCODE_ESCAPE);
    require(ui.shaders == original && ui.selected == 0 && ui.page == Page::Shaders,
            "Cancel must restore the move snapshot");
    key(ui, SDL_SCANCODE_M);
    key(ui, SDL_SCANCODE_DOWN);
    key(ui, SDL_SCANCODE_RETURN);
    require(ui.selected == 1 && ui.shaders.effects[0].parameters[0] == 20, "Confirm did not accept the reorder");
    key(ui, SDL_SCANCODE_RIGHT);
    require(ui.selected == 1 && !ui.shaders.effects[1].enabled && ui.shaders.effects[0].enabled,
            "Toggling an effect must keep its position");
    key(ui, SDL_SCANCODE_M);
    key(ui, SDL_SCANCODE_UP);
    require(ui.selected == 0 && !ui.shaders.effects[0].enabled,
            "Moving must be allowed across enabled and disabled effects");
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
    snapshot.values.shaders.effects[1].parameters = {7, 0, 1};
    snapshot.values.shaders.effects[2].enabled = false;
    snapshot.values.shaders.effects[2].parameters = {10, -10, 20, -20, 30, -30};
    snapshot.values.shaders.effects[3].enabled = true;
    snapshot.values.shaders.effects[3].parameters = {65};
    snapshot.values.shaders.effects[4].enabled = false;
    snapshot.values.shaders.effects[4].parameters = {35, 75};
    snapshot.values.shaders.effects[5].enabled = true;
    snapshot.values.shaders.effects[5].parameters[4] = 0;
    snapshot.values.shaders.effects[5].parameters[14] = 35;
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
        require(!pad.shaders.active(), "North must sort rather than toggle effects");
        press(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        require(pad.shaders.active(), "D-pad Right did not toggle the shader");
        pad.selected = 2;
        press(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        require(pad.selected == 2 && pad.shaders.effects[2].enabled && !pad.shaders.effects[1].enabled,
                "D-pad Left must toggle without sorting");
        press(SDL_GAMEPAD_BUTTON_NORTH);
        require(pad.selected == 1 && pad.shaders.effects[1].effect == shaders::Effect::ChromaticAberration &&
                    pad.shaders.effects[1].enabled && !pad.shaders.effects[2].enabled,
                "North must sort active effects first and retain the highlighted effect");
        press(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        pad.selected = 0;
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
static void sort_controls()
{
    Frontend ui;
    ui.page = Page::Shaders;
    ui.shaders = test_stack();
    ui.shaders.effects[1].enabled = true;
    ui.shaders.effects[4].enabled = true;
    const auto original = ui.shaders;
    key(ui, SDL_SCANCODE_LEFT);
    key(ui, SDL_SCANCODE_RIGHT);
    require(ui.shaders == original && ui.selected == 0, "Both arrows must toggle without sorting");
    key(ui, SDL_SCANCODE_S);
    auto expected = original;
    expected.effects = {original.effects[1], original.effects[4], original.effects[0], original.effects[2],
                        original.effects[3], original.effects[5]};
    require(ui.shaders == expected && ui.selected == 2,
            "Sort must preserve both groups' order and the selected inactive effect");
    key(ui, SDL_SCANCODE_S);
    require(ui.shaders == expected && ui.selected == 2, "Repeated sorting changed an already sorted stack");
    key(ui, SDL_SCANCODE_M);
    key(ui, SDL_SCANCODE_UP);
    const auto moving = ui.shaders;
    key(ui, SDL_SCANCODE_S);
    key(ui, SDL_SCANCODE_LEFT);
    require(ui.shaders == moving && ui.selected == 1, "Sorting and toggling must be ignored during a move");
    key(ui, SDL_SCANCODE_ESCAPE);
    require(ui.shaders == expected && ui.selected == 2, "Cancel did not restore a move across active effects");
    ui.selected = 0;
    key(ui, SDL_SCANCODE_M);
    key(ui, SDL_SCANCODE_UP);
    require(ui.selected == 0 && ui.shaders == expected, "Moving above the first effect must stop at the boundary");
    key(ui, SDL_SCANCODE_M);
    ui.selected = static_cast<int>(ui.shaders.effects.size()) - 1;
    key(ui, SDL_SCANCODE_M);
    key(ui, SDL_SCANCODE_DOWN);
    require(ui.selected == static_cast<int>(ui.shaders.effects.size()) - 1 && ui.shaders == expected,
            "Moving below the last effect must stop before Back");
    key(ui, SDL_SCANCODE_M);
    ui.shaders = original;
    ui.selected = static_cast<int>(ui.shaders.effects.size());
    key(ui, SDL_SCANCODE_S);
    key(ui, SDL_SCANCODE_RIGHT);
    require(ui.shaders == expected && ui.selected == static_cast<int>(ui.shaders.effects.size()),
            "Sorting must retain Back selection, and arrows on Back must leave effects unchanged");

    auto duplicates = original;
    auto second = duplicates.effects[0];
    second.parameters[0] = 20;
    duplicates.effects.push_back(second);
    const int selected = duplicates.sort(static_cast<int>(duplicates.effects.size()) - 1);
    require(selected == 6 && duplicates.effects[selected].parameters[0] == 20,
            "Sort must track the selected instance when effect types repeat");
}

static void film_controls()
{
    Frontend ui;
    ui.shaders = test_stack();
    require(ui.shaders.effects.size() == 6 && ui.shaders.effects[1].effect == shaders::Effect::FilmNoise,
            "Film grain is missing from the catalog");
    const auto defaults = ui.shaders.effects[1].parameters;
    ui.page = Page::Shaders;
    ui.selected = 1;
    key(ui, SDL_SCANCODE_RIGHT);
    require(ui.selected == 1 && ui.shaders.effects[1].enabled,
            "Enabling film grain must keep its position");
    key(ui, SDL_SCANCODE_RETURN);
    ui.selected = 0;
    key(ui, SDL_SCANCODE_RIGHT);
    require(ui.shaders.effects[1].parameters[0] == defaults[0] + 1, "Film grain did not use its own parameter step");
    key(ui, SDL_SCANCODE_RETURN);
    key(ui, SDL_SCANCODE_DOWN);
    key(ui, SDL_SCANCODE_RETURN);
    require(ui.shaders.effects[1].parameters[0] == defaults[0] + 2, "Film grain value selection failed");
    ui.selected = 1;
    key(ui, SDL_SCANCODE_RIGHT);
    require(ui.shaders.effects[1].parameters[1] == 1, "Hotspot toggle failed");
    ui.selected = 3;
    key(ui, SDL_SCANCODE_RETURN);
    require(ui.shaders.effects[1].parameters == defaults, "Restore defaults used the wrong effect");
    key(ui, SDL_SCANCODE_ESCAPE);
    ui.selected = 0;
    key(ui, SDL_SCANCODE_RIGHT);
    key(ui, SDL_SCANCODE_M);
    key(ui, SDL_SCANCODE_DOWN);
    key(ui, SDL_SCANCODE_RETURN);
    require(ui.shaders.effects[1].effect == shaders::Effect::ColorCorrection && ui.shaders.effects[0].enabled,
            "Mixed effect reorder failed");

    shaders::Stack settings;
    const auto before = settings;
    for (const auto* text : {"\"film_noise\" 1 0", "\"film_noise\" 1 7 1 2",
                             "\"film_noise\" 1 7 1 0 unexpected",
                             "\"film_noise\" 1 7 1 0 999999999999999999999999",
                             "\"film_noise\" 1 17 1 1", "\"unknown\" 1 0 0 0"})
    {
        std::istringstream invalid(text);
        settings.read(invalid);
        require(settings == before, "Invalid film grain settings modified the stack");
    }

    Frontend chromatic;
    chromatic.page = Page::Shaders;
    chromatic.selected = effect_index(chromatic.shaders, shaders::Effect::ChromaticAberration);
    require(chromatic.shaders.effects[chromatic.selected].effect == shaders::Effect::ChromaticAberration,
            "Chromatic aberration is missing from the catalog");
    const int chromatic_index = chromatic.selected;
    const auto chromatic_defaults = chromatic.shaders.effects[chromatic.selected].parameters;
    key(chromatic, SDL_SCANCODE_RIGHT);
    key(chromatic, SDL_SCANCODE_RETURN);
    chromatic.selected = 4;
    key(chromatic, SDL_SCANCODE_RIGHT);
    require(chromatic.shaders.effects[chromatic_index].parameters.size() == 6 &&
                chromatic.shaders.effects[chromatic_index].parameters[4] == chromatic_defaults[4] + 1,
            "Chromatic aberration did not expose independent RGB offsets");
    chromatic.selected = 6;
    key(chromatic, SDL_SCANCODE_RETURN);
    require(chromatic.shaders.effects[chromatic_index].parameters == chromatic_defaults &&
                !chromatic.shaders.effects[effect_index(chromatic.shaders, shaders::Effect::FilmNoise)].enabled,
            "Chromatic defaults affected film grain");

    Frontend vignette;
    vignette.page = Page::Shaders;
    vignette.selected = effect_index(vignette.shaders, shaders::Effect::Vignette);
    require(vignette.shaders.effects[vignette.selected].effect == shaders::Effect::Vignette && !vignette.shaders.active(),
            "Vignette must be a separate disabled effect");
    const int vignette_index = vignette.selected;
    key(vignette, SDL_SCANCODE_RIGHT);
    key(vignette, SDL_SCANCODE_RETURN);
    key(vignette, SDL_SCANCODE_LEFT);
    require(vignette.shaders.effects[vignette_index].parameters == std::vector<int>({95}),
            "Vignette strength adjustment failed");
    vignette.selected = 1;
    key(vignette, SDL_SCANCODE_RETURN);
    require(vignette.shaders.effects[vignette_index].parameters == std::vector<int>({100}) &&
                !vignette.shaders.effects[effect_index(vignette.shaders, shaders::Effect::FilmNoise)].enabled,
            "Vignette defaults affected film grain");
}

static void cas_controls()
{
    Frontend ui;
    ui.page = Page::Shaders;
    ui.selected = effect_index(ui.shaders, shaders::Effect::Cas);
    const int index = ui.selected;
    require(!ui.shaders.active() && ui.shaders.effects[ui.selected].parameters == std::vector<int>({0, 100}),
            "CAS defaults differ from the reference");
    key(ui, SDL_SCANCODE_RETURN);
    require(ui.page == Page::Shaders, "Inactive CAS controls were accessible");
    key(ui, SDL_SCANCODE_RIGHT);
    key(ui, SDL_SCANCODE_RETURN);
    key(ui, SDL_SCANCODE_RIGHT);
    require(ui.shaders.effects[index].parameters[0] == 5, "CAS contrast adjustment failed");
    ui.selected = 1;
    key(ui, SDL_SCANCODE_LEFT);
    require(ui.shaders.effects[index].parameters[1] == 95, "CAS intensity adjustment failed");
    ui.selected = 2;
    key(ui, SDL_SCANCODE_RETURN);
    require(ui.shaders.effects[index].parameters == std::vector<int>({0, 100}), "CAS restore defaults failed");
}

static void crt_controls()
{
    Frontend ui;
    ui.page = Page::Shaders;
    ui.selected = effect_index(ui.shaders, shaders::Effect::CrtGeom);
    const int index = ui.selected;
    const auto defaults = ui.shaders.effects[ui.selected].parameters;
    const auto& definition = shaders::definition(shaders::Effect::CrtGeom);
    require(defaults.size() == 18 && definition.output_resolution && !ui.shaders.active(),
            "CRT-Geom must be a disabled output-resolution effect with all 18 controls");
    require(shaders::parameter_value(definition.parameters[0], 24) == "2.4" &&
                shaders::parameter_value(definition.parameters[6], 30) == "0.030" &&
                shaders::parameter_value(definition.parameters[8], -5) == "-0.05 rad",
            "CRT floating-point controls were not formatted in their actual units");
    key(ui, SDL_SCANCODE_RIGHT);
    key(ui, SDL_SCANCODE_RETURN);
    key(ui, SDL_SCANCODE_RIGHT);
    require(ui.shaders.effects[index].parameters[0] == 25, "CRT gamma adjustment failed");
    ui.selected = 8;
    key(ui, SDL_SCANCODE_LEFT);
    require(ui.shaders.effects[index].parameters[8] == -5, "CRT tilt adjustment failed");
    ui.selected = 18;
    key(ui, SDL_SCANCODE_RETURN);
    require(ui.shaders.effects[index].parameters == defaults, "CRT restore defaults failed");
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
static std::array<int, 3> cas_reference(const std::vector<std::array<int, 3>>& image, int width, int height,
                                         int x, int y, int contrast, int sharpening)
{
    if (!sharpening)
    {
        return image[y * width + x];
    }
    std::array<std::array<double, 3>, 9> neighborhood{};
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            const auto& pixel = image[std::clamp(y + dy, 0, height - 1) * width + std::clamp(x + dx, 0, width - 1)];
            auto& sample = neighborhood[(dy + 1) * 3 + dx + 1];
            for (int channel = 0; channel < 3; ++channel)
            {
                const double value = pixel[channel] / 255.0;
                sample[channel] = value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
            }
        }
    }
    std::array<int, 3> result{};
    for (int channel = 0; channel < 3; ++channel)
    {
        std::array<double, 9> samples{};
        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            samples[i] = neighborhood[i][channel];
        }
        const double cross_min = std::min({samples[1], samples[3], samples[4], samples[5], samples[7]});
        const double cross_max = std::max({samples[1], samples[3], samples[4], samples[5], samples[7]});
        const double minimum = cross_min + *std::min_element(samples.begin(), samples.end());
        const double maximum = cross_max + *std::max_element(samples.begin(), samples.end());
        const double amplitude = std::clamp(std::min(minimum, 2 - maximum) / std::max(maximum, .0001), 0.0, 1.0);
        const double weight = amplitude > 0 ? -1 / ((1 / std::sqrt(amplitude)) * (8 - 3 * contrast / 100.0)) : 0;
        const double filtered = std::clamp(((samples[1] + samples[3] + samples[5] + samples[7]) * weight + samples[4]) /
                                               (1 + 4 * weight), 0.0, 1.0);
        const double value = samples[4] + (filtered - samples[4]) * sharpening / 100.0;
        const double encoded = value <= .0031308 ? value * 12.92 : 1.055 * std::pow(value, 1 / 2.4) - .055;
        result[channel] = static_cast<int>(std::round(std::clamp(encoded, 0.0, 1.0) * 255));
    }
    return result;
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
        film.parameters.assign(3, 0);
        auto& chromatic = stack.effects[2];
        auto& vignette_effect = stack.effects[3];
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
        const double distance = std::hypot(10.5 / 64 - .5, 20.5 / 64 - .5);
        const double t = std::clamp((distance - .25) / .5, 0.0, 1.0);
        const double factor = 1 - t * t * (3 - 2 * t);
        film.enabled = false;
        vignette_effect.enabled = true;
        vignette_effect.parameters[0] = 0;
        require(capture() == neutral, "Zero-strength vignette must preserve the input");
        vignette_effect.parameters[0] = 50;
        const auto half_vignette = capture();
        require(std::abs(half_vignette[sample] - std::round(neutral[sample] * (1 + factor) / 2)) <= 2,
                "Vignette strength did not scale the falloff");
        vignette_effect.parameters[0] = 100;
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
        vignette_effect.enabled = false;
        film.enabled = true;
        film.parameters[1] = 1;
        const auto hotspot = capture();
        require(std::abs(hotspot[sample] - std::round(neutral[sample] + factor * .2 * 255)) <= 2,
                "Hotspot differs from the reference");
        film.parameters[1] = 0;
        film.parameters[2] = 1;
        chain.reset();
        const auto scratches = capture();
        const auto clear_frame = capture();
        require(scratches != neutral && clear_frame == neutral, "Scratches must be occasional and independently switchable");
        film.parameters[2] = 0;
        film.enabled = false;
        vignette_effect.enabled = true;
        stack.effects[0].enabled = true;
        stack.effects[0].parameters[0] = 20;
        const auto color_first = capture();
        require(std::abs(color_first[sample] - std::round((neutral[sample] + 51) * factor)) <= 2,
                "Color correction followed by vignette used the wrong program");
        std::swap(stack.effects[0], stack.effects[3]);
        const auto vignette_first = capture();
        require(std::abs(vignette_first[sample] - (std::round(neutral[sample] * factor) + 51)) <= 2 &&
                    vignette_first != color_first,
                "Reordering different effects did not rebuild their programs");
        stack.effects[0].enabled = false;
        const auto color_only = capture();
        require(std::abs(color_only[sample] - (neutral[sample] + 51)) <= 1,
                "Replacing a vignette pass with color correction used a stale program");
        stack.effects[3].enabled = false;
        require(chain.draw(renderer, input, 64, 64, stack) == input && !chain.allocated(),
                "Disabling the mixed stack retained resources");
        SDL_DestroyTexture(input);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}

static void viewport_pixels(const char* driver)
{
    auto* window = SDL_CreateWindow("Viewport shaders", 320, 240, 0);
    require(window, SDL_GetError());
    auto* renderer = SDL_CreateRenderer(window, driver);
    require(renderer, SDL_GetError());
    {
        SecondaryWindow secondary;
        secondary.create(window, renderer, nullptr, nullptr, "Viewport shaders", 320, 240);
        std::array<GamePresentation, 2> presentations;
        shaders::Stack settings;
        settings.effects = {shaders::Shader(shaders::Effect::ChromaticAberration),
                            shaders::Shader(shaders::Effect::ColorCorrection), shaders::Shader(shaders::Effect::Vignette)};
        settings.effects[0].parameters[4] = 100;
        settings.effects[1].parameters[0] = 15;
        for (const auto* system : {"nds", "3ds"})
        {
            const unsigned width = std::string_view(system) == "nds" ? 64 : 80;
            const unsigned height = 96;
            const unsigned texture_width = width + 16, texture_height = height + 16;
            const auto screens = dual_screen::views(system, 320, 240);
            for (auto format : {SDL_PIXELFORMAT_RGBA32, SDL_PIXELFORMAT_RGB565})
            {
                for (bool flipped : {false, true})
                {
                    auto* input = SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_STREAMING,
                                                    texture_width, texture_height);
                    require(input, SDL_GetError());
                    SDL_SetTextureBlendMode(input, SDL_BLENDMODE_NONE);
                    std::vector<Uint8> image(texture_width * texture_height * 4);
                    for (unsigned y = 0; y < texture_height; ++y)
                    {
                        for (unsigned x = 0; x < texture_width; ++x)
                        {
                            const unsigned logical_y = (flipped && y < height) ? height - 1 - y : y;
                            const unsigned screen = logical_y < height / 2 ? 0 : 1;
                            const auto source = dual_screen::pixels(screens[screen].source, width, height);
                            const auto offset = (y * texture_width + x) * 4;
                            const bool visible = y < height && x >= source.x && x < source.x + source.w;
                            image[offset] = visible ? 40 + x - static_cast<unsigned>(source.x) : 255;
                            image[offset + 1] = visible ? 60 + logical_y - static_cast<unsigned>(source.y) : 0;
                            image[offset + 2] = visible ? 100 + screen * 40 + x - static_cast<unsigned>(source.x) : 255;
                            image[offset + 3] = 255;
                        }
                    }
                    const int pitch = texture_width * SDL_BYTESPERPIXEL(format);
                    std::vector<Uint8> upload(pitch * texture_height);
                    require(SDL_ConvertPixels(texture_width, texture_height, SDL_PIXELFORMAT_RGBA32, image.data(),
                                               texture_width * 4, format, upload.data(), pitch), SDL_GetError());
                    require(SDL_ConvertPixels(texture_width, texture_height, format, upload.data(), pitch,
                                               SDL_PIXELFORMAT_RGBA32, image.data(), texture_width * 4), SDL_GetError());
                    require(SDL_UpdateTexture(input, nullptr, upload.data(), pitch), SDL_GetError());
                    secondary.upload(upload.data(), texture_width, texture_height, pitch, format);
                    // End with shaders disabled to verify the original crop/flip presentation path too.
                    for (int mode : {1, 2, 0})
                    {
                        settings.effects[0].enabled = settings.effects[1].enabled = mode == 2;
                        settings.effects[2].enabled = mode != 0;
                        for (int scaling : {0, 2, 3})
                        {
                            for (unsigned screen = 0; screen < 2; ++screen)
                            {
                                auto* target = screen ? secondary.renderer : renderer;
                                if (screen)
                                {
                                    secondary.begin_draw();
                                }
                                require(SDL_SetRenderDrawColor(target, 0, 0, 0, 255), SDL_GetError());
                                require(SDL_RenderClear(target), SDL_GetError());
                                int output_width, output_height;
                                require(SDL_GetRenderOutputSize(target, &output_width, &output_height), SDL_GetError());
                                GamePresentation::Image frame;
                                frame.texture = screen ? secondary.texture(input) : input;
                                frame.width = width;
                                frame.height = height;
                                frame.scaling = scaling;
                                frame.flipped = flipped;
                                frame.shaders = &settings;
                                const auto view = dual_screen::window_view(system, screen, output_width, output_height);
                                const auto crop = dual_screen::pixels(view.source, width, height);
                                const auto destination = GamePresentation::screen_viewport(frame, view);
                                presentations[screen].draw(target, frame, destination, &crop);
                                auto* surface = SDL_RenderReadPixels(target, nullptr);
                                require(surface, SDL_GetError());
                                for (float position : {.125f, .375f, .5f, .625f, .875f})
                                {
                                    const int x = static_cast<int>(destination.x + position * destination.w);
                                    const int y = static_cast<int>(destination.y + position * destination.h);
                                    const int local_x = static_cast<int>((x + .5f - destination.x) / destination.w * crop.w);
                                    const int local_y = static_cast<int>((y + .5f - destination.y) / destination.h * crop.h);
                                    const unsigned source_y = static_cast<unsigned>(crop.y) + local_y;
                                    const unsigned storage_y = flipped ? height - 1 - source_y : source_y;
                                    const unsigned source_x = static_cast<unsigned>(crop.x) + local_x;
                                    std::array<int, 3> expected{};
                                    for (int channel = 0; channel < 3; ++channel)
                                    {
                                        unsigned channel_x = source_x;
                                        if (mode == 2 && channel == 2)
                                        {
                                            const auto shifted = std::min(static_cast<int>(crop.w) - 1,
                                                                          static_cast<int>(local_x + .5f + .01f * crop.w));
                                            channel_x = static_cast<unsigned>(crop.x) + shifted;
                                        }
                                        expected[channel] = image[(storage_y * texture_width + channel_x) * 4 + channel];
                                    }
                                    if (mode == 2)
                                    {
                                        expected = corrected(expected, settings.effects[1]);
                                    }
                                    if (mode != 0)
                                    {
                                        const double distance = std::hypot((local_x + .5) / crop.w - .5,
                                                                            (local_y + .5) / crop.h - .5);
                                        const double t = std::clamp((distance - .25) / .5, 0.0, 1.0);
                                        const double factor = 1 - t * t * (3 - 2 * t);
                                        for (auto& channel : expected)
                                        {
                                            channel = static_cast<int>(std::round(channel * factor));
                                        }
                                    }
                                    Uint8 r, g, b, a;
                                    require(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a), SDL_GetError());
                                    if (std::abs(int(r) - expected[0]) > 3 || std::abs(int(g) - expected[1]) > 3 ||
                                        std::abs(int(b) - expected[2]) > 3)
                                    {
                                        std::cerr << driver << ' ' << system << " screen " << screen << " flipped " << flipped
                                                  << " mode " << mode << " scaling " << scaling << " position " << position
                                                  << " got " << int(r) << ',' << int(g) << ',' << int(b) << " expected "
                                                  << expected[0] << ',' << expected[1] << ',' << expected[2] << '\n';
                                        throw std::runtime_error("Shaders did not use viewport-local coordinates and sampling");
                                    }
                                }
                                SDL_DestroySurface(surface);
                                require(SDL_RenderPresent(target), SDL_GetError());
                                if (screen)
                                {
                                    secondary.end_draw();
                                }
                            }
                        }
                    }
                    for (auto& presentation : presentations)
                    {
                        presentation.reset();
                    }
                    SDL_DestroyTexture(input);
                }
            }
        }
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}

static void cas_pixels(const char* driver)
{
    auto* window = SDL_CreateWindow("CAS pixels", 64, 64, 0);
    require(window, SDL_GetError());
    auto* renderer = SDL_CreateRenderer(window, driver);
    require(renderer, SDL_GetError());
    bool saw_sharpening = false, saw_contrast_adaptation = false;
    {
        GamePresentation presentation;
        const unsigned frame_width = 20, frame_height = 24, texture_width = 28, texture_height = 32;
        for (const auto size : {SDL_Point{9, 7}, SDL_Point{1, 1}})
        {
            const SDL_FRect crop{3, 8, float(size.x), float(size.y)};
            for (auto format : {SDL_PIXELFORMAT_RGBA32, SDL_PIXELFORMAT_RGB565})
            {
                for (bool flipped : {false, true})
                {
                    auto* input = SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_STREAMING,
                                                    texture_width, texture_height);
                    require(input, SDL_GetError());
                    SDL_SetTextureBlendMode(input, SDL_BLENDMODE_NONE);
                    for (int pattern = 0; pattern < 5; ++pattern)
                    {
                        std::vector<Uint8> pixels(texture_width * texture_height * 4);
                        for (unsigned i = 0; i < texture_width * texture_height; ++i)
                        {
                            pixels[i * 4] = pixels[i * 4 + 2] = pixels[i * 4 + 3] = 255;
                        }
                        for (int y = 0; y < size.y; ++y)
                        {
                            for (int x = 0; x < size.x; ++x)
                            {
                                const unsigned storage_y = flipped ? frame_height - 1 - (8 + y) : 8 + y;
                                for (int channel = 0; channel < 3; ++channel)
                                {
                                    const int value = pattern == 0 ? 0 : pattern == 1 ? 255 : pattern == 2 ? 128
                                                      : pattern == 3 ? channel * 127
                                                                     : 32 + ((x * 3 + y * 5 + channel * 2) % 7) * 32;
                                    pixels[(storage_y * texture_width + 3 + x) * 4 + channel] = value;
                                }
                            }
                        }
                        const int pitch = texture_width * SDL_BYTESPERPIXEL(format);
                        std::vector<Uint8> upload(pitch * texture_height);
                        require(SDL_ConvertPixels(texture_width, texture_height, SDL_PIXELFORMAT_RGBA32, pixels.data(),
                                                   texture_width * 4, format, upload.data(), pitch), SDL_GetError());
                        require(SDL_ConvertPixels(texture_width, texture_height, format, upload.data(), pitch,
                                                   SDL_PIXELFORMAT_RGBA32, pixels.data(), texture_width * 4), SDL_GetError());
                        require(SDL_UpdateTexture(input, nullptr, upload.data(), pitch), SDL_GetError());
                        std::vector<std::array<int, 3>> image(size.x * size.y);
                        for (int y = 0; y < size.y; ++y)
                        {
                            for (int x = 0; x < size.x; ++x)
                            {
                                const unsigned storage_y = flipped ? frame_height - 1 - (8 + y) : 8 + y;
                                for (int channel = 0; channel < 3; ++channel)
                                {
                                    image[y * size.x + x][channel] = pixels[(storage_y * texture_width + 3 + x) * 4 + channel];
                                }
                            }
                        }
                        for (int order = 0; order < 3; ++order)
                        {
                            shaders::Stack settings;
                            settings.effects = {shaders::Shader(shaders::Effect::Cas),
                                                shaders::Shader(shaders::Effect::ColorCorrection)};
                            settings.effects[0].enabled = true;
                            settings.effects[1].enabled = order != 0;
                            settings.effects[1].parameters[0] = 20;
                            if (order == 2)
                            {
                                std::swap(settings.effects[0], settings.effects[1]);
                            }
                            const auto cas = effect_index(settings, shaders::Effect::Cas);
                            const auto color = effect_index(settings, shaders::Effect::ColorCorrection);
                            auto neighborhood = image;
                            if (order == 2)
                            {
                                for (auto& pixel : neighborhood)
                                {
                                    pixel = corrected(pixel, settings.effects[color]);
                                }
                            }
                            std::vector<std::array<int, 3>> default_pixels;
                            for (const auto parameters : {std::array<int, 2>{0, 0}, {0, 100}, {100, 100}, {50, 35}})
                            {
                                settings.effects[cas].parameters = {parameters[0], parameters[1]};
                                GamePresentation::Image frame;
                                frame.texture = input;
                                frame.width = frame_width;
                                frame.height = frame_height;
                                frame.scaling = 0;
                                frame.flipped = flipped;
                                frame.shaders = &settings;
                                presentation.draw(renderer, frame, {0, 0, float(size.x), float(size.y)}, &crop);
                                auto* surface = SDL_RenderReadPixels(renderer, nullptr);
                                require(surface, SDL_GetError());
                                std::vector<std::array<int, 3>> actual(size.x * size.y);
                                for (int y = 0; y < size.y; ++y)
                                {
                                    for (int x = 0; x < size.x; ++x)
                                    {
                                        auto expected = cas_reference(neighborhood, size.x, size.y, x, y,
                                                                       parameters[0], parameters[1]);
                                        if (order == 1)
                                        {
                                            expected = corrected(expected, settings.effects[color]);
                                        }
                                        Uint8 r, g, b, a;
                                        require(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a), SDL_GetError());
                                        actual[y * size.x + x] = {r, g, b};
                                        for (int channel = 0; channel < 3; ++channel)
                                        {
                                            if (std::abs(actual[y * size.x + x][channel] - expected[channel]) > 3)
                                            {
                                                std::cerr << driver << " CAS pattern " << pattern << " order " << order
                                                          << " flipped " << flipped << " contrast " << parameters[0]
                                                          << " intensity " << parameters[1] << " pixel " << x << ',' << y
                                                          << " channel " << channel << " got " << actual[y * size.x + x][channel]
                                                          << " expected " << expected[channel] << '\n';
                                                throw std::runtime_error("CAS differs from the linear-light reference");
                                            }
                                        }
                                    }
                                }
                                SDL_DestroySurface(surface);
                                require(SDL_RenderPresent(renderer), SDL_GetError());
                                if (order == 0 && pattern == 4 && size.x > 1)
                                {
                                    saw_sharpening |= parameters[1] > 0 && actual != image;
                                    if (parameters == std::array<int, 2>{0, 100})
                                    {
                                        default_pixels = actual;
                                    }
                                    if (parameters == std::array<int, 2>{100, 100})
                                    {
                                        saw_contrast_adaptation |= actual != default_pixels;
                                    }
                                }
                            }
                        }
                    }
                    presentation.reset();
                    SDL_DestroyTexture(input);
                }
            }
        }
    }
    require(saw_sharpening && saw_contrast_adaptation, "CAS controls did not change detailed input");
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}

static std::array<int, 3> crt_reference(const std::vector<std::array<int, 3>>& input, const std::vector<int>& parameters,
                                              SDL_Point input_size, SDL_Point output_size, int x, int y)
{
    const double u = (x + .5) / output_size.x, v = (y + .5) / output_size.y;
    const double ox = parameters[10] / 100.0, oy = parameters[11] / 100.0;
    if (!ox || !oy)
    {
        return {};
    }
    const double mapped_u = (u - .5) / ox + .5, mapped_v = (v - .5) / oy + .5;
    if (mapped_u < 0 || mapped_u > 1 || mapped_v < 0 || mapped_v > 1)
    {
        return {};
    }
    const double corner = parameters[6] / 1000.0;
    const double cx = corner - std::min(std::min(u, 1 - u), corner);
    const double cy = corner - std::min(std::min(v, 1 - v) * output_size.y / output_size.x, corner);
    const double border = std::clamp((corner - std::hypot(cx, cy)) * parameters[7], 0.0, 1.0);
    const double ratio_x = mapped_u * input_size.x - .5;
    const double ratio_y = mapped_v * input_size.y - .5;
    const double fraction_x = ratio_x - std::floor(ratio_x);
    const double fraction = ratio_y - std::floor(ratio_y);
    const int row = static_cast<int>(std::floor(ratio_y));
    const int column = static_cast<int>(std::floor(ratio_x));
    const double pi = 3.141592653589;
    std::array<double, 4> coefficients{pi * (1 + fraction_x), pi * fraction_x,
                                       pi * (1 - fraction_x), pi * (2 - fraction_x)};
    double sum = 0;
    for (auto& coefficient : coefficients)
    {
        coefficient = std::max(std::abs(coefficient), .0001);
        coefficient = 2 * std::sin(coefficient) * std::sin(coefficient / 2) / (coefficient * coefficient);
        sum += coefficient;
    }
    for (auto& coefficient : coefficients)
    {
        coefficient /= sum;
    }
    const double filter = double(input_size.y) / output_size.y / 3;
    const double weight = parameters[14] / 100.0, luminance = parameters[15] / 100.0;
    std::array<double, 3> result{};
    for (int channel = 0; channel < 3; ++channel)
    {
        const auto sample = [&](double x, int y)
        {
            const int left = static_cast<int>(std::floor(x));
            const int a = input[std::clamp(y, 0, input_size.y - 1) * input_size.x +
                                std::clamp(left, 0, input_size.x - 1)][channel];
            const int b = input[std::clamp(y, 0, input_size.y - 1) * input_size.x +
                                std::clamp(left + 1, 0, input_size.x - 1)][channel];
            return std::pow((a + (b - a) * (x - std::floor(x))) / 255.0, parameters[0] / 10.0);
        };
        double current_color = 0, next_color = 0;
        for (int tap = 0; tap < 4; ++tap)
        {
            const double x = column + double(tap - 1) / parameters[13];
            current_color += sample(x, row) * coefficients[tap];
            next_color += sample(x, row + 1) * coefficients[tap];
        }
        current_color = std::clamp(current_color, 0.0, 1.0);
        next_color = std::clamp(next_color, 0.0, 1.0);
        const auto beam = [&](double distance, double color)
        {
            const double width = 2 + 2 * std::pow(color, 4);
            return (luminance + 1.4) * std::exp(-std::pow(distance / weight / std::sqrt(.5 * width), width)) /
                   (.6 + .2 * width);
        };
        const double current = (beam(fraction, current_color) + beam(fraction + filter, current_color) +
                                beam(std::abs(fraction - filter), current_color)) / 3;
        const double next = (beam(1 - fraction, next_color) + beam(std::abs(1 - fraction - filter), next_color) +
                             beam(std::abs(1 - fraction + filter), next_color)) / 3;
        const bool dimmed = (x % 2 == 0) ? channel == 1 : channel != 1;
        const double mask = dimmed ? 1 - parameters[12] / 100.0 : 1;
        double value = (current_color * current + next_color * next) * border * mask;
        if (parameters[1])
        {
            value = std::clamp(value, 0.0, 1.0);
            const double power = 1 / ((-.7 * (1 - weight) + 1) * (1 - .5 * parameters[12] / 100.0)) - 1.25;
            const double circle = std::sqrt(std::max(0.0, 1 - (value - 1) * (value - 1)));
            value = std::sqrt(value) + (circle - std::sqrt(value)) * power;
        }
        else
        {
            value = std::pow(std::max(0.0, value), 10.0 / parameters[2]);
        }
        result[channel] = value;
    }
    const bool dark = std::sqrt(result[0] * result[0] + result[1] * result[1] + result[2] * result[2]) * .5775 < .5;
    const double gray = dark ? result[0] * .18 + result[1] * .72 + result[2] * .02
                             : result[0] * .3 + result[1] * .6 + result[2] * .1;
    std::array<int, 3> output{};
    for (int channel = 0; channel < 3; ++channel)
    {
        output[channel] = static_cast<int>(std::round(std::clamp(gray + (result[channel] - gray) * parameters[17] / 100.0,
                                                                0.0, 1.0) * 255));
    }
    return output;
}

static void crt_pixels(const char* driver)
{
    auto* window = SDL_CreateWindow("CRT-Geom pixels", 128, 512, 0);
    require(window, SDL_GetError());
    auto* renderer = SDL_CreateRenderer(window, driver);
    require(renderer, SDL_GetError());
    {
        GamePresentation presentation;
        shaders::Stack settings;
        settings.effects = {shaders::Shader(shaders::Effect::CrtGeom)};
        settings.effects[0].enabled = true;
        GamePresentation::Image frame;
        frame.shaders = &settings;
        frame.scaling = 2;
        auto capture = [&](const SDL_FRect& crop, SDL_Point output_size)
        {
            require(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255), SDL_GetError());
            require(SDL_RenderClear(renderer), SDL_GetError());
            presentation.draw(renderer, frame, {0, 0, float(output_size.x), float(output_size.y)}, &crop);
            const bool output_effect = std::any_of(settings.effects.begin(), settings.effects.end(),
                [](const shaders::Shader& shader)
                { return shader.enabled && shaders::definition(shader.effect).output_resolution; });
            require(!output_effect || presentation.intermediate_size().x == 0,
                    "CRT output was enlarged again after rendering");
            auto* surface = SDL_RenderReadPixels(renderer, nullptr);
            require(surface, SDL_GetError());
            std::vector<std::array<int, 3>> result(output_size.x * output_size.y);
            for (int y = 0; y < output_size.y; ++y)
            {
                for (int x = 0; x < output_size.x; ++x)
                {
                    Uint8 r, g, b, a;
                    require(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a), SDL_GetError());
                    result[y * output_size.x + x] = {r, g, b};
                }
            }
            SDL_DestroySurface(surface);
            require(SDL_RenderPresent(renderer), SDL_GetError());
            return result;
        };
        const SDL_FRect crop{3, 8, 8, 6};
        const SDL_Point output_size{64, 48};
        frame.width = 20;
        frame.height = 24;
        for (auto format : {SDL_PIXELFORMAT_RGBA32, SDL_PIXELFORMAT_RGB565})
        {
            for (bool flipped : {false, true})
            {
                frame.flipped = flipped;
                auto* input = SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_STREAMING, 28, 32);
                require(input, SDL_GetError());
                SDL_SetTextureBlendMode(input, SDL_BLENDMODE_NONE);
                frame.texture = input;
                for (int value : {0, 128, 255, -1})
                {
                    std::vector<Uint8> pixels(28 * 32 * 4);
                    for (int i = 0; i < 28 * 32; ++i)
                    {
                        pixels[i * 4] = pixels[i * 4 + 2] = pixels[i * 4 + 3] = 255;
                    }
                    for (int y = 8; y < 14; ++y)
                    {
                        const int storage_y = flipped ? 23 - y : y;
                        for (int x = 3; x < 11; ++x)
                        {
                            for (int channel = 0; channel < 3; ++channel)
                            {
                                pixels[(storage_y * 28 + x) * 4 + channel] = value < 0
                                    ? 32 + (((x - 3) * 3 + (y - 8) * 5 + channel * 2) % 7) * 32 : value;
                            }
                        }
                    }
                    const int pitch = 28 * SDL_BYTESPERPIXEL(format);
                    std::vector<Uint8> upload(pitch * 32);
                    require(SDL_ConvertPixels(28, 32, SDL_PIXELFORMAT_RGBA32, pixels.data(), 28 * 4,
                                               format, upload.data(), pitch), SDL_GetError());
                    require(SDL_ConvertPixels(28, 32, format, upload.data(), pitch,
                                               SDL_PIXELFORMAT_RGBA32, pixels.data(), 28 * 4), SDL_GetError());
                    require(SDL_UpdateTexture(input, nullptr, upload.data(), pitch), SDL_GetError());
                    std::vector<std::array<int, 3>> image(8 * 6);
                    for (int y = 0; y < 6; ++y)
                    {
                        const int storage_y = flipped ? 15 - y : 8 + y;
                        for (int x = 0; x < 8; ++x)
                        {
                            for (int channel = 0; channel < 3; ++channel)
                            {
                                image[y * 8 + x][channel] = pixels[(storage_y * 28 + 3 + x) * 4 + channel];
                            }
                        }
                    }
                    for (int preset = 0; preset < 7; ++preset)
                    {
                        settings.effects = {shaders::Shader(shaders::Effect::CrtGeom)};
                        settings.effects[0].enabled = true;
                        auto& parameters = settings.effects[0].parameters;
                        parameters[4] = 0;
                        parameters[6] = 1;
                        if (preset == 1)
                        {
                            parameters[12] = 0;
                        }
                        if (preset == 2)
                        {
                            parameters[17] = 0;
                        }
                        if (preset == 3)
                        {
                            parameters[1] = 1;
                        }
                        if (preset == 4)
                        {
                            parameters[14] = 45;
                            parameters[15] = 20;
                        }
                        if (preset == 5)
                        {
                            parameters[0] = 18;
                            parameters[2] = 24;
                            parameters[10] = -100;
                            parameters[12] = 70;
                        }
                        if (preset == 6)
                        {
                            parameters[10] = 110;
                            parameters[11] = 95;
                            parameters[13] = 3;
                        }
                        const auto actual = capture(crop, output_size);
                        for (int y = 0; y < output_size.y; ++y)
                        {
                            for (int x = 0; x < output_size.x; ++x)
                            {
                                const auto expected = crt_reference(image, parameters, {8, 6}, output_size, x, y);
                                for (int channel = 0; channel < 3; ++channel)
                                {
                                    if (std::abs(actual[y * output_size.x + x][channel] - expected[channel]) > 3)
                                    {
                                        std::cerr << driver << " CRT preset " << preset << " input " << value
                                                  << " flipped " << flipped << " pixel " << x << ',' << y
                                                  << " channel " << channel << " got " << actual[y * output_size.x + x][channel]
                                                  << " expected " << expected[channel] << '\n';
                                        throw std::runtime_error("CRT scanline/mask/gamma output differs from the reference");
                                    }
                                }
                            }
                        }
                    }
                    if (format == SDL_PIXELFORMAT_RGBA32 && !flipped && value == 128)
                    {
                        settings.effects = {shaders::Shader(shaders::Effect::CrtGeom)};
                        settings.effects[0].enabled = true;
                        auto& parameters = settings.effects[0].parameters;
                        parameters[12] = 0;
                        const auto curved = capture(crop, output_size);
                        require(curved[0][0] == 0 && curved[24 * 64 + 32][0] > 0,
                                "CRT curvature must keep a visible center and black corners");
                        for (int y = 0; y < 48; ++y)
                        {
                            for (int x = 0; x < 64; ++x)
                            {
                                require(std::abs(curved[y * 64 + x][0] - curved[y * 64 + 63 - x][0]) <= 3 &&
                                            std::abs(curved[y * 64 + x][0] - curved[(47 - y) * 64 + x][0]) <= 3,
                                        "Zero-tilt CRT geometry must be centered and symmetric");
                            }
                        }
                        const auto resized = capture(crop, {63, 47});
                        require(resized[23 * 63 + 31][0] > 0 && resized[0][0] == 0,
                                "CRT resize or exact center projection failed");
                        parameters[8] = 25;
                        require(capture(crop, output_size) != curved, "CRT tilt had no effect");
                        parameters[8] = 0;
                        parameters[4] = 0;
                        parameters[10] = 0;
                        const auto blank = capture(crop, output_size);
                        require(std::all_of(blank.begin(), blank.end(), [](const auto& pixel)
                                             { return pixel == std::array<int, 3>{0, 0, 0}; }),
                                "Zero overscan must blank the screen without invalid arithmetic");
                        parameters[10] = 100;
                        parameters[6] = 1;
                        parameters[12] = 30;
                        const auto crt_only = capture(crop, output_size);
                        settings.effects.emplace_back(shaders::Effect::Cas);
                        settings.effects[1].enabled = true;
                        settings.effects[1].parameters = {100, 100};
                        const auto with_cas = capture(crop, output_size);
                        for (int y = 0; y < 48; ++y)
                        {
                            for (int x = 0; x < 64; ++x)
                            {
                                const auto expected = cas_reference(crt_only, 64, 48, x, y, 100, 100);
                                for (int channel = 0; channel < 3; ++channel)
                                {
                                    require(std::abs(with_cas[y * 64 + x][channel] - expected[channel]) <= 3,
                                            "An effect after CRT did not preserve display resolution");
                                }
                            }
                        }
                        std::swap(settings.effects[0], settings.effects[1]);
                        const auto cas_first = capture(crop, output_size);
                        for (std::size_t pixel = 0; pixel < crt_only.size(); ++pixel)
                        {
                            require(std::abs(cas_first[pixel][0] - crt_only[pixel][0]) <= 3,
                                    "An effect before CRT changed flat input unexpectedly");
                        }
                        settings.effects[1].enabled = false;
                        const auto source_only = capture(crop, output_size);
                        require(presentation.intermediate_size().x == 64 &&
                                    std::all_of(source_only.begin(), source_only.end(), [](const auto& pixel)
                                                { return std::abs(pixel[0] - 128) <= 1; }),
                                "Disabling CRT did not restore source-resolution scaling");
                    }
                }
                presentation.reset();
                SDL_DestroyTexture(input);
            }
        }
        // Alternate fields must depend on source height, not the composite or output height.
        auto* fields = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 12, 408);
        require(fields, SDL_GetError());
        SDL_SetTextureBlendMode(fields, SDL_BLENDMODE_NONE);
        std::vector<Uint8> pixels(12 * 408 * 4, 255);
        for (int y = 0; y < 400; ++y)
        {
            for (int x = 2; x < 6; ++x)
            {
                for (int channel = 0; channel < 3; ++channel)
                {
                    pixels[((y + 3) * 12 + x) * 4 + channel] = y % 2 ? 0 : 255;
                }
            }
        }
        require(SDL_UpdateTexture(fields, nullptr, pixels.data(), 12 * 4), SDL_GetError());
        settings.effects = {shaders::Shader(shaders::Effect::CrtGeom)};
        settings.effects[0].enabled = true;
        settings.effects[0].parameters[4] = 0;
        settings.effects[0].parameters[6] = 1;
        settings.effects[0].parameters[12] = 0;
        frame.texture = fields;
        frame.width = 8;
        frame.height = 406;
        frame.flipped = false;
        const auto first = capture({2, 3, 4, 400}, {64, 480});
        const auto second = capture({2, 3, 4, 400}, {64, 480});
        require(first[240 * 64 + 32][0] > 32 && second[240 * 64 + 32][0] == 0,
                "CRT interlacing did not alternate even and odd source fields");
        settings.effects[0].parameters[16] = 0;
        const auto progressive = capture({2, 3, 4, 400}, {64, 480});
        require(capture({2, 3, 4, 400}, {64, 480}) == progressive, "Disabling CRT interlacing did not stop field alternation");
        presentation.reset();
        SDL_DestroyTexture(fields);
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
        sort_controls();
        film_controls();
        cas_controls();
        crt_controls();
        if (argc > 1)
        {
            pixels(argv[1]);
            film_pixels(argv[1]);
            viewport_pixels(argv[1]);
            cas_pixels(argv[1]);
            crt_pixels(argv[1]);
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
