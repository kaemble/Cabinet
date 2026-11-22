#include "core_options.h"
#include "frontend.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <tuple>

static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static void key(Frontend& ui, SDL_Scancode code) {
    SDL_Event event{}; event.type = SDL_EVENT_KEY_DOWN; event.key.scancode = code; ui.event(event);
}
static void deadzone_defaults() {
    for (const auto& [name, zero, nonzero] : {
            std::tuple{"citra_analog_deadzone", "0", "15"},
            std::tuple{"reicast_analog_stick_deadzone", "0%", "15%"},
            std::tuple{"reicast_trigger_deadzone", "0%", "15%"},
            std::tuple{"ppsspp_analog_deadzone", "0.0", "0.15"}}) {
        for (int api = 0; api < 3; ++api) {
            Frontend ui; CoreOptions options;
            const std::string legacy_value = std::string("Deadzone; ") + nonzero + '|' + zero;
            retro_variable legacy[]{{name, legacy_value.c_str()}, {nullptr, nullptr}};
            retro_core_option_definition v1[2]{};
            v1[0].key = name; v1[0].desc = "Deadzone"; v1[0].default_value = nonzero;
            v1[0].values[0] = {nonzero, nullptr}; v1[0].values[1] = {zero, "0%"};
            retro_core_option_v2_definition v2[2]{};
            v2[0].key = name; v2[0].desc = "Deadzone"; v2[0].default_value = nonzero;
            v2[0].values[0] = v1[0].values[0]; v2[0].values[1] = v1[0].values[1];
            retro_core_options_v2 modern{nullptr, v2};
            auto register_options = [&] {
                if (api == 0) options.register_variables(legacy, ui);
                else if (api == 1) options.register_options(v1, ui);
                else options.register_options(&modern, ui);
            };
            register_options();
            require(options.defaults.at(name) == zero && options.values.at(name) == zero,
                    "Core deadzone must default to zero");
            options.values[name] = nonzero;
            register_options();
            require(options.values.at(name) == nonzero, "Explicit core deadzone override was replaced");
            options.restore_values({}, ui);
            require(options.values.at(name) == zero, "Reset must restore zero core deadzone");
        }
    }
}
static void normalized_catalog()
{
    retro_core_option_definition definitions[8]{};
    definitions[0].key = "fixture_complex"; definitions[0].desc = "Name; with | separators";
    definitions[0].info = "Literal values and labels";
    definitions[0].values[0] = {"left|right", "Both sides"};
    definitions[0].values[1] = {"semi; colon", "Déjà vu; |"};
    definitions[0].values[2] = {"", "Empty value"};
    definitions[0].default_value = "semi; colon";
    definitions[1].key = "fixture_invalid_default";
    definitions[1].values[0] = {"first", nullptr}; definitions[1].values[1] = {"second", nullptr};
    definitions[1].default_value = "missing";
    definitions[2].key = "mupen64plus-rdp-plugin"; definitions[2].default_value = "parallel";
    definitions[2].values[0] = {"angrylion", nullptr}; definitions[2].values[1] = {"gliden64", nullptr};
    definitions[2].values[2] = {"parallel", nullptr};
    definitions[3].key = "mupen64plus-rsp-plugin"; definitions[3].default_value = "hle";
    definitions[3].values[0] = {"hle", nullptr}; definitions[3].values[1] = {"parallel", nullptr};
    definitions[4].key = "mupen64plus-43screensize"; definitions[4].default_value = "1280x960";
    definitions[4].values[0] = {"1280x960", "Double"}; definitions[4].values[1] = {"640x480", "Native"};
    definitions[5].key = "mupen64plus-parallel-rdp-upscaling"; definitions[5].default_value = "2x";
    definitions[5].values[0] = {"4x", nullptr}; definitions[5].values[1] = {"1x", nullptr}; definitions[5].values[2] = {"2x", nullptr};
    definitions[6].key = "fixture_no_values";
    for (int api : {1, 2})
    {
        Frontend ui; CoreOptions options;
        options.values["fixture_complex"] = "invalid saved value";
        options.values["fixture_invalid_default"] = "second";
        retro_core_option_v2_definition v2[8]{};
        for (int i = 0; i < 7; ++i)
        {
            v2[i].key = definitions[i].key; v2[i].desc = definitions[i].desc;
            v2[i].info = definitions[i].info; v2[i].default_value = definitions[i].default_value;
            std::copy(std::begin(definitions[i].values), std::end(definitions[i].values), std::begin(v2[i].values));
        }
        retro_core_options_v2 modern{nullptr, v2};
        auto registerOptions = [&]
        {
            if (api == 1) options.register_options(definitions, ui);
            else options.register_options(&modern, ui);
        };
        registerOptions();
        require(ui.all_core_options.size() == 6 && ui.all_core_options[0].name == definitions[0].desc &&
                ui.all_core_options[0].description == definitions[0].info &&
                ui.all_core_options[0].values == std::vector<std::string>{"left|right", "semi; colon", ""} &&
                ui.all_core_options[0].labels == std::vector<std::string>{"Both sides", "Déjà vu; |", "Empty value"},
                "Native option strings, enum order or labels were lost during normalization");
        require(options.values.at("fixture_complex") == "semi; colon" && ui.all_core_options[0].index == 1 &&
                options.defaults.at("fixture_invalid_default") == "first" &&
                options.values.at("fixture_invalid_default") == "second" &&
                ui.all_core_options[1].name == "fixture_invalid_default",
                "Invalid defaults/overrides or missing labels were not normalized consistently");
        require(options.values.at("mupen64plus-rsp-plugin") == "parallel" && ui.core_choices[1].index == 1,
                "Modern Mupen defaults did not normalize the RDP/RSP dependency");
        require(options.resolution_key == "mupen64plus-parallel-rdp-upscaling" &&
                ui.resolution_values == std::vector<std::string>{"1x", "2x", "4x"} && ui.resolution_index == 1 &&
                options.resolutions.at("mupen64plus-43screensize") == std::vector<std::string>{"640x480", "1280x960"},
                "Resolution sorting or inactive renderer resolution was lost");
        require(options.set_value({"fixture_complex", ""}, ui), "Advertised empty enum value was rejected");
        registerOptions();
        require(options.values.at("fixture_complex").empty() && ui.all_core_options[0].index == 2,
                "Re-registration did not preserve an exact configured enum value");
        options.runtime_overrides["fixture_complex"] = "left|right";
        registerOptions();
        retro_variable effective{"fixture_complex", nullptr};
        require(!ui.all_core_options[0].visible && options.get_variable(effective) &&
                std::string(effective.value) == "left|right", "Runtime override visibility or effective value changed");
        const auto saved = options.persistent_values(ui);
        require(std::find(saved.begin(), saved.end(), std::pair<std::string, std::string>{"fixture_complex", ""}) != saved.end(),
                "Runtime override replaced the user's persisted value");
        options.register_options(static_cast<const retro_core_option_definition*>(nullptr), ui);
        require(ui.all_core_options.empty() && ui.core_choices.empty() && options.defaults.empty() &&
                options.resolutions.empty() && options.resolution_key.empty() && !ui.resolution_needs_restart,
                "Replacing the option catalog retained stale definitions or derived menus");
    }
}
static void melonds_defaults() {
    retro_core_option_v2_definition definitions[5]{};
    definitions[0].key = "melonds_screen_layout1";
    definitions[0].values[0] = {"top-bottom", nullptr}; definitions[0].default_value = "top-bottom";
    definitions[1].key = "melonds_render_mode";
    definitions[1].values[0] = {"software", nullptr}; definitions[1].values[1] = {"opengl", nullptr};
    definitions[1].values[2] = {"compute", nullptr}; definitions[1].default_value = "software";
    definitions[2].key = "melonds_opengl_resolution";
    for (int i = 0; i < 4; ++i) definitions[2].values[i] = {i == 0 ? "1" : i == 1 ? "2" : i == 2 ? "3" : "4", nullptr};
    definitions[2].default_value = "1";
    definitions[3].key = "melonds_show_cursor";
    definitions[3].values[0] = {"disabled", "Never"}; definitions[3].values[1] = {"timeout", nullptr};
    definitions[3].values[2] = {"always", nullptr}; definitions[3].default_value = "timeout";
    retro_core_options_v2 advertised{nullptr, definitions};
    Frontend ui; CoreOptions options;
    options.register_options(&advertised, ui);
    options.boot_values = options.values;
    require(options.values.at("melonds_render_mode") == "software" && options.values.at("melonds_show_cursor") == "disabled" &&
        ui.resolution_needs_restart, "melonDS DS native renderer/cursor/restart defaults incorrect");
    options.sync_pending(ui); options.take_update();
    require(!options.sync_pending(ui) && !options.take_update(), "Idle settings repeated renderer work");
    ui.resolution_index = 1; ui.mark_core_settings_changed();
    require(options.sync_pending(ui) && options.resolution_restart_pending &&
        options.values.at("melonds_render_mode") == "compute", "Video resolution did not stage Compute with restart");
    auto effective = [&](const char* key) {
        retro_variable variable{key, nullptr}; require(options.get_variable(variable), "Missing core option");
        return std::string(variable.value);
    };
    require(effective("melonds_opengl_resolution") == "1" && effective("melonds_render_mode") == "software",
        "Pending resolution replaced the running software renderer");
    options.set_value({"melonds_show_cursor", "always"}, ui); options.sync_pending(ui);
    require(effective("melonds_show_cursor") == "always" && effective("melonds_render_mode") == "software",
        "Another core option edit bypassed the restart requirement");
    ui.pending_core_option = std::pair<std::string, std::string>{"melonds_opengl_resolution", "4"}; ui.mark_core_settings_changed();
    options.sync_pending(ui);
    require(options.values.at("melonds_opengl_resolution") == "4" && options.values.at("melonds_render_mode") == "compute" &&
        ui.resolution_index == 3, "More Core options did not synchronize staged resolution/renderer");
    Frontend restarted_ui; CoreOptions restarted;
    const auto saved = options.persistent_values(ui); restarted.values = {saved.begin(), saved.end()};
    restarted.register_options(&advertised, restarted_ui); restarted.boot_values = restarted.values;
    require(restarted.values.at("melonds_render_mode") == "compute" && restarted.values.at("melonds_show_cursor") == "always" &&
        !restarted.resolution_restart_pending, "Restart did not apply Compute or retain an explicit cursor setting");
    restarted.sync_pending(restarted_ui); restarted.take_update();
    restarted_ui.resolution_index = 2; restarted_ui.mark_core_settings_changed();
    require(restarted.sync_pending(restarted_ui) && restarted.resolution_restart_pending,
        "Changing between upscaled resolutions did not require restart");
    restarted_ui.pending_core_option = std::pair<std::string, std::string>{"melonds_opengl_resolution", "1"}; restarted_ui.mark_core_settings_changed();
    restarted.sync_pending(restarted_ui);
    retro_variable active{"melonds_render_mode", nullptr}; restarted.get_variable(active);
    require(restarted.values.at("melonds_render_mode") == "software" && std::string(active.value) == "compute",
        "Returning to 1x did not defer the Software renderer until restart");
    Frontend inherited_ui; CoreOptions inherited;
    inherited.values = {{"melonds_opengl_resolution", "4"}, {"melonds_render_mode", "opengl"}};
    inherited.register_options(&advertised, inherited_ui);
    require(inherited.values.at("melonds_render_mode") == "compute", "Saved/CLI upscaling did not select Compute at boot");
    inherited.restore_values({}, inherited_ui); inherited.sync_pending(inherited_ui);
    require(inherited.values.at("melonds_opengl_resolution") == "1" && inherited.values.at("melonds_render_mode") == "software" &&
        inherited.values.at("melonds_show_cursor") == "disabled", "Reset did not restore linked DS defaults");
}
static int callbacks = 0;
int main() {
    try {
        require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), SDL_GetError());
        deadzone_defaults();
        normalized_catalog();
        melonds_defaults();
        Frontend ui; CoreOptions options;
        retro_core_option_definition definitions[3]{};
        definitions[0].key = "fixture_color"; definitions[0].desc = "Color"; definitions[0].info = "Choose the color";
        definitions[0].values[0] = {"red", "Red label"}; definitions[0].values[1] = {"blue", "Blue label"};
        definitions[0].default_value = "blue";
        definitions[1].key = "dolphin_efb_scale"; definitions[1].desc = "Internal resolution";
        definitions[1].values[0] = {"1", "Native"}; definitions[1].values[1] = {"2", "Double"}; definitions[1].default_value = "1";
        options.register_options(definitions, ui);
        options.boot_values = options.values;
        require(options.values.at("fixture_color") == "blue" && ui.all_core_options[0].index == 1 &&
            ui.all_core_options[0].labels[0] == "Red label", "Modern default order or value labels lost");
        options.update_display = [] { ++callbacks; return false; };
        options.sync_pending(ui); const int first = callbacks;
        options.sync_pending(ui); require(callbacks == first, "Unchanged options repeated the display callback");
        ui.page = Page::Advanced; ui.selected = 2;
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.page == Page::CoreOptions, "Advanced did not open More Core options");
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.page == Page::CoreOptionValues && ui.selected == 1, "Value submenu did not select current value");
        key(ui, SDL_SCANCODE_UP); key(ui, SDL_SCANCODE_RETURN);
        require(ui.page == Page::CoreOptions && ui.selected == 0, "Selecting value lost parent menu location");
        options.sync_pending(ui);
        require(options.values.at("fixture_color") == "red" && options.take_update() && !options.take_update(),
            "Option selection was not delivered once to core");
        key(ui, SDL_SCANCODE_DOWN); key(ui, SDL_SCANCODE_RETURN);
        key(ui, SDL_SCANCODE_DOWN); key(ui, SDL_SCANCODE_RETURN); options.sync_pending(ui);
        require(ui.resolution_index == 1 && options.values.at("dolphin_efb_scale") == "2" && options.resolution_restart_pending,
            "Full option list did not synchronize Video resolution");
        ui.resolution_index = 0; ui.mark_core_settings_changed(); options.sync_pending(ui);
        require(ui.all_core_options[1].index == 0, "Video resolution did not synchronize full option list");
        require(options.set_visible({"fixture_color", false}, ui) && ui.visible_all_core_options().size() == 1,
            "Core option visibility ignored");
        require(!options.set_value({"fixture_color", "green"}, ui), "Invalid enum value accepted");
        const auto saved = options.persistent_values(ui);
        require(std::find(saved.begin(), saved.end(), std::pair<std::string,std::string>{"fixture_color","red"}) != saved.end(),
            "Non-curated option omitted from persistence");
        options.restore_values({{"fixture_color","blue"}}, ui); options.sync_pending(ui);
        require(options.values.at("fixture_color") == "blue", "Inherited value not restored");
        key(ui, SDL_SCANCODE_ESCAPE); require(ui.page == Page::Advanced && ui.selected == 2, "Back lost Advanced location");
        retro_core_option_v2_definition v2[3]{};
        v2[0].key = "fixture_v2"; v2[0].desc = "V2 option"; v2[0].info = "V2 description";
        v2[0].values[0] = {"yes","Yes label"}; v2[0].values[1] = {"no","No label"}; v2[0].default_value = "no";
        v2[1].key = "fixture_delimiter"; v2[1].desc = "Name; with delimiter";
        v2[1].values[0] = {"left|right",nullptr}; v2[1].default_value = "left|right";
        retro_core_options_v2 modern{nullptr,v2}; options.register_options(&modern,ui);
        require(ui.all_core_options.size() == 2 && ui.all_core_options[0].description == "V2 description" &&
            options.values.at("fixture_v2") == "no", "V2 re-registration retained stale definitions");
        require(options.values.at("fixture_delimiter") == "left|right" && ui.all_core_options[1].name == "Name; with delimiter",
            "Modern enum or name was parsed as legacy delimiters");
        retro_variable legacy[]{{"legacy", "Legacy option; first|second"},{nullptr,nullptr}};
        options.register_variables(legacy,ui);
        require(ui.all_core_options.size() == 1 && ui.all_core_options[0].name == "Legacy option" &&
            ui.all_core_options[0].values.size() == 2, "Legacy options not exposed");
        std::cout << "Core option definitions, navigation, synchronization, visibility and persistence passed\n";
        SDL_Quit(); return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; SDL_Quit(); return 1; }
}
