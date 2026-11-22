#include "core_options.h"
#include "frontend.h"
#include <algorithm>
#include <sstream>
#include <string_view>

void CoreOptions::register_mupen_choice(Frontend& ui, const std::string& key,
                                        const std::vector<std::string>& advertised_values)
{
    auto& choices = ui.core_choices;
    if (std::any_of(choices.begin(), choices.end(),
                    [&](const auto& option)
                    {
                        return option.key == key;
                    }))
    {
        return;
    }
    Frontend::CoreChoice option;
    option.key = key;
    std::vector<std::pair<std::string, std::string>> candidates;
    if (key == "mupen64plus-rdp-plugin")
    {
        option.name = "RDP renderer";
        option.description = "Choose GLideN64/OpenGL or ParaLLEl-RDP/Vulkan. Restart game to apply.";
        candidates = {{"gliden64", "GLideN64 (OpenGL)"},
                      {"parallel", "ParaLLEl-RDP (Vulkan)"},
                      {"angrylion", "Angrylion (software)"}};
    }
    else if (key == "mupen64plus-rsp-plugin")
    {
        option.name = "RSP emulator";
        option.description = "HLE favors performance; ParaLLEl/CXD4 provide LLE. ParaLLEl-RDP requires LLE.";
        candidates = {{"hle", "HLE"}, {"parallel", "ParaLLEl"}, {"cxd4", "CXD4"}};
    }
    else if (key == "mupen64plus-ThreadedRenderer")
    {
        option.name = "Threaded rendering";
        option.description = "GLideN64: may improve performance, but increases input latency. Restart required.";
        option.gliden64_only = true;
        candidates = {{"False", "off"}, {"True", "on"}};
    }
    else if (key == "mupen64plus-BilinearMode")
    {
        option.name = "Texture filtering";
        option.description = "GLideN64: standard bilinear or the N64's 3-point texture filter. Restart required.";
        option.gliden64_only = true;
        candidates = {{"standard", "Standard"}, {"3point", "N64 3-point"}};
    }
    else
    {
        return;
    }
    for (const auto& [value, label] : candidates)
    {
        if (std::find(advertised_values.begin(), advertised_values.end(), value) != advertised_values.end())
        {
            if (value == values[key])
            {
                option.index = option.values.size();
            }
            option.values.push_back(value);
            option.labels.push_back(label);
        }
    }
    if (option.values.empty())
    {
        return;
    }
    choices.push_back(std::move(option));
    auto rank = [](const auto& option)
    {
        return (option.key == "mupen64plus-rdp-plugin")         ? 0
               : (option.key == "mupen64plus-rsp-plugin")       ? 1
               : (option.key == "mupen64plus-ThreadedRenderer") ? 2
                                                                : 3;
    };
    std::stable_sort(choices.begin(), choices.end(),
                     [&](const auto& a, const auto& b)
                     {
                         return rank(a) < rank(b);
                     });
}
void CoreOptions::refresh_mupen_resolution(Frontend& ui)
{
    if (!defaults.contains("mupen64plus-rdp-plugin"))
    {
        return;
    }
    const auto& renderer = values.at("mupen64plus-rdp-plugin");
    const std::string key = renderer == "parallel"   ? "mupen64plus-parallel-rdp-upscaling"
                            : renderer == "gliden64" ? "mupen64plus-43screensize"
                                                     : "";
    if (key == resolution_key)
    {
        return;
    }
    resolution_key = key;
    if (key.empty())
    {
        ui.resolution_values.clear();
        ui.resolution_index = 0;
        return;
    }
    ui.resolution_values = resolutions[key];
    const auto found = std::find(ui.resolution_values.begin(), ui.resolution_values.end(), values[key]);
    ui.resolution_index = found == ui.resolution_values.end() ? 0 : found - ui.resolution_values.begin();
    if (found == ui.resolution_values.end() && !ui.resolution_values.empty())
    {
        ui.mark_core_settings_changed();
    }
    ui.resolution_needs_restart = true;
}
void CoreOptions::register_definitions(std::vector<Definition> definitions, Frontend& ui)
{
    ui.all_core_options.clear();
    ui.core_choices.clear();
    defaults.clear();
    resolutions.clear();
    resolution_key.clear();
    ui.resolution_values.clear();
    ui.resolution_index = 0;
    ui.resolution_needs_restart = false;
    for (auto& definition : definitions)
    {
        if (definition.values.empty())
        {
            continue;
        }
        const bool runahead_alias = definition.key == "bsnes_run_ahead_frames" ||
                                    definition.key == "swanstation_Main_RunaheadFrameCount";
        if (runahead_alias)
        {
            for (std::size_t i = definition.values.size(); i-- > 0;)
            {
                const auto& value = definition.values[i];
                if (value != "OFF" && value != "0" && value != "1" && value != "2" && value != "3")
                {
                    definition.values.erase(definition.values.begin() + i);
                    definition.labels.erase(definition.labels.begin() + i);
                }
            }
            if (definition.values.empty())
            {
                continue;
            }
        }
        auto& option = ui.all_core_options.emplace_back();
        option.restart_required = !runahead_alias;
        option.key = std::move(definition.key);
        option.name = definition.name.empty() ? option.key : std::move(definition.name);
        option.description = std::move(definition.description);
        option.values = std::move(definition.values);
        option.labels = std::move(definition.labels);
        option.visible = !runtime_overrides.contains(option.key);
        const auto& key = option.key;
        auto accepts = [&](const std::string& value)
        {
            return std::find(option.values.begin(), option.values.end(), value) != option.values.end();
        };
        std::string default_value = definition.default_value;
        if (key == "mupen64plus-BilinearMode")
        {
            default_value = "3point";
        }
        if (key == "mupen64plus-pak1")
        {
            default_value = "rumble";
        }
        if (key == "melonds_show_cursor")
        {
            default_value = "disabled";
        }
        if (key == "citra_analog_function")
        {
            default_value = "c_stick";
        }
        if (key == "melonds_touch_mode")
        {
            default_value = "touch";
        }
        if (key == "citra_analog_deadzone")
        {
            default_value = "0";
        }
        if (key == "reicast_analog_stick_deadzone" || key == "reicast_trigger_deadzone")
        {
            default_value = "0%";
        }
        if (key == "ppsspp_analog_deadzone")
        {
            default_value = "0.0";
        }
#if defined(__aarch64__)
        if (key == "dolphin_main_cpu_thread")
        {
            default_value = "enabled";
        }
#else
        if (key == "dolphin_main_cpu_thread")
        {
            default_value = "disabled";
        }
#endif
        // Each game's VMUs belong with its saves, rather than shared BIOS files.
        if (key == "reicast_per_content_vmus" || key == "flycast_per_content_vmus")
        {
            default_value = "All VMUs";
        }
        if (!accepts(default_value))
        {
            default_value = accepts(definition.default_value) ? definition.default_value : option.values.front();
        }
        defaults[key] = default_value;
        const auto configured = values.find(key);
        if (configured == values.end() || !accepts(configured->second))
        {
            values[key] = default_value;
        }
        register_mupen_choice(ui, key, option.values);
        if (key == "citra_resolution_factor" || key == "ppsspp_internal_resolution" || key == "dolphin_efb_scale" ||
            key == "swanstation_GPU_ResolutionScale" || key == "melonds_opengl_resolution" ||
            key == "mupen64plus-43screensize" || key == "mupen64plus-parallel-rdp-upscaling" ||
            key == "reicast_internal_resolution" || key == "flycast_internal_resolution")
        {
            resolution_key = key;
            ui.resolution_needs_restart = key == "dolphin_efb_scale" || key == "mupen64plus-43screensize" ||
                                          key == "mupen64plus-parallel-rdp-upscaling" ||
                                          key == "reicast_internal_resolution" || key == "flycast_internal_resolution";
            ui.resolution_values = option.values;
            // The Video menu lists resolutions by size, independently of enum order.
            auto magnitude = [](const std::string& item)
            {
                unsigned value = 0;
                std::istringstream(item) >> value;
                return value;
            };
            std::stable_sort(ui.resolution_values.begin(), ui.resolution_values.end(),
                             [&](const auto& a, const auto& b)
                             {
                                 return magnitude(a) < magnitude(b);
                             });
            resolutions[key] = ui.resolution_values;
        }
    }
    // Mupen substitutes an LLE RSP for HLE when ParaLLEl-RDP is selected.
    ui.normalize_core_choices();
    for (const auto& choice : ui.core_choices)
    {
        values[choice.key] = choice.values[choice.index];
    }
    refresh_mupen_resolution(ui);
    sync_melonds_renderer(ui);
    project_values(ui);
    ui.mark_core_settings_changed();
}

