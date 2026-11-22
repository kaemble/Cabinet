#pragma once
#include <libretro.h>
#include <map>
#include <string>
#include <utility>
#include <vector>

class Frontend;

// Owns the libretro option values and their boot/default state. The menu's
// existing choice models are populated and synchronized through these methods.
class CoreOptions
{
public:
    std::map<std::string, std::string> values, defaults, boot_values;
    // Session-only presentation constraints; never written to user settings.
    std::map<std::string, std::string> runtime_overrides;
    std::map<std::string, std::vector<std::string>> resolutions;
    std::string resolution_key;
    bool changed = false, resolution_restart_pending = false;

    retro_core_options_update_display_callback_t update_display = nullptr;
    void register_options(const retro_core_option_definition* definitions, Frontend& ui);
    void register_options(const retro_core_options_v2* options, Frontend& ui);
    bool set_value(const retro_variable& option, Frontend& ui);
    bool set_visible(const retro_core_option_display& option, Frontend& ui);
    void restore_values(const std::map<std::string, std::string>& inherited, Frontend& ui);
    void register_variables(const retro_variable* variables, Frontend& ui);
    bool get_variable(retro_variable& variable) const;
    bool take_update();
    // Synchronize only after the option models change. Returns whether a
    // resolution restart notification is needed.
    bool sync_pending(Frontend& ui);
    void sync_choices(Frontend& ui);
    // True when a newly selected resolution needs a restart notification.
    bool sync_resolution(Frontend& ui);
    std::vector<std::pair<std::string, std::string>> persistent_values(const Frontend& ui) const;

private:
    struct Definition
    {
        std::string key, name, description, default_value;
        std::vector<std::string> values, labels;
    };
    void register_definitions(std::vector<Definition> definitions, Frontend& ui);
    template <class T> void register_modern(const T* definitions, Frontend& ui);
    void project_values(Frontend& ui);
    void sync_melonds_renderer(Frontend& ui);
    void register_mupen_choice(Frontend& ui, const std::string& key, const std::vector<std::string>& advertised_values);
    void refresh_mupen_resolution(Frontend& ui);
};
