#pragma once
#include <cstdint>
#include "wii_controller.h"
#include "aspect_ratio.h"
#include <SDL3/SDL.h>
#include <array>
#include <string>
#include <vector>
#include <optional>
#include <utility>
#include <mutex>
#include "input_bindings.h"

enum class Action
{
    None,
    Save,
    Load,
    Reset,
    Quit,
    RestoreBindings,
    CreateGameConfig,
    ResetGameConfig
};
enum class Page
{
    Game,
    Main,
    Configuration,
    Controls,
    Bindings,
    Hotkeys,
    Video,
    Audio,
    Advanced,
    CoreOptions,
    CoreOptionValues,
    Interface,
    Confirm
};
class TextRenderer;

// A chord fires once when both buttons are down and rearms after both release.
// Individual button presses are delivered to gameplay immediately.
struct MenuChord
{
    bool consumed = false;
    struct Result
    {
        bool open = false;
    };
    Result update(bool a, bool b);
    void clear()
    {
        consumed = false;
    }
};

class Frontend
{
public:
    Frontend()
    {
        hotkeys.configure_hotkeys("generic", {});
    }
    ~Frontend();
    Frontend(const Frontend&) = delete;
    Frontend& operator=(const Frontend&) = delete;
    bool discover();
    void event(const SDL_Event& event);
    void update(bool focused, std::uint64_t now);
    // Sample gameplay separately, after pacing waits and before core execution.
    void sample_gameplay(bool focused);
    void draw(SDL_Renderer* renderer, TextRenderer& text, int width, int height);
    bool needs_redraw(std::uint64_t now) const;
    std::uint64_t next_menu_update(std::uint64_t now) const;
    void open_menu();
    void notify(std::string message);
    void resume_with_message(std::string message);
    Action take_action();
    bool paused() const
    {
        return page != Page::Game;
    }
    SDL_JoystickID controller_id() const;
    bool request_rumble(bool strong, std::uint16_t strength);
    void stop_rumble();
    const std::array<std::int16_t, 16>& gameplay() const
    {
        return game;
    }

    InputBindings bindings;
    InputBindings hotkeys;
    const InputSnapshot& input() const
    {
        return snapshot;
    }

    Page page = Page::Game;
    int selected = 0;
    std::vector<std::string> resolution_values;
    int resolution_index = 0;
    bool resolution_needs_restart = false;
    struct CoreChoice
    {
        std::string key, name, description;
        std::vector<std::string> values, labels;
        int index = 0;
        bool restart_required = true, gliden64_only = false, visible = true;
    };
    std::vector<CoreChoice> core_choices;
    std::vector<CoreChoice> all_core_options;
    std::optional<std::pair<std::string, std::string>> pending_core_option;
    std::vector<int> visible_all_core_options() const;
    CoreChoice* current_core_option();
    const CoreChoice* current_core_option() const;
    std::vector<int> visible_core_choices() const;
    void normalize_core_choices();
    void cycle_core_choice(int index, int direction);
    // Also call this after changing the public option models programmatically.
    void mark_core_settings_changed()
    {
        core_settings_changed = true;
    }
    bool take_core_settings_changed();
    bool vsync = true, vrr = false;
    bool runahead_available = false;
    int runahead_frames = 0;
    std::string runahead_option_key;
    std::array<std::string, 4> runahead_values;
    void cycle_runahead(int direction);
    std::string game_title = "Game";
    int volume = 80;
    int wii_controller = 0;
    int aspect_mode = aspect_ratio::Auto;
    int scaling = 2; // nearest, bilinear, Sharp Bilinear, integer
    bool fullscreen = false, swap_confirm_cancel = false, stick_shortcut = false, home_shortcut = false;
    bool pause_on_focus_loss = true, hide_cursor = true;
    bool confirm_save_states = true;
    bool show_stats = false;
    std::string stats_text;
    bool game_config = false;
    bool rumble_enabled = true;
    int stick_deadzone = 5, trigger_deadzone = 5;
    bool fast_forward = false; // Command-line request.
    bool fast_forward_held = false;
    bool can_save = false, has_save = false;

private:
    // Compare visible content rather than invalidating on every input event:
    // mouse motion and held buttons often leave the menu unchanged.
    struct MenuAppearance
    {
        Page page;
        int selected;
        bool controller, swapped, keyboard, capturing, can_save, has_save, game_config;
        Action confirmation;
        std::string title, system, notice, stats;
        std::vector<std::string> items, descriptions;
        bool operator==(const MenuAppearance&) const = default;
    };
    MenuAppearance menu_appearance(std::uint64_t now) const;
    std::optional<MenuAppearance> presented_menu;
    struct MouseTarget
    {
        SDL_FRect rect;
        int action;
    };
    std::vector<MouseTarget> mouse_targets;
    Page mouse_page = Page::Game, mouse_press_page = Page::Game;
    SDL_WindowID mouse_window = 0;
    float mouse_scale_x = 1, mouse_scale_y = 1, wheel_remainder = 0;
    int mouse_press = -1, mouse_press_selection = 0;
    int mouse_target(float x, float y) const;
    bool mouse_event(const SDL_Event& event);
    SDL_Gamepad* pad = nullptr;
    std::mutex rumble_mutex;
    bool rumble_supported = false;
    std::array<std::uint16_t, 2> rumble_strength{}, rumble_sent{};
    std::uint64_t rumble_refresh = 0;
    void update_rumble(std::uint64_t now);
    std::array<std::int16_t, 16> game{};
    InputSnapshot snapshot;
    bool gameplay_ready = false;
    bool core_settings_changed = true;
    std::string selected_core_option;
    int core_options_selection = 0;
    bool binding_keyboard = false, capturing = false, previous_clear = false;
    std::array<bool, SDL_SCANCODE_COUNT> hotkey_keys{};
    std::array<bool, 2> hotkey_pressed{};
    std::array<std::int16_t, 16> previous_hotkeys{};
    void bind_source(int source);
    std::array<bool, 6> previous{};
    MenuChord chord;
    bool blocked = false;
    int repeat_direction = 0;
    std::uint64_t repeat_at = 0, notice_until = 0;
    std::string notice;
    Action pending = Action::None, confirmation = Action::None;
    int return_selection = 0;
    Page return_page = Page::Main;
    void resume();
    void navigate(int direction);
    void activate();
    void back();
    void adjust(int direction);
    void confirm(Action action);
    void request_state(Action action);
    std::vector<std::string> rows() const;
    std::string label(SDL_GamepadButton button) const;
};