void CoreOptions::register_variables(const retro_variable* variables, Frontend& ui)
{
    std::vector<Definition> definitions;
    for (auto* variable = variables; variable && variable->key; ++variable)
    {
        const std::string text = variable->value ? variable->value : "";
        const auto separator = text.find("; ");
        if (separator == std::string::npos)
        {
            continue;
        }
        Definition definition;
        definition.key = variable->key;
        definition.name = text.substr(0, separator);
        std::istringstream accepted(text.substr(separator + 2));
        for (std::string value; std::getline(accepted, value, '|');)
        {
            if (value.empty())
            {
                continue;
            }
            definition.values.push_back(value);
            definition.labels.push_back(value);
        }
        if (definition.values.empty())
        {
            continue;
        }
        definition.default_value = definition.values.front();
        definitions.push_back(std::move(definition));
    }
    register_definitions(std::move(definitions), ui);
}

template <class T> void CoreOptions::register_modern(const T* definitions, Frontend& ui)
{
    std::vector<Definition> normalized;
    for (auto* source = definitions; source && source->key; ++source)
    {
        Definition definition;
        definition.key = source->key;
        definition.name = source->desc ? source->desc : source->key;
        definition.description = source->info ? source->info : "";
        for (const auto& value : source->values)
        {
            if (!value.value)
            {
                break;
            }
            definition.values.emplace_back(value.value);
            definition.labels.emplace_back(value.label ? value.label : value.value);
        }
        if (definition.values.empty())
        {
            continue;
        }
        definition.default_value = source->default_value ? source->default_value : definition.values.front();
        normalized.push_back(std::move(definition));
    }
    register_definitions(std::move(normalized), ui);
}

