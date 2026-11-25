#include "frontend.h"
#include <cstdint>
#include <cstddef>
#include <utility>
#include "system_names.h"
#include "ui_text.h"
#include <libretro.h>
#include <algorithm>

MenuChord::Result MenuChord::update(bool a, bool b)
{
    Result result;
    if (a && b && !consumed)
    {
        result.open = true;
        consumed = true;
    }
    if (!a && !b)
    {
        consumed = false;
    }
    return result;
}

Frontend::~Frontend()
{
    stop_rumble();
    if (pad)
    {
        SDL_CloseGamepad(pad);
    }
}
bool Frontend::request_rumble(bool strong, std::uint16_t strength)
{
    std::lock_guard lock(rumble_mutex);
    if (!rumble_supported)
    {
        return false;
    }
    rumble_strength[strong ? 0 : 1] = strength;
    return true;
}
void Frontend::stop_rumble()
{
    std::lock_guard lock(rumble_mutex);
    rumble_strength = {};
    if (pad && (rumble_sent[0] || rumble_sent[1]))
    {
        SDL_RumbleGamepad(pad, 0, 0, 0);
    }
    rumble_sent = {};
    rumble_refresh = 0;
}
void Frontend::update_rumble(std::uint64_t now)
{
    if (!rumble_enabled || paused())
    {
        stop_rumble();
        return;
    }
    std::lock_guard lock(rumble_mutex);
    if (!rumble_supported || !pad)
    {
        return;
    }
    if (rumble_strength != rumble_sent || ((rumble_strength[0] || rumble_strength[1]) && now >= rumble_refresh))
    {
        // Libretro strengths persist until changed. Renew a bounded SDL effect
        // so a stalled or crashed runner cannot leave the motors running.
        if (SDL_RumbleGamepad(pad, rumble_strength[0], rumble_strength[1], 2000))
        {
            rumble_sent = rumble_strength;
            rumble_refresh = now + 1000;
        }
    }
}
SDL_JoystickID Frontend::controller_id() const
{
    return pad ? SDL_GetGamepadID(pad) : 0;
}
void Frontend::notify(std::string message)
{
    notice = std::move(message);
    notice_until = SDL_GetTicks() + 4500;
}
bool Frontend::discover()
{
    if (pad)
    {
        return false;
    }
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count && !pad; ++i)
    {
        pad = SDL_OpenGamepad(ids[i]);
    }
    SDL_free(ids);
    if (pad)
    {
        std::lock_guard lock(rumble_mutex);
        rumble_supported =
            SDL_GetBooleanProperty(SDL_GetGamepadProperties(pad), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
    }
    if (pad)
    {
        blocked = true;
    }
    return pad != nullptr;
}
void Frontend::open_menu()
{
    discard_shader_move();
    stop_rumble();
    fast_forward_held = false;
    hotkey_pressed.fill(false);
    mouse_press = -1;
    wheel_remainder = 0;
    capturing = false;
    page = Page::Main;
    selected = 0;
    game.fill(0);
    snapshot = {};
    blocked = true;
    chord.clear();
    previous.fill(false);
    repeat_direction = 0;
}
void Frontend::resume()
{
    discard_shader_move();
    fast_forward_held = false;
    hotkey_pressed.fill(false);
    mouse_press = -1;
    wheel_remainder = 0;
    page = Page::Game;
    game.fill(0);
    snapshot = {};
    blocked = true;
    chord.clear();
    previous.fill(false);
    repeat_direction = 0;
}
void Frontend::resume_with_message(std::string message)
{
    resume();
    notify(std::move(message));
}
int Frontend::mouse_target(float x, float y) const
{
    if (!paused() || mouse_page != page)
    {
        return -1;
    }
    SDL_FPoint point{x * mouse_scale_x, y * mouse_scale_y};
    for (const auto& target : mouse_targets)
    {
        if ((!capturing || target.action == -2) && SDL_PointInRectFloat(&point, &target.rect))
        {
            return target.action;
        }
    }
    return -1;
}
bool Frontend::mouse_event(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST)
    {
        mouse_press = -1;
    }
    if (e.type != SDL_EVENT_MOUSE_MOTION && e.type != SDL_EVENT_MOUSE_BUTTON_DOWN &&
        e.type != SDL_EVENT_MOUSE_BUTTON_UP && e.type != SDL_EVENT_MOUSE_WHEEL)
    {
        return false;
    }
    const auto window = (e.type == SDL_EVENT_MOUSE_MOTION)  ? e.motion.windowID
                        : (e.type == SDL_EVENT_MOUSE_WHEEL) ? e.wheel.windowID
                                                            : e.button.windowID;
    if (!paused() || window != mouse_window)
    {
        mouse_press = -1;
        return true;
    }
    if (e.type == SDL_EVENT_MOUSE_MOTION)
    {
        // A stationary cursor must not undo keyboard/controller navigation.
        if (e.motion.xrel != 0 || e.motion.yrel != 0)
        {
            const int target = mouse_target(e.motion.x, e.motion.y);
            if (target >= 0 && !shader_move)
            {
                selected = target;
            }
        }
    }
    else if (e.type == SDL_EVENT_MOUSE_WHEEL)
    {
        if (capturing || mouse_page != page)
        {
            return true;
        }
        mouse_press = -1;
        wheel_remainder += e.wheel.y * ((e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) ? -1 : 1);
        const int steps = static_cast<int>(wheel_remainder);
        wheel_remainder -= steps;
        // Clamp rather than wrap when scrolling through a long binding list.
        if (!shader_move)
        {
            selected = std::clamp(selected - steps, 0, static_cast<int>(rows().size()) - 1);
        }
    }
    else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        // While capturing a keyboard binding, any mouse button can be assigned
        // (including right/middle). Cancel targets still work via the menu.
        if (capturing && (page == Page::Bindings || page == Page::Hotkeys) && binding_keyboard &&
            e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !blocked)
        {
            const int target = mouse_target(e.button.x, e.button.y);
            if (target == -2)
            {
                // Fall through to normal cancel handling below.
            }
            else
            {
                binding_keyboard = true;
                bind_source(InputBindings::mouse_source(e.button.button));
                mouse_press = -1;
                return true;
            }
        }
        if (e.button.button != SDL_BUTTON_LEFT)
        {
            return true;
        }
        const int target = mouse_target(e.button.x, e.button.y);
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        {
            mouse_press = target;
            mouse_press_page = page;
            if (target >= 0 && !shader_move)
            {
                selected = target;
            }
            mouse_press_selection = selected;
        }
        else
        {
            const bool activate_click =
                target != -1 && target == mouse_press && page == mouse_press_page && selected == mouse_press_selection;
            mouse_press = -1;
            if (activate_click)
            {
                if (target == -2)
                {
                    back();
                }
                else if (target == -4)
                {
                    bind_source(0);
                }
                else if (target == -5)
                {
                    toggle_shader();
                }
                else if (target == -6)
                {
                    move_shader();
                }
                else
                {
                    activate();
                }
            }
        }
    }
    return true;
}
void Frontend::event(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP)
    {
        const auto key = e.key.scancode;
        if (key > SDL_SCANCODE_UNKNOWN && key < SDL_SCANCODE_COUNT)
        {
            hotkey_keys[key] = e.type == SDL_EVENT_KEY_DOWN;
        }
    }
    if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST)
    {
        hotkey_keys.fill(false);
        hotkey_pressed.fill(false);
        fast_forward_held = false;
    }
    if (mouse_event(e))
    {
        return;
    }
    if (e.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN)
    {
        fullscreen = true;
    }
    else if (e.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN)
    {
        fullscreen = false;
    }
    if (e.type == SDL_EVENT_GAMEPAD_ADDED && discover())
    {
        notify("Controller connected");
    }
    if (e.type == SDL_EVENT_GAMEPAD_REMOVED && e.gdevice.which == controller_id())
    {
        stop_rumble();
        {
            std::lock_guard lock(rumble_mutex);
            rumble_supported = false;
        }
        SDL_CloseGamepad(pad);
        pad = nullptr;
        game.fill(0);
        snapshot = {};
        chord.clear();
        discover();
        if (!pad)
        {
            open_menu();
        }
        notify(pad ? "Controller disconnected - using next pad" : "Controller disconnected - game paused");
    }
    if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST && pause_on_focus_loss && !paused())
    {
        open_menu();
    }
    if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
    {
        if (!paused())
        {
            open_menu();
        }
        confirm(Action::Quit);
    }
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.which == controller_id() && !paused() && !blocked)
    {
        const int source = e.gbutton.button + 1;
        for (std::size_t i = 0; i < hotkey_pressed.size(); ++i)
        {
            const auto& binding = hotkeys.rows()[i];
            if (binding.pad == source ||
                std::find(binding.extra_pads.begin(), binding.extra_pads.end(), source) != binding.extra_pads.end())
            {
                hotkey_pressed[i] = true;
            }
        }
    }
    if (e.type != SDL_EVENT_KEY_DOWN || e.key.repeat)
    {
        return;
    }
    if (e.key.scancode == SDL_SCANCODE_F11)
    {
        fullscreen = !fullscreen;
        return;
    }
    if ((page == Page::Bindings || page == Page::Hotkeys) && capturing)
    {
        if (e.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            capturing = false;
            blocked = true;
        }
        else if (!blocked && e.key.scancode == SDL_SCANCODE_DELETE)
        {
            bind_source(0);
        }
        else if (!blocked && (binding_keyboard || page == Page::Hotkeys))
        {
            binding_keyboard = true;
            bind_source(e.key.scancode);
        }
        return;
    }
    if ((page == Page::Bindings || page == Page::Hotkeys) && e.key.scancode == SDL_SCANCODE_DELETE &&
        selected < static_cast<int>(((page == Page::Hotkeys) ? hotkeys : bindings).rows().size()))
    {
        bind_source(0);
        return;
    }
    if (e.key.scancode == SDL_SCANCODE_ESCAPE)
    {
        if (paused())
        {
            back();
        }
        else
        {
            open_menu();
        }
        return;
    }
    if (!paused())
    {
        // Retain quick taps even when down/up both arrive between core frames.
        if (!blocked)
        {
            for (std::size_t i = 0; i < hotkey_pressed.size(); ++i)
            {
                const auto& binding = hotkeys.rows()[i];
                if (binding.key == e.key.scancode || std::find(binding.extra_keys.begin(), binding.extra_keys.end(),
                                                               e.key.scancode) != binding.extra_keys.end())
                {
                    hotkey_pressed[i] = true;
                }
            }
        }
        return;
    }
    if (page == Page::Shaders && e.key.scancode == SDL_SCANCODE_DELETE)
    {
        toggle_shader();
        return;
    }
    if (page == Page::Shaders && e.key.scancode == SDL_SCANCODE_M)
    {
        move_shader();
        return;
    }
    switch (e.key.scancode)
    {
    case SDL_SCANCODE_UP:
        navigate(-1);
        break;
    case SDL_SCANCODE_DOWN:
        navigate(1);
        break;
    case SDL_SCANCODE_LEFT:
        adjust(-1);
        break;
    case SDL_SCANCODE_RIGHT:
        adjust(1);
        break;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_X:
        activate();
        break;
    case SDL_SCANCODE_BACKSPACE:
    case SDL_SCANCODE_Z:
        back();
        break;
    default:
        break;
    }
}