void CoreOptions::register_options(const retro_core_option_definition* definitions, Frontend& ui)
{
    register_modern(definitions, ui);
}
void CoreOptions::register_options(const retro_core_options_v2* options, Frontend& ui)
{
    register_modern(options ? options->definitions : nullptr, ui);
}
void CoreOptions::sync_melonds_renderer(Frontend& ui)
{
    if (!defaults.contains("melonds_screen_layout1") || !values.contains("melonds_render_mode"))
    {
        return;
    }
    ui.resolution_needs_restart = true;
    const std::string renderer = values["melonds_opengl_resolution"] == "1" ? "software" : "compute";
    const auto definition = std::find_if(ui.all_core_options.begin(), ui.all_core_options.end(),
                                         [](const auto& option)
                                         {
                                             return option.key == "melonds_render_mode";
                                         });
    if (definition == ui.all_core_options.end() ||
        std::find(definition->values.begin(), definition->values.end(), renderer) == definition->values.end())
    {
        return;
    }
    if (values["melonds_render_mode"] != renderer)
    {
        values["melonds_render_mode"] = renderer;
        changed = true;
    }
}
void CoreOptions::project_values(Frontend& ui)
{
    for (auto& option : ui.all_core_options)
    {
        auto found = std::find(option.values.begin(), option.values.end(), values[option.key]);
        if (found == option.values.end())
        {
            values[option.key] = option.values.front();
            option.index = 0;
        }
        else
        {
            option.index = found - option.values.begin();
        }
    }
    for (auto& choice : ui.core_choices)
    {
        auto found = std::find(choice.values.begin(), choice.values.end(), values[choice.key]);
        choice.index = found == choice.values.end() ? 0 : found - choice.values.begin();
    }
    if (!ui.runahead_option_key.empty())
    {
        ui.runahead_available = false;
    }
    ui.runahead_option_key.clear();
    for (const auto& option : ui.all_core_options)
    {
        if (option.key != "bsnes_run_ahead_frames" && option.key != "swanstation_Main_RunaheadFrameCount")
        {
            continue;
        }
        const std::string off = option.key == "bsnes_run_ahead_frames" ? "OFF" : "0";
        const std::array<std::string, 4> candidates{off, "1", "2", "3"};
        if (!std::all_of(candidates.begin(), candidates.end(), [&](const auto& value)
                        {
                            return std::find(option.values.begin(), option.values.end(), value) != option.values.end();
                        }))
        {
            continue;
        }
        ui.runahead_option_key = option.key;
        ui.runahead_values = candidates;
        ui.runahead_available = true;
        const auto found = std::find(candidates.begin(), candidates.end(), values[option.key]);
        ui.runahead_frames = found == candidates.end() ? 0 : static_cast<int>(found - candidates.begin());
        break;
    }
    refresh_mupen_resolution(ui);
    if (!resolution_key.empty())
    {
        auto found = std::find(ui.resolution_values.begin(), ui.resolution_values.end(), values[resolution_key]);
        ui.resolution_index = found == ui.resolution_values.end() ? 0 : found - ui.resolution_values.begin();
    }
    if (ui.page == Page::CoreOptions)
    {
        ui.selected = std::min(ui.selected, static_cast<int>(ui.visible_all_core_options().size()));
    }
    if (ui.page == Page::CoreOptionValues)
    {
        auto* option = ui.current_core_option();
        ui.selected = std::min(ui.selected, option ? static_cast<int>(option->values.size()) : 0);
    }
}
bool CoreOptions::set_value(const retro_variable& option, Frontend& ui)
{
    if (!option.key || !option.value)
    {
        return false;
    }
    if (const auto forced = runtime_overrides.find(option.key); forced != runtime_overrides.end())
    {
        return forced->second == option.value;
    }
    auto definition = std::find_if(ui.all_core_options.begin(), ui.all_core_options.end(),
                                   [&](const auto& v)
                                   {
                                       return v.key == option.key;
                                   });
    if (definition == ui.all_core_options.end() ||
        std::find(definition->values.begin(), definition->values.end(), option.value) == definition->values.end())
    {
        return false;
    }
    if (values[option.key] == option.value)
    {
        return true;
    }
    values[option.key] = option.value;
    changed = true;
    if (option.key == resolution_key)
    {
        sync_melonds_renderer(ui);
        if (ui.resolution_needs_restart)
        {
            resolution_restart_pending = true;
        }
    }
    project_values(ui);
    ui.mark_core_settings_changed();
    return true;
}
bool CoreOptions::set_visible(const retro_core_option_display& display, Frontend& ui)
{
    if (!display.key)
    {
        return false;
    }
    for (auto& option : ui.all_core_options)
    {
        if (option.key == display.key)
        {
            option.visible = display.visible && !runtime_overrides.contains(display.key);
            if (ui.page == Page::CoreOptions)
            {
                ui.selected = std::min(ui.selected, static_cast<int>(ui.visible_all_core_options().size()));
            }
            return true;
        }
    }
    return false;
}
void CoreOptions::restore_values(const std::map<std::string, std::string>& inherited, Frontend& ui)
{
    for (const auto& option : ui.all_core_options)
    {
        const auto found = inherited.find(option.key);
        retro_variable variable{option.key.c_str(),
                                found == inherited.end() ? defaults.at(option.key).c_str() : found->second.c_str()};
        set_value(variable, ui);
    }
    sync_melonds_renderer(ui);
    project_values(ui);
    ui.mark_core_settings_changed();
}