void Frontend::update(bool focused, std::uint64_t now)
{
    update_rumble(now);
    fast_forward_held = false;
    game.fill(0);
    snapshot = {};
    gameplay_ready = false;
    if (!focused && pause_on_focus_loss)
    {
        blocked = true;
        chord.clear();
        previous.fill(false);
        return;
    }
    auto button = [&](SDL_GamepadButton b)
    {
        return pad && SDL_GetGamepadButton(pad, b);
    };
    const int x = pad ? SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) : 0;
    const int y = pad ? SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) : 0;
    constexpr int deadzone = 16000;
    const std::array<bool, 6> nav{button(SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -deadzone,
                                  button(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > deadzone,
                                  button(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -deadzone,
                                  button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > deadzone,
                                  button(SDL_GAMEPAD_BUTTON_SOUTH),
                                  button(SDL_GAMEPAD_BUTTON_EAST)};
    const std::array<bool, SDL_SCANCODE_COUNT> no_keys{};
    const bool* keys = focused ? SDL_GetKeyboardState(nullptr) : no_keys.data();
    bool held = std::any_of(nav.begin(), nav.end(),
                            [](bool v)
                            {
                                return v;
                            });
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
    {
        if (b != SDL_GAMEPAD_BUTTON_GUIDE || home_shortcut)
        {
            held |= button(static_cast<SDL_GamepadButton>(b));
        }
    }
    for (int k = 1; k < SDL_SCANCODE_COUNT; ++k)
    {
        held |= keys[k];
    }
    if (pad)
    {
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a)
        {
            held |= std::abs(static_cast<int>(SDL_GetGamepadAxis(pad, static_cast<SDL_GamepadAxis>(a)))) > 8000;
        }
    }
    if (blocked)
    {
        if (!held)
        {
            blocked = false;
        }
        previous_hotkeys =
            hotkeys.sample(pad, focused ? hotkey_keys.data() : no_keys.data(), 0, stick_deadzone, trigger_deadzone)
                .buttons;
        previous = nav;
        previous_clear = button(SDL_GAMEPAD_BUTTON_NORTH);
        previous_move = button(SDL_GAMEPAD_BUTTON_WEST);
        return;
    }
    auto result = home_shortcut
                      ? chord.update(button(SDL_GAMEPAD_BUTTON_GUIDE), button(SDL_GAMEPAD_BUTTON_GUIDE))
                      : chord.update(button(stick_shortcut ? SDL_GAMEPAD_BUTTON_LEFT_STICK : SDL_GAMEPAD_BUTTON_START),
                                     button(stick_shortcut ? SDL_GAMEPAD_BUTTON_RIGHT_STICK : SDL_GAMEPAD_BUTTON_BACK));
    if (result.open)
    {
        if (paused())
        {
            back();
        }
        else
        {
            open_menu();
        }
        blocked = true;
        return;
    }
    if ((page == Page::Bindings || page == Page::Hotkeys) && capturing)
    {
        if (!binding_keyboard || page == Page::Hotkeys)
        {
            int source = InputBindings::capture(pad);
            if (source)
            {
                binding_keyboard = false;
                bind_source(source);
            }
        }
        previous = nav;
        return;
    }
    const bool clear = button(SDL_GAMEPAD_BUTTON_NORTH);
    const bool move = button(SDL_GAMEPAD_BUTTON_WEST);
    if (page == Page::Shaders)
    {
        if (clear && !previous_clear)
        {
            toggle_shader();
        }
        if (move && !previous_move)
        {
            move_shader();
        }
    }
    previous_move = move;
    if ((page == Page::Bindings || page == Page::Hotkeys) && !capturing && clear && !previous_clear &&
        selected < static_cast<int>(((page == Page::Hotkeys) ? hotkeys : bindings).rows().size()))
    {
        bind_source(0);
        previous_clear = true;
        return;
    }
    previous_clear = clear;
    if (paused())
    {
        int direction = nav[0] ? -1 : nav[1] ? 1 : 0;
        if (direction && (direction != repeat_direction || now >= repeat_at))
        {
            navigate(direction);
            repeat_at = now + ((direction != repeat_direction) ? 350 : 120);
        }
        repeat_direction = direction;
        if (nav[2] && !previous[2])
        {
            adjust(-1);
        }
        if (nav[3] && !previous[3])
        {
            adjust(1);
        }
        const int confirm_button = swap_confirm_cancel ? 5 : 4;
        const int cancel_button = swap_confirm_cancel ? 4 : 5;
        if (nav[confirm_button] && !previous[confirm_button])
        {
            activate();
        }
        else if (nav[cancel_button] && !previous[cancel_button])
        {
            back();
        }
        previous = nav;
        return;
    }
    const auto active_hotkeys =
        hotkeys.sample(pad, focused ? hotkey_keys.data() : no_keys.data(), 0, stick_deadzone, trigger_deadzone).buttons;
    fast_forward_held = active_hotkeys[2];
    if (hotkey_pressed[0] || (active_hotkeys[0] && !previous_hotkeys[0]))
    {
        request_state(Action::Save);
    }
    else if (hotkey_pressed[1] || (active_hotkeys[1] && !previous_hotkeys[1]))
    {
        request_state(Action::Load);
    }
    previous_hotkeys = active_hotkeys;
    hotkey_pressed.fill(false);
    if (paused())
    {
        previous = nav;
        return;
    }
    gameplay_ready = true;
    previous = nav;
}

void Frontend::sample_gameplay(bool focused)
{
    game.fill(0);
    snapshot = {};
    if (!gameplay_ready || blocked || paused() || (!focused && pause_on_focus_loss))
    {
        return;
    }
    const std::array<bool, SDL_SCANCODE_COUNT> no_keys{};
    const bool* keys = focused ? SDL_GetKeyboardState(nullptr) : no_keys.data();
    const std::uint32_t mouse_buttons = focused ? SDL_GetMouseState(nullptr, nullptr) : 0;
    snapshot = bindings.sample(pad, keys, mouse_buttons, stick_deadzone, trigger_deadzone);
    game = snapshot.buttons;
}

std::vector<int> Frontend::visible_core_choices() const
{
    bool gliden64 = true;
    for (const auto& option : core_choices)
    {
        if (option.key == "mupen64plus-rdp-plugin")
        {
            gliden64 = option.values[option.index] == "gliden64";
        }
    }
    std::vector<int> result;
    for (int i = 0; i < static_cast<int>(core_choices.size()); ++i)
    {
        if (!core_choices[i].gliden64_only || gliden64)
        {
            result.push_back(i);
        }
    }
    return result;
}
void Frontend::normalize_core_choices()
{
    bool needs_lle = false;
    for (const auto& choice : core_choices)
    {
        if (choice.key == "mupen64plus-rdp-plugin")
        {
            needs_lle = choice.values[choice.index] != "gliden64";
        }
    }
    if (needs_lle)
    {
        for (auto& rsp : core_choices)
        {
            if (rsp.key == "mupen64plus-rsp-plugin" && rsp.values[rsp.index] == "hle")
            {
                for (std::size_t i = 0; i < rsp.values.size(); ++i)
                {
                    if (rsp.values[i] == "parallel" || rsp.values[i] == "cxd4")
                    {
                        rsp.index = i;
                        break;
                    }
                }
            }
        }
    }
}
void Frontend::cycle_core_choice(int index, int direction)
{
    auto& option = core_choices[index];
    const int previous_index = option.index;
    bool needs_lle = false;
    for (const auto& choice : core_choices)
    {
        if (choice.key == "mupen64plus-rdp-plugin")
        {
            needs_lle = choice.values[choice.index] != "gliden64";
        }
    }
    for (std::size_t tries = 0; tries < option.values.size(); ++tries)
    {
        option.index = (option.index + direction + static_cast<int>(option.values.size())) % option.values.size();
        if (option.key != "mupen64plus-rsp-plugin" || !needs_lle || option.values[option.index] != "hle")
        {
            break;
        }
    }
    normalize_core_choices();
    if (option.index != previous_index)
    {
        mark_core_settings_changed();
    }
    if (option.restart_required)
    {
        notify("Restart game to apply " + option.name);
    }
}

bool Frontend::take_core_settings_changed()
{
    const bool changed = core_settings_changed;
    core_settings_changed = false;
    return changed;
}

std::vector<int> Frontend::visible_all_core_options() const
{
    std::vector<int> result;
    for (int i = 0; i < static_cast<int>(all_core_options.size()); ++i)
    {
        if (all_core_options[i].visible)
        {
            result.push_back(i);
        }
    }
    return result;
}
const Frontend::CoreChoice* Frontend::current_core_option() const
{
    for (const auto& option : all_core_options)
    {
        if (option.key == selected_core_option)
        {
            return &option;
        }
    }
    return nullptr;
}
Frontend::CoreChoice* Frontend::current_core_option()
{
    return const_cast<CoreChoice*>(std::as_const(*this).current_core_option());
}

std::vector<std::string> Frontend::rows() const
{
    switch (page)
    {
    case Page::Main:
        return {"Resume", "Save state", "Load state", "Restart game", "Configuration", "Interface", "Quit"};
    case Page::Configuration:
        return {
            "Input", "Video", "Audio", "Advanced", game_config ? "Reset to inherited settings" : "Create game config",
            "Back"};
    case Page::Advanced:
    {
        std::vector<std::string> result{std::string("VRR: ") + (vrr ? "on" : "off"),
                                        std::string("VSync: ") + (vsync ? "on" : "off")};
        if (runahead_available)
        {
            result.push_back("Run-ahead: " + (runahead_frames == 0 ? std::string("off")
                                             : std::to_string(runahead_frames) + (runahead_frames == 1 ? " frame" : " frames")));
        }
        for (int index : visible_core_choices())
        {
            const auto& option = core_choices[index];
            result.push_back(option.name + ": " + option.labels[option.index] +
                             (option.restart_required ? " (restart required)" : ""));
        }
        result.push_back("More Core options");
        result.push_back("Back");
        return result;
    }
    case Page::CoreOptions:
    {
        std::vector<std::string> result;
        for (int index : visible_all_core_options())
        {
            const auto& option = all_core_options[index];
            result.push_back(option.name + ": " + option.labels[option.index]);
        }
        result.push_back("Back");
        return result;
    }
    case Page::CoreOptionValues:
    {
        std::vector<std::string> result;
        if (const auto* option = current_core_option())
        {
            for (int i = 0; i < static_cast<int>(option->values.size()); ++i)
            {
                result.push_back(option->labels[i] + ((i == option->index) ? " (current)" : ""));
            }
        }
        result.push_back("Back");
        return result;
    }
    case Page::Controls:
    {
        std::vector<std::string> result{"Gamepad bindings",
                                        "Keyboard bindings",
                                        "Hotkeys",
                                        std::string("Rumble: ") + (rumble_enabled ? "on" : "off")};
        if (bindings.has_analog_input())
        {
            result.push_back("Analog stick deadzone: " + std::to_string(stick_deadzone) + "%");
            result.push_back("Trigger deadzone: " + std::to_string(trigger_deadzone) + "%");
        }
        result.push_back("Restore system defaults");
        if (bindings.system() == "wii")
        {
            result.insert(result.begin(), std::string("Controller: ") + wii_controller::labels[wii_controller]);
        }
        result.push_back("Back");
        return result;
    }
    case Page::Hotkeys:
    {
        std::vector<std::string> result;
        for (const auto& binding : hotkeys.rows())
        {
            std::string source;
            auto append = [&](const std::string& name)
            {
                if (!source.empty())
                {
                    source += " / ";
                }
                source += name;
            };
            if (binding.key)
            {
                append(SDL_GetScancodeName(binding.key));
            }
            for (int key : binding.extra_keys)
            {
                append(SDL_GetScancodeName(static_cast<SDL_Scancode>(key)));
            }
            if (binding.pad)
            {
                append(InputBindings::source_name(binding.pad));
            }
            for (int pad : binding.extra_pads)
            {
                append(InputBindings::source_name(pad));
            }
            result.push_back(binding.name + ": " + (source.empty() ? "Unbound" : source));
        }
        result.push_back("Back");
        return result;
    }
    case Page::Bindings:
    {
        std::vector<std::string> result;
        for (const auto& binding : bindings.rows())
        {
            std::string source =
                binding_keyboard ? InputBindings::key_name(binding.key) : InputBindings::source_name(binding.pad);
            const auto& extras = binding_keyboard ? binding.extra_keys : binding.extra_pads;
            for (int input : extras)
            {
                source +=
                    " / " + (binding_keyboard ? InputBindings::key_name(input) : InputBindings::source_name(input));
            }
            result.push_back(binding.name + ": " + source);
        }
        result.push_back("Back");
        return result;
    }
    case Page::Video:
    {
        std::vector<std::string> result{fullscreen ? "Fullscreen: on" : "Fullscreen: off",
                                        std::string("Scaling: ") + ((scaling == 3)   ? "Integer"
                                                                    : (scaling == 2) ? "Sharp Bilinear"
                                                                    : (scaling == 1) ? "Bilinear"
                                                                                     : "Nearest neighbor")};
        if (!resolution_values.empty())
        {
            result.push_back("Internal resolution: " + resolution_values[resolution_index] +
                             (resolution_needs_restart ? " (restart required)" : ""));
        }
        result.push_back(std::string("Aspect ratio: ") + aspect_ratio::name(aspect_mode));
        result.push_back("Shaders");
        result.push_back("Back");
        return result;
    }
    case Page::Shaders:
    {
        std::vector<std::string> result;
        for (const auto& shader : shaders.effects)
        {
            result.push_back(std::string(shaders::name(shader.effect)) + (shader.enabled ? ": on" : ": off"));
        }
        result.push_back("Back");
        return result;
    }
    case Page::ShaderParameters:
    {
        std::vector<std::string> result;
        const auto& shader = shaders.effects.at(shader_selection);
        for (std::size_t i = 0; i < shaders::color_parameters.size(); ++i)
        {
            result.push_back(std::string(shaders::color_parameters[i].name) + ": " +
                             std::to_string(shader.parameters[i]) + "%");
        }
        result.push_back("Restore defaults");
        result.push_back("Back");
        return result;
    }
    case Page::ShaderParameterValues:
    {
        std::vector<std::string> result;
        const auto& parameter = shaders::color_parameters[shader_parameter];
        const int current = shaders.effects.at(shader_selection).parameters[shader_parameter];
        for (int value = parameter.minimum; value <= parameter.maximum; value += parameter.step)
        {
            result.push_back(std::to_string(value) + "%" + ((value == current) ? " (current)" : ""));
        }
        result.push_back("Back");
        return result;
    }
    case Page::Interface:
        return {pause_on_focus_loss ? "Pause on focus loss: on" : "Pause on focus loss: off",
                hide_cursor ? "Hide mouse cursor: on" : "Hide mouse cursor: off",
                home_shortcut    ? "Menu: Home / Guide"
                : stick_shortcut ? "Menu: L3 + R3"
                                 : "Menu: Start + Select/View",
                swap_confirm_cancel ? "Swap confirm/cancel: on" : "Swap confirm/cancel: off",
                confirm_save_states ? "Confirm state actions: on" : "Confirm state actions: off",
                show_stats ? "Show stats: on" : "Show stats: off",
                "Back"};
    case Page::Audio:
        return {"Volume: " + std::to_string(volume) + "%", "Back"};
    case Page::Confirm:
        return {"Cancel", "Confirm"};
    default:
        return {};
    }
}
void Frontend::navigate(int direction)
{
    if (page == Page::Shaders && shader_move)
    {
        selected = shaders.move(selected, direction);
        return;
    }
    int count = static_cast<int>(rows().size());
    if (count)
    {
        selected = (selected + direction + count) % count;
    }
}
void Frontend::discard_shader_move()
{
    if (shader_move)
    {
        shaders = *shader_move;
        shader_move.reset();
        selected = shader_selection;
    }
}
void Frontend::toggle_shader()
{
    if (page != Page::Shaders || shader_move || selected < 0 || selected >= static_cast<int>(shaders.effects.size()))
    {
        return;
    }
    if (!shaders_supported)
    {
        notify("Shaders require an OpenGL, OpenGL ES, or Vulkan renderer");
        return;
    }
    selected = shaders.toggle(selected);
}
void Frontend::move_shader()
{
    if (page != Page::Shaders || selected < 0 || selected >= static_cast<int>(shaders.effects.size()))
    {
        return;
    }
    if (shader_move)
    {
        shader_move.reset();
    }
    else
    {
        shader_selection = selected;
        shader_move = shaders;
    }
}
void Frontend::confirm(Action action)
{
    stop_rumble();
    fast_forward_held = false;
    game.fill(0);
    snapshot = {};
    return_page = page;
    return_selection = selected;
    confirmation = action;
    page = Page::Confirm;
    selected = 0;
    blocked = true;
}
void Frontend::request_state(Action action)
{
    if (!can_save)
    {
        notify("This core does not support save states");
    }
    else if (action == Action::Load && !has_save)
    {
        notify("No saved state for this game and core");
    }
    else if (confirm_save_states && (action == Action::Load || has_save))
    {
        confirm(action);
    }
    else
    {
        pending = action;
    }
}
void Frontend::cycle_runahead(int direction)
{
    runahead_frames = (runahead_frames + direction + 4) % 4;
    if (!runahead_option_key.empty())
    {
        pending_core_option = std::pair{runahead_option_key, runahead_values[runahead_frames]};
        mark_core_settings_changed();
    }
}

void Frontend::back()
{
    if (page == Page::Shaders && shader_move)
    {
        discard_shader_move();
    }
    else if (page == Page::ShaderParameterValues)
    {
        page = Page::ShaderParameters;
        selected = shader_parameter;
        blocked = true;
    }
    else if (page == Page::ShaderParameters)
    {
        page = Page::Shaders;
        selected = shader_selection;
        blocked = true;
    }
    else if (page == Page::Shaders)
    {
        page = Page::Video;
        selected = resolution_values.empty() ? 3 : 4;
        blocked = true;
    }
    else if (page == Page::CoreOptionValues)
    {
        page = Page::CoreOptions;
        selected = std::min(core_options_selection, static_cast<int>(visible_all_core_options().size()));
        blocked = true;
    }
    else if (page == Page::CoreOptions)
    {
        page = Page::Advanced;
        selected = 2 + static_cast<int>(runahead_available) + visible_core_choices().size();
        blocked = true;
    }
    else if (page == Page::Hotkeys)
    {
        if (capturing)
        {
            capturing = false;
            blocked = true;
        }
        else
        {
            page = Page::Controls;
            selected = 2 + (bindings.system() == "wii" ? 1 : 0);
            blocked = true;
        }
    }
    else if (page == Page::Bindings)
    {
        if (capturing)
        {
            capturing = false;
            blocked = true;
        }
        else
        {
            page = Page::Controls;
            selected = (binding_keyboard ? 1 : 0) + (bindings.system() == "wii" ? 1 : 0);
            blocked = true;
        }
    }
    else if (page == Page::Main)
    {
        resume();
    }
    else if (page == Page::Confirm)
    {
        page = return_page;
        selected = return_selection;
        blocked = true;
    }
    else if (page == Page::Interface)
    {
        page = Page::Main;
        selected = 5;
        blocked = true;
    }
    else if (page == Page::Controls || page == Page::Video || page == Page::Audio || page == Page::Advanced)
    {
        selected = (page == Page::Controls) ? 0 : (page == Page::Video) ? 1 : (page == Page::Audio) ? 2 : 3;
        page = Page::Configuration;
        blocked = true;
    }
    else
    {
        selected = 4;
        page = Page::Main;
        blocked = true;
    }
}
int Frontend::controls_row() const
{
    const int row = selected - (bindings.system() == "wii" ? 1 : 0);
    return (row >= 4 && !bindings.has_analog_input()) ? row + 2 : row;
}
void Frontend::adjust(int direction)
{
    if (page == Page::ShaderParameters && selected >= 0 && selected < static_cast<int>(shaders::color_parameters.size()))
    {
        auto& value = shaders.effects.at(shader_selection).parameters[selected];
        const auto& parameter = shaders::color_parameters[selected];
        value = std::clamp(value + direction * parameter.step, parameter.minimum, parameter.maximum);
        return;
    }
    if (page == Page::Controls)
    {
        const int row = controls_row();
        if (row == 4 || row == 5)
        {
            auto& deadzone = (row == 4) ? stick_deadzone : trigger_deadzone;
            deadzone = std::clamp(deadzone + direction, 0, 30);
            return;
        }
    }
    if (page == Page::Controls && selected == 3 + (bindings.system() == "wii" ? 1 : 0))
    {
        activate();
        return;
    }
    if (page == Page::Video && selected == 2 && !resolution_values.empty())
    {
        const int previous_index = resolution_index;
        resolution_index = std::clamp(resolution_index + direction, 0, static_cast<int>(resolution_values.size()) - 1);
        if (resolution_index != previous_index)
        {
            mark_core_settings_changed();
        }
    }
    else if (page == Page::Video && selected == 1)
    {
        scaling = (scaling + direction + 4) % 4;
    }
    else if (page == Page::Video && selected == (resolution_values.empty() ? 2 : 3))
    {
        aspect_mode = (aspect_mode + direction + aspect_ratio::Count) % aspect_ratio::Count;
    }
    else if (page == Page::Controls && bindings.system() == "wii" && selected == 0)
    {
        wii_controller = (wii_controller + direction + wii_controller::count) % wii_controller::count;
        bindings.select_wii_controller(wii_controller);
    }
    else if (page == Page::Audio && selected == 0)
    {
        volume = std::clamp(volume + direction * 10, 0, 100);
    }
    else if (page == Page::Advanced && selected < 2)
    {
        activate();
    }
    else if (page == Page::Advanced && runahead_available && selected == 2)
    {
        cycle_runahead(direction);
    }
    else if (page == Page::Advanced)
    {
        const auto choices = visible_core_choices();
        const int row = selected - 2 - static_cast<int>(runahead_available);
        if (row >= 0 && row < static_cast<int>(choices.size()))
        {
            cycle_core_choice(choices[row], direction);
        }
    }
    else if ((page == Page::Video && (selected < 2 || selected == (resolution_values.empty() ? 2 : 3))) ||
             (page == Page::Interface && selected < 6))
    {
        activate();
    }
}
void Frontend::activate()
{
    switch (page)
    {
    case Page::Shaders:
        if (shader_move)
        {
            shader_move.reset();
        }
        else if (selected >= 0 && selected < static_cast<int>(shaders.effects.size()))
        {
            if (!shaders.effects[selected].enabled)
            {
                break;
            }
            shader_selection = selected;
            page = Page::ShaderParameters;
            selected = 0;
            blocked = true;
        }
        else
        {
            back();
        }
        break;
    case Page::ShaderParameters:
        if (selected >= 0 && selected < static_cast<int>(shaders::color_parameters.size()))
        {
            shader_parameter = selected;
            const auto& parameter = shaders::color_parameters[selected];
            selected = (shaders.effects.at(shader_selection).parameters[selected] - parameter.minimum) / parameter.step;
            page = Page::ShaderParameterValues;
            blocked = true;
        }
        else if (selected == static_cast<int>(shaders::color_parameters.size()))
        {
            shaders.effects.at(shader_selection).parameters = shaders::Shader{}.parameters;
        }
        else
        {
            back();
        }
        break;
    case Page::ShaderParameterValues:
    {
        const auto& parameter = shaders::color_parameters[shader_parameter];
        const int value = parameter.minimum + selected * parameter.step;
        if (value >= parameter.minimum && value <= parameter.maximum)
        {
            shaders.effects.at(shader_selection).parameters[shader_parameter] = value;
        }
        back();
        break;
    }
    case Page::Main:
        switch (selected)
        {
        case 0:
            resume();
            break;
        case 1:
            request_state(Action::Save);
            break;
        case 2:
            request_state(Action::Load);
            break;
        case 3:
            confirm(Action::Reset);
            break;
        case 4:
            page = Page::Configuration;
            selected = 0;
            blocked = true;
            break;
        case 5:
            page = Page::Interface;
            selected = 0;
            blocked = true;
            break;
        case 6:
            confirm(Action::Quit);
            break;
        }
        break;
    case Page::Configuration:
        if (selected == 5)
        {
            back();
        }
        else if (selected == 4)
        {
            if (game_config)
            {
                confirm(Action::ResetGameConfig);
            }
            else
            {
                pending = Action::CreateGameConfig;
            }
        }
        else
        {
            page = (selected == 0)   ? Page::Controls
                   : (selected == 1) ? Page::Video
                   : (selected == 2) ? Page::Audio
                                     : Page::Advanced;
            selected = 0;
            blocked = true;
        }
        break;
    case Page::Advanced:
        if (selected == 0)
        {
            vrr = !vrr;
        }
        else if (selected == 1)
        {
            vsync = !vsync;
        }
        else if (runahead_available && selected == 2)
        {
            cycle_runahead(1);
        }
        else
        {
            const auto choices = visible_core_choices();
            const int row = selected - 2 - static_cast<int>(runahead_available);
            if (row >= 0 && row < static_cast<int>(choices.size()))
            {
                cycle_core_choice(choices[row], 1);
            }
            else if (row == static_cast<int>(choices.size()))
            {
                page = Page::CoreOptions;
                selected = 0;
                blocked = true;
            }
            else
            {
                back();
            }
        }
        break;
    case Page::CoreOptions:
    {
        const auto options = visible_all_core_options();
        if (selected >= 0 && selected < static_cast<int>(options.size()))
        {
            auto& option = all_core_options[options[selected]];
            core_options_selection = selected;
            selected_core_option = option.key;
            page = Page::CoreOptionValues;
            selected = option.index;
            blocked = true;
        }
        else
        {
            back();
        }
        break;
    }
    case Page::CoreOptionValues:
        if (auto* option = current_core_option();
            option && selected >= 0 && selected < static_cast<int>(option->values.size()))
        {
            if (option->index != selected)
            {
                option->index = selected;
                pending_core_option = {option->key, option->values[selected]};
                mark_core_settings_changed();
            }
        }
        back();
        break;
    case Page::Confirm:
        if (selected == 1)
        {
            if (confirmation == Action::RestoreBindings)
            {
                try
                {
                    bindings.reset();
                    hotkeys.reset();
                    stick_deadzone = trigger_deadzone = 5;
                    notify("System defaults restored");
                }
                catch (const std::exception& error)
                {
                    notify(error.what());
                }
            }
            else
            {
                pending = confirmation;
            }
        }
        page = return_page;
        selected = return_selection;
        blocked = true;
        break;
    case Page::Controls:
    {
        const int row = controls_row();
        if (row < 0)
        {
            wii_controller = (wii_controller + 1) % wii_controller::count;
            bindings.select_wii_controller(wii_controller);
        }
        else if (row < 2)
        {
            binding_keyboard = row == 1;
            page = Page::Bindings;
            selected = 0;
            blocked = true;
        }
        else if (row == 2)
        {
            page = Page::Hotkeys;
            selected = 0;
            blocked = true;
        }
        else if (row == 3)
        {
            rumble_enabled = !rumble_enabled;
            if (!rumble_enabled)
            {
                stop_rumble();
            }
        }
        else if (row == 4 || row == 5)
        {
            adjust(1);
        }
        else if (row == 6)
        {
            confirm(Action::RestoreBindings);
        }
        else
        {
            back();
        }
        break;
    }
    case Page::Hotkeys:
    case Page::Bindings:
        if (selected == static_cast<int>(((page == Page::Hotkeys) ? hotkeys : bindings).rows().size()))
        {
            back();
        }
        else
        {
            capturing = true;
            blocked = true;
        }
        break;
    case Page::Video:
        if (selected == 0)
        {
            fullscreen = !fullscreen;
        }
        else if (selected == 1)
        {
            scaling = (scaling + 1) % 4;
        }
        else if (selected == 2 && !resolution_values.empty())
        {
            const int previous_index = resolution_index;
            resolution_index = (resolution_index + 1) % resolution_values.size();
            if (resolution_index != previous_index)
            {
                mark_core_settings_changed();
            }
        }
        else if (selected == (resolution_values.empty() ? 2 : 3))
        {
            aspect_mode = (aspect_mode + 1) % aspect_ratio::Count;
        }
        else if (selected == (resolution_values.empty() ? 3 : 4))
        {
            page = Page::Shaders;
            selected = 0;
            blocked = true;
        }
        else
        {
            back();
        }
        break;
    case Page::Interface:
        if (selected == 0)
        {
            pause_on_focus_loss = !pause_on_focus_loss;
        }
        else if (selected == 1)
        {
            hide_cursor = !hide_cursor;
        }
        else if (selected == 2)
        {
            if (home_shortcut)
            {
                home_shortcut = false;
                stick_shortcut = false;
            }
            else if (stick_shortcut)
            {
                stick_shortcut = false;
                home_shortcut = true;
                notify("Home / Guide may be intercepted by the OS");
            }
            else
            {
                stick_shortcut = true;
            }
            chord.clear();
            blocked = true;
        }
        else if (selected == 3)
        {
            swap_confirm_cancel = !swap_confirm_cancel;
            blocked = true;
        }
        else if (selected == 4)
        {
            confirm_save_states = !confirm_save_states;
        }
        else if (selected == 5)
        {
            show_stats = !show_stats;
        }
        else
        {
            back();
        }
        break;
    case Page::Audio:
        if (selected == 0)
        {
            volume = (volume == 100) ? 0 : volume + 10;
        }
        else
        {
            back();
        }
        break;
    default:
        break;
    }
}
void Frontend::bind_source(int source)
{
    try
    {
        auto& profile = (page == Page::Hotkeys) ? hotkeys : bindings;
        if (page == Page::Hotkeys && !source)
        {
            profile.assign(selected, true, 0);
            profile.assign(selected, false, 0);
        }
        else
        {
            profile.add(static_cast<std::size_t>(selected), binding_keyboard, source);
        }
        notify("Binding saved");
    }
    catch (const std::exception& error)
    {
        notify(error.what());
    }
    capturing = false;
    blocked = true;
}
Action Frontend::take_action()
{
    Action action = pending;
    pending = Action::None;
    return action;
}

std::string Frontend::label(SDL_GamepadButton b) const
{
    if (!pad)
    {
        return (b == SDL_GAMEPAD_BUTTON_SOUTH) ? "Enter" : "Esc";
    }
    switch (SDL_GetGamepadButtonLabel(pad, b))
    {
    case SDL_GAMEPAD_BUTTON_LABEL_A:
        return "A";
    case SDL_GAMEPAD_BUTTON_LABEL_B:
        return "B";
    case SDL_GAMEPAD_BUTTON_LABEL_X:
        return "X";
    case SDL_GAMEPAD_BUTTON_LABEL_Y:
        return "Y";
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS:
        return "Cross";
    case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:
        return "Circle";
    case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:
        return "Square";
    case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:
        return "Triangle";
    default:
        return InputBindings::source_name(b + 1);
    }
}

Frontend::MenuAppearance Frontend::menu_appearance(std::uint64_t now) const
{
    MenuAppearance appearance{page,
                              selected,
                              pad != nullptr,
                              swap_confirm_cancel,
                              binding_keyboard,
                              capturing,
                              can_save,
                              has_save,
                              game_config,
                              confirmation,
                              game_title,
                              bindings.system(),
                              (now < notice_until) ? notice : "",
                              show_stats ? stats_text : "",
                              rows(),
                              {}};
    if (page == Page::Shaders)
    {
        appearance.descriptions.push_back(shader_move ? "moving" : "idle");
    }
    if (page == Page::Advanced)
    {
        for (const auto& choice : core_choices)
        {
            appearance.descriptions.push_back(choice.description);
        }
    }
    if (page == Page::CoreOptionValues)
    {
        if (const auto* option = current_core_option())
        {
            appearance.descriptions.push_back(option->name);
            appearance.descriptions.push_back(option->description);
        }
    }
    else if (page == Page::CoreOptions)
    {
        for (const auto& option : all_core_options)
        {
            appearance.descriptions.push_back(option.description);
        }
    }
    return appearance;
}

bool Frontend::needs_redraw(std::uint64_t now) const
{
    return !paused() || !presented_menu || *presented_menu != menu_appearance(now);
}

std::uint64_t Frontend::next_menu_update(std::uint64_t now) const
{
    std::uint64_t deadline = now + 1000;
    if (!notice.empty() && notice_until > now)
    {
        deadline = std::min(deadline, notice_until);
    }
    if (!blocked && repeat_direction != 0)
    {
        deadline = std::min(deadline, repeat_at);
    }
    return deadline;
}

void Frontend::draw(SDL_Renderer* renderer, TextRenderer& font, int width, int height)
{
    const auto draw_now = SDL_GetTicks();
    mouse_targets.clear();
    mouse_page = page;
    if (width <= 0 || height <= 0)
    {
        return;
    }
    if (auto* window = SDL_GetRenderWindow(renderer))
    {
        mouse_window = SDL_GetWindowID(window);
        int window_w = 0, window_h = 0;
        if (SDL_GetWindowSize(window, &window_w, &window_h) && window_w > 0 && window_h > 0)
        {
            mouse_scale_x = float(width) / window_w;
            mouse_scale_y = float(height) / window_h;
        }
    }
    SDL_SetRenderScale(renderer, 1, 1);
    const float w = static_cast<float>(width), h = static_cast<float>(height);
    const float scale = std::min(w / 1280.0f, h / 720.0f);
    const int body = std::max(18, static_cast<int>(24 * scale));
    const int small = std::max(16, static_cast<int>(21 * scale));
    const int heading_size = std::max(24, static_cast<int>(32 * scale));
    const float margin = std::max(24.0f, w * .045f);
    const bool compact = w < 760;
    const float bar_height = compact ? small * 2.0f + 34 : std::max(44.0f, 44 * scale);
    auto box = [&](SDL_FRect rect, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
    {
        SDL_SetRenderDrawColor(renderer, r, g, b, a);
        SDL_RenderFillRect(renderer, &rect);
    };
    const SDL_Color white{255, 255, 255, 255}, gray{187, 187, 187, 255};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    if (paused())
    {
        if (!video_preview())
        {
            box({0, 0, w, h}, 0, 0, 0, 165);
        }
        auto menu_text =
            [&](float x, float y, const std::string& value, int size, SDL_Color color, float max_width = 10000)
        {
            if (video_preview())
            {
                const float offset = std::max(1.0f, 2 * scale);
                font.draw(x + offset, y + offset, value, size, {0, 0, 0, 220}, max_width);
            }
            font.draw(x, y, value, size, color, max_width);
        };
        const float bar_top = h - bar_height;
        const float left = std::max(margin + 22, w * .15f);
        const std::string scope = game_config ? game_title : system_display_name(bindings.system());
        std::string heading = game_title;
        switch (page)
        {
        case Page::Configuration:
            heading = "Configuration / " + scope;
            break;
        case Page::Controls:
            heading = "Input / " + scope;
            break;
        case Page::Hotkeys:
            heading = "Hotkeys / " + scope;
            break;
        case Page::Bindings:
            heading = std::string(binding_keyboard ? "Keyboard bindings / " : "Gamepad bindings / ") + scope;
            break;
        case Page::Shaders:
            heading = "Shaders / " + scope;
            break;
        case Page::ShaderParameters:
            heading = shaders::name(shaders.effects.at(shader_selection).effect);
            break;
        case Page::ShaderParameterValues:
            heading = shaders::color_parameters[shader_parameter].name;
            break;
        case Page::Video:
            heading = "Video / " + scope;
            break;
        case Page::Interface:
            heading = "Interface";
            break;
        case Page::Audio:
            heading = "Audio / " + scope;
            break;
        case Page::Advanced:
            heading = "Advanced / " + scope;
            break;
        case Page::CoreOptions:
            heading = "More Core options / " + scope;
            break;
        case Page::CoreOptionValues:
            heading = (current_core_option() ? current_core_option()->name : "Core option") + " / " + scope;
            break;
        case Page::Confirm:
            heading =
                (confirmation == Action::ResetGameConfig) ? "Reset this game to inherited settings?"
                : (confirmation == Action::RestoreBindings)
                    ? (game_config ? "Replace this game's custom bindings?" : "Replace this system's custom bindings?")
                : (confirmation == Action::Save)  ? "Overwrite the existing save state?"
                : (confirmation == Action::Load)  ? "Load? Unsaved progress will be lost"
                : (confirmation == Action::Reset) ? "Restart? Unsaved progress will be lost"
                                                  : "Quit? Unsaved progress will be lost";
            break;
        default:
            break;
        }
        const float row_height = body * 1.55f;
        const float header_y = std::max({h * .08f, small + 28.0f, bar_height + body * .5f}) + h * .02f;
        const float list_top = std::max(h * .21f, header_y + heading_size + body * .5f);
        menu_text(left, header_y, heading, heading_size, white, w - left - margin);
        const auto items = rows();
        const int visible = std::max(1, static_cast<int>((bar_top - list_top - 18) / row_height));
        const int first = selected / visible * visible;
        const int last = std::min(first + visible, static_cast<int>(items.size()));
        for (int i = first; i < last; ++i)
        {
            const float y = list_top + (i - first) * row_height;
            mouse_targets.push_back({{left - body, y, w - margin - left + body, row_height}, i});
            if (i == selected)
            {
                menu_text(left - body, y - body * .12f, "•", body, white);
            }
            const bool inactive = page == Page::Shaders && i < static_cast<int>(shaders.effects.size()) &&
                                  !shaders.effects[i].enabled;
            menu_text(left, y, items[i], body, inactive ? SDL_Color{112, 112, 112, 255} : (i == selected) ? white : gray,
                      w - left - margin);
        }
        std::string description;
        if (page == Page::Main)
        {
            const std::array<const char*, 7> descriptions{"Continue playing",
                                                          "Save your current progress",
                                                          "Restore your saved progress",
                                                          "Restart the game from the beginning",
                                                          "Configure controls, video and audio",
                                                          "Change global interface settings",
                                                          "Close the game"};
            description = descriptions[std::clamp(selected, 0, 6)];
            if (selected == 1 && !can_save)
            {
                description = "This core does not support save states";
            }
            if (selected == 2 && !has_save)
            {
                description = "No saved state for this game and core";
            }
            if (selected == 2 && !can_save)
            {
                description = "This core does not support save states";
            }
        }
        else if (page == Page::Configuration)
        {
            description = (selected == 0)   ? "Configure keyboard and gamepad bindings"
                          : (selected == 1) ? "Configure display settings"
                          : (selected == 2) ? "Configure sound settings"
                          : (selected == 3) ? "Configure pacing and advanced core settings"
                          : (selected == 4) ? (game_config ? "Remove this game configuration and inherit defaults"
                                                           : "Create game specific settings")
                                            : "Return to the main menu";
        }
        else if (page == Page::CoreOptions)
        {
            const auto options = visible_all_core_options();
            description = (selected >= 0 && selected < static_cast<int>(options.size()))
                              ? all_core_options[options[selected]].description
                              : "Return to Advanced";
        }
        else if (page == Page::CoreOptionValues)
        {
            if (const auto* option = current_core_option())
            {
                description = option->description;
            }
        }
        else if (page == Page::Advanced)
        {
            if (selected == 0)
            {
                description = "Use native content pacing on a display with VRR enabled. Requires VSync on.";
            }
            else if (selected == 1)
            {
                description = "Synchronize presentation to the display. Fast-forward temporarily overrides this.";
            }
            else if (runahead_available && selected == 2)
            {
                description = "Reduce input latency by running ahead up to three frames. Requires more processing power.";
            }
            else
            {
                const auto choices = visible_core_choices();
                const int row = selected - 2 - static_cast<int>(runahead_available);
                description = (row >= 0 && row < static_cast<int>(choices.size()))
                                  ? core_choices[choices[row]].description
                              : (row == static_cast<int>(choices.size())) ? "Browse all options provided by the core"
                                                                          : "Return to Configuration";
            }
        }
        else if (page == Page::Controls)
        {
            const std::array<const char*, 8> descriptions{
                "Change this system's gamepad bindings",
                "Change this system's keyboard bindings",
                "Configure save, load, and hold-to-fast-forward hotkeys",
                "Enable controller vibration when supported",
                "Left / Right: adjust analog stick deadzone in 1% steps (0-30%)",
                "Left / Right: adjust trigger deadzone in 1% steps (0-30%)",
                "Restore this system's default bindings and deadzones",
                "Return to Configuration"};
            description = (bindings.system() == "wii" && selected == 0)
                              ? "Choose the controller emulated by Dolphin"
                              : descriptions[std::clamp(controls_row(), 0, 7)];
        }
        else if (page == Page::Hotkeys)
        {
            description =
                capturing ? "Release controls, then press a keyboard key or gamepad input"
                          : "Select to add an input. Delete / North clears this action. Hold Fast forward to speed up.";
        }
        else if (page == Page::Bindings)
        {
            description = capturing ? "Release controls, then press a new input"
                                    : "Select to add an input. Delete / North clears this action.";
        }
        else if (page == Page::Shaders)
        {
            description = shader_move ? "Move within the enabled or disabled group"
                          : selected < static_cast<int>(shaders.effects.size())
                              ? (shaders.effects[selected].enabled ? "Configure this effect's parameters"
                                                                  : "Enable this effect to configure its parameters")
                          : "Return to Video";
        }
        else if (page == Page::ShaderParameters)
        {
            description = selected < static_cast<int>(shaders::color_parameters.size())
                              ? shaders::color_parameters[selected].description
                          : selected == static_cast<int>(shaders::color_parameters.size()) ? "Restore default shader parameters"
                                                                                         : "Return to Shaders";
        }
        else if (page == Page::ShaderParameterValues)
        {
            description = shaders::color_parameters[shader_parameter].description;
        }
        else if (page == Page::Video)
        {
            const std::array<const char*, 3> descriptions{
                "Toggle fullscreen for this session", "Choose nearest, Bilinear, Sharp Bilinear, or Integer scaling",
                "Return to Configuration"};
            description = (selected == (resolution_values.empty() ? 3 : 4)) ? "Configure the game image shader stack"
                          : (selected == (resolution_values.empty() ? 4 : 5)) ? "Return to Configuration"
                          : (selected == (resolution_values.empty() ? 2 : 3))
                              ? "Choose Auto, 16:9, 4:3, 8:7, or Stretch to fit"
                          : (selected == 2 && !resolution_values.empty())
                              ? (resolution_needs_restart ? "Left / Right: resolution. Restart game to apply it."
                                                          : "Left / Right: resolution. Applies when gameplay resumes.")
                              : descriptions[std::clamp(selected, 0, 2)];
        }
        else if (page == Page::Interface)
        {
            description = (selected == 0)   ? "Pause when game window loses focus"
                          : (selected == 1) ? "Hide the pointer while inactive"
                          : (selected == 2) ? "Choose how to open the in-game menu"
                          : (selected == 3) ? "Swap the gamepad buttons for menu confirm and cancel"
                          : (selected == 4) ? "Ask before loading or overwriting a save state, including hotkeys"
                          : (selected == 5) ? "Show core video FPS, average present interval, and core idle time"
                                            : "Return to the main menu";
        }
        else if (page == Page::Audio)
        {
            description = (selected == 0) ? "Use Left / Right to adjust volume" : "Return to Configuration";
        }
        box({0, bar_top, w, bar_height}, 16, 16, 16, 255);
        const float icon_size = small * 1.35f;
        const float gap = small * .35f;
        const float between = small;
        struct Prompt
        {
            const char* icon;
            const char* title;
            int action;
        };
        std::vector<Prompt> prompts;
        const bool shader_row = page == Page::Shaders && selected < static_cast<int>(shaders.effects.size());
        const bool unbind = (page == Page::Bindings || page == Page::Hotkeys) && !capturing &&
                            selected < static_cast<int>(((page == Page::Hotkeys) ? hotkeys : bindings).rows().size());
        if (unbind)
        {
            prompts.push_back({pad ? "controller_buttons_north" : "keyboard_delete", "Unbind", -4});
        }
        if (shader_row)
        {
            if (!shader_move)
            {
                prompts.push_back({pad ? "controller_buttons_north" : "keyboard_delete", "Toggle", -5});
                prompts.push_back({pad ? "controller_buttons_west" : "", pad ? "Move" : "M: Move", -6});
            }
            else
            {
                prompts.push_back({pad ? "controller_dpad_vertical" : "keyboard_arrows_vertical", "Move", -1});
            }
        }
        prompts.push_back({pad ? (swap_confirm_cancel ? "controller_buttons_south" : "controller_buttons_east") : "keyboard_escape",
                           shader_move ? "Discard" : "Back", -2});
        if (!shader_row || shader_move || shaders.effects[selected].enabled)
        {
            prompts.push_back({pad ? (swap_confirm_cancel ? "controller_buttons_east" : "controller_buttons_south") : "keyboard_enter",
                               shader_move ? "Done" : "Select", -3});
        }
        auto prompt_width = [&](const Prompt& prompt)
        {
            return (*prompt.icon ? icon_size + gap : 0) + font.width(prompt.title, small);
        };
        float x = w - margin + between;
        for (const auto& prompt : prompts)
        {
            x -= prompt_width(prompt) + between;
        }
        const float text_lift = small * .15f;
        const float prompt_y = (compact ? h - small - 14 : bar_top + (bar_height - small) / 2 - 2) - text_lift;
        font.draw(margin, bar_top + (compact ? 6 : (bar_height - small) / 2 - 2) - text_lift, description, small, white,
                  compact ? w - margin * 2 : x - margin - between);
        for (const auto& prompt : prompts)
        {
            const float prompt_size = prompt_width(prompt);
            if (prompt.action != -1)
            {
                mouse_targets.push_back({{x, prompt_y - 4, prompt_size, icon_size + 8}, prompt.action});
            }
            if (*prompt.icon)
            {
                font.icon(x, prompt_y + text_lift - (icon_size - small) / 2 + 2, icon_size, prompt.icon);
                x += icon_size + gap;
            }
            font.draw(x, prompt_y, prompt.title, small, white);
            x += font.width(prompt.title, small) + between;
        }
    }
    if (!notice.empty() && draw_now < notice_until)
    {
        box({0, 0, w, bar_height}, 16, 16, 16, 150);
        font.draw(margin, (bar_height - small) / 2 - 2 - small * .15f, notice, small, white, w - margin * 2);
    }
    if (show_stats && !stats_text.empty())
    {
        const int size = std::max(12, static_cast<int>(14 * scale));
        float y = (!notice.empty() && draw_now < notice_until) ? bar_height + 8 : 8;
        for (std::size_t start = 0; start < stats_text.size();)
        {
            const auto end = stats_text.find('\n', start);
            const auto line = stats_text.substr(start, end - start);
            font.draw(9, y + 1, line, size, {0, 0, 0, 230}, w - 16);
            font.draw(8, y, line, size, white, w - 16);
            if (end == std::string::npos)
            {
                break;
            }
            start = end + 1;
            y += size * 1.3f;
        }
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    if (paused())
    {
        presented_menu = menu_appearance(draw_now);
    }
    else
    {
        presented_menu.reset();
    }
}