bool CoreOptions::get_variable(retro_variable& variable) const
{
    // Keep the running DS renderer intact while the menu holds a resolution
    // selected for the next launch. Other core option edits can still apply.
    if (resolution_restart_pending && defaults.contains("melonds_screen_layout1") &&
        (std::string_view(variable.key) == "melonds_opengl_resolution" ||
         std::string_view(variable.key) == "melonds_render_mode"))
    {
        if (const auto boot = boot_values.find(variable.key); boot != boot_values.end())
        {
            variable.value = boot->second.c_str();
            return true;
        }
    }
    if (const auto forced = runtime_overrides.find(variable.key); forced != runtime_overrides.end())
    {
        variable.value = forced->second.c_str();
        return true;
    }
    const auto found = values.find(variable.key);
    variable.value = found == values.end() ? nullptr : found->second.c_str();
    return found != values.end();
}

bool CoreOptions::take_update()
{
    const bool update = changed;
    changed = false;
    return update;
}

bool CoreOptions::sync_pending(Frontend& ui)
{
    if (!ui.take_core_settings_changed())
    {
        return false;
    }
    const bool resolution_was_pending = resolution_restart_pending;
    // Commit Video's pending selection before a catalog edit projects values
    // back into the UI, otherwise that projection can discard the selection.
    const bool resolution_notification = sync_resolution(ui);
    if (ui.pending_core_option)
    {
        auto option = std::move(*ui.pending_core_option);
        ui.pending_core_option.reset();
        set_value({option.first.c_str(), option.second.c_str()}, ui);
        ui.take_core_settings_changed();
    }
    sync_choices(ui);
    project_values(ui);
    if (update_display)
    {
        update_display();
    }
    return resolution_notification || (!resolution_was_pending && resolution_restart_pending);
}

void CoreOptions::sync_choices(Frontend& ui)
{
    ui.normalize_core_choices();
    for (const auto& option : ui.core_choices)
    {
        const auto& value = option.values[option.index];
        if (values[option.key] != value)
        {
            values[option.key] = value;
            if (!option.restart_required)
            {
                changed = true;
            }
        }
    }
    refresh_mupen_resolution(ui);
}
bool CoreOptions::sync_resolution(Frontend& ui)
{
    if (!resolution_key.empty() && !ui.resolution_values.empty())
    {
        const auto& value = ui.resolution_values[ui.resolution_index];
        if (values[resolution_key] != value)
        {
            values[resolution_key] = value;
            sync_melonds_renderer(ui);
            if (ui.resolution_needs_restart)
            {
                resolution_restart_pending = true;
                return true;
            }
            else
            {
                changed = true;
            }
        }
    }
    return false;
}

std::vector<std::pair<std::string, std::string>> CoreOptions::persistent_values(const Frontend& ui) const
{
    std::vector<std::pair<std::string, std::string>> result;
    for (const auto& option : ui.all_core_options)
    {
        result.emplace_back(option.key, option.values[option.index]);
    }
    for (const auto& option : ui.core_choices)
    {
        result.emplace_back(option.key, option.values[option.index]);
    }
    // Keep each N64 renderer's resolution preference when switching backends.
    for (const std::string key : {"mupen64plus-43screensize", "mupen64plus-parallel-rdp-upscaling"})
    {
        if (resolutions.contains(key) && values.contains(key))
        {
            const auto& value = (key == resolution_key && !ui.resolution_values.empty())
                                    ? ui.resolution_values[ui.resolution_index]
                                    : values.at(key);
            result.emplace_back(key, value);
        }
    }
    std::map<std::string, std::string> unique;
    for (const auto& [key, value] : result)
    {
        unique[key] = value;
    }
    return {unique.begin(), unique.end()};
}
