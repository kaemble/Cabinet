#include "frontend.h"
#include "ui_text.h"
#include "virtual_gamepad.h"
#include <libretro.h>
#include <iostream>
#include <stdexcept>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static void key(Frontend& ui, SDL_Scancode code) {
    SDL_Event event{}; event.type = SDL_EVENT_KEY_DOWN; event.key.scancode = code;
    ui.event(event);
}
static SDL_JoystickID attach() {
    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    identify_virtual_gamepad(desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.name = "Cabinet test controller";
    auto id = SDL_AttachVirtualJoystick(&desc);
    require(id != 0, SDL_GetError());
    return id;
}
static void tests() {
    for (const auto* system : {"gb", "gba", "gg", "nes", "nds", "md", "sms", "saturn", "snes", "pce", "wii"})
    {
        Frontend ui;
        const bool wii = std::string_view(system) == "wii";
        ui.wii_controller = wii ? 1 : 0;
        ui.bindings.configure(system, {}, ui.wii_controller);
        require(!ui.bindings.has_analog_input(), "Digital controller advertises analog input");
        ui.page = Page::Controls;
        ui.selected = 4 + (wii ? 1 : 0);
        key(ui, SDL_SCANCODE_RIGHT);
        require(ui.stick_deadzone == 5 && ui.trigger_deadzone == 5, "Hidden deadzones must not be adjustable");
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.page == Page::Confirm, "Digital controls must show Restore defaults after Rumble");
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.page == Page::Controls, "Cancel restore must return to digital controls");
        key(ui, SDL_SCANCODE_DOWN);
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.page == Page::Configuration, "Digital controls Back must follow Restore defaults");
    }
    for (const auto* system : {"gc", "wii"}) {
        Frontend ui; ui.bindings.configure(system, {});
        require(ui.bindings.has_analog_input(), "Analog controller must advertise analog input");
        ui.page = Page::Controls;
        const int offset = std::string_view(system) == "wii" ? 1 : 0;
        ui.selected = 4 + offset;
        require(ui.stick_deadzone == 5 && ui.trigger_deadzone == 5, "Input deadzones must default to 5%");
        key(ui, SDL_SCANCODE_RIGHT);
        require(ui.stick_deadzone == 6 && ui.trigger_deadzone == 5, "Stick deadzone must change independently in 1% steps");
        for (int i = 0; i < 40; ++i) key(ui, SDL_SCANCODE_LEFT);
        require(ui.stick_deadzone == 0, "Stick deadzone must clamp at zero");
        for (int i = 0; i < 40; ++i) key(ui, SDL_SCANCODE_RIGHT);
        require(ui.stick_deadzone == 30, "Stick deadzone must clamp at 30%");
        ui.selected = 5 + offset; key(ui, SDL_SCANCODE_LEFT);
        require(ui.trigger_deadzone == 4 && ui.stick_deadzone == 30, "Trigger deadzone must change independently");
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.trigger_deadzone == 5 && ui.page == Page::Controls, "Confirm must increment a deadzone");
        for (int i = 0; i < 40; ++i) key(ui, SDL_SCANCODE_RIGHT);
        require(ui.trigger_deadzone == 30, "Trigger deadzone must clamp at 30%");
        for (int i = 0; i < 40; ++i) key(ui, SDL_SCANCODE_LEFT);
        require(ui.trigger_deadzone == 0, "Trigger deadzone must clamp at zero");
        ui.selected = 6 + offset; key(ui, SDL_SCANCODE_RETURN);
        require(ui.page == Page::Confirm, "Restoring input defaults must require confirmation");
        key(ui, SDL_SCANCODE_DOWN); key(ui, SDL_SCANCODE_RETURN);
        require(ui.stick_deadzone == 5 && ui.trigger_deadzone == 5, "Restore defaults must reset both deadzones");
    }
    MenuChord chord;
    require(!chord.update(true, false).open, "Start alone must not open menu");
    require(chord.update(true, true).open, "Staggered chord must open menu");
    require(!chord.update(true, true).open, "Held chord must not retrigger");
    require(!chord.update(false, true).open, "Partial chord release must not retrigger");
    require(!chord.update(true, true).open, "Chord must wait for both buttons to release");
    require(!chord.update(false, false).open, "Chord release must not open menu");
    require(!chord.update(false, true).open, "Select alone must not open menu");
    require(chord.update(true, true).open, "Reverse-order chord must open menu");

    isolate_virtual_gamepads();
    require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), SDL_GetError());
    // The same policy applies to menu actions, keyboard shortcuts, and pads.
    for (bool confirmations : {true, false}) for (int source = 0; source < 3; ++source)
        for (int operation = 0; operation < 3; ++operation) {
            const bool load = operation == 2;
            const bool existing = operation != 0;
            const auto action = load ? Action::Load : Action::Save;
            const auto scancode = load ? SDL_SCANCODE_F7 : SDL_SCANCODE_F5;
            const auto button = load ? SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER : SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
            SDL_JoystickID id = 0; SDL_Joystick* joystick = nullptr;
            if (source == 2) {
                id = attach(); joystick = SDL_OpenJoystick(id); require(joystick, SDL_GetError());
                for (int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; ++axis)
                    SDL_SetJoystickVirtualAxis(joystick, axis, axis >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? -32768 : 0);
                SDL_UpdateJoysticks(); SDL_UpdateGamepads();
            }
            {
                Frontend ui;
                require(ui.confirm_save_states, "State confirmations must default to enabled");
                ui.confirm_save_states = confirmations; ui.can_save = true; ui.has_save = existing;
                if (source == 2) {
                    require(ui.discover(), "Confirmation pad not discovered");
                    ui.hotkeys.assign(load ? 1 : 0, false, button + 1);
                }
                auto release = [&] {
                    if (source == 1) { SDL_Event e{}; e.type = SDL_EVENT_KEY_UP; e.key.scancode = scancode; ui.event(e); }
                    if (source == 2) { SDL_SetJoystickVirtualButton(joystick, button, false); SDL_UpdateJoysticks(); SDL_UpdateGamepads(); }
                    ui.update(true, SDL_GetTicks()); ui.sample_gameplay(true);
                };
                auto trigger = [&] {
                    if (source == 0) { ui.open_menu(); ui.selected = load ? 2 : 1; key(ui, SDL_SCANCODE_RETURN); }
                    if (source == 1) { key(ui, scancode); ui.update(true, SDL_GetTicks()); ui.sample_gameplay(true); }
                    if (source == 2) { SDL_SetJoystickVirtualButton(joystick, button, true); SDL_UpdateJoysticks(); SDL_UpdateGamepads(); ui.update(true, SDL_GetTicks()); ui.sample_gameplay(true); }
                };
                release(); trigger();
                const auto original_page = source == 0 ? Page::Main : Page::Game;
                if (confirmations && existing) {
                    require(ui.page == Page::Confirm && ui.selected == 0 && ui.take_action() == Action::None,
                        "State action bypassed confirmation or did not default to Cancel");
                    ui.update(true, SDL_GetTicks()); ui.sample_gameplay(true);
                    require(ui.take_action() == Action::None, "Held shortcut accepted its own confirmation");
                    key(ui, SDL_SCANCODE_ESCAPE); release();
                    require(ui.page == original_page && ui.take_action() == Action::None, "Cancel did not return to the originating page");
                    trigger(); release(); key(ui, SDL_SCANCODE_DOWN); key(ui, SDL_SCANCODE_RETURN);
                    require(ui.take_action() == action && ui.page == original_page, "Confirmed state action was lost");
                } else {
                    require(ui.take_action() == action && ui.page == original_page, "Disabled confirmation or first save unexpectedly prompted");
                }
                release();
            }
            if (joystick) SDL_CloseJoystick(joystick);
            if (id) SDL_DetachVirtualJoystick(id);
        }
    {
        Frontend ui;
        ui.can_save = ui.has_save = true;
        ui.confirm_save_states = false;
        auto release = [&](SDL_Scancode code) {
            SDL_Event e{}; e.type = SDL_EVENT_KEY_UP; e.key.scancode = code; ui.event(e);
            ui.update(true, SDL_GetTicks()); ui.sample_gameplay(true);
        };
        key(ui, SDL_SCANCODE_F5); ui.update(true, SDL_GetTicks()); ui.sample_gameplay(true);
        require(ui.take_action() == Action::Save && !ui.paused(), "F5 did not save directly during gameplay");
        ui.update(true, SDL_GetTicks()); ui.sample_gameplay(true);
        require(ui.take_action() == Action::None, "Held save hotkey repeated");
        release(SDL_SCANCODE_F5);
        key(ui, SDL_SCANCODE_F7); ui.update(true, SDL_GetTicks()); ui.sample_gameplay(true);
        require(ui.take_action() == Action::Load && !ui.paused(), "F7 did not load directly during gameplay");
        release(SDL_SCANCODE_F7);
        key(ui, SDL_SCANCODE_F5); release(SDL_SCANCODE_F5);
        require(ui.take_action() == Action::Save, "Quick save hotkey tap was lost between updates");
        key(ui, SDL_SCANCODE_TAB); ui.update(true, SDL_GetTicks()); ui.sample_gameplay(true);
        require(ui.fast_forward_held, "Tab did not start fast-forward");
        release(SDL_SCANCODE_TAB);
        require(!ui.fast_forward_held, "Fast-forward remained active after release");
        key(ui, SDL_SCANCODE_TAB); ui.update(true, SDL_GetTicks()); ui.sample_gameplay(true);
        SDL_Event focus{}; focus.type = SDL_EVENT_WINDOW_FOCUS_LOST; ui.event(focus);
        require(ui.paused() && !ui.fast_forward_held, "Focus loss left fast-forward active");
        release(SDL_SCANCODE_TAB);
        key(ui, SDL_SCANCODE_F5); ui.update(true, SDL_GetTicks()); ui.sample_gameplay(true);
        require(ui.take_action() == Action::None, "Menu key activated a gameplay hotkey");
        release(SDL_SCANCODE_F5);
        ui.page = Page::Controls; ui.selected = 2; key(ui, SDL_SCANCODE_RETURN);
        require(ui.page == Page::Hotkeys, "Input Hotkeys submenu missing");
        ui.update(true, SDL_GetTicks()); ui.sample_gameplay(true);
        key(ui, SDL_SCANCODE_RETURN); ui.update(true, SDL_GetTicks()); ui.sample_gameplay(true);
        key(ui, SDL_SCANCODE_F6);
        require(ui.hotkeys.rows()[0].key == SDL_SCANCODE_F6 && ui.hotkeys.rows()[0].extra_keys == std::vector<int>{SDL_SCANCODE_F5}, "Hotkey capture did not accept a keyboard key");
        key(ui, SDL_SCANCODE_DELETE);
        require(!ui.hotkeys.rows()[0].key && ui.hotkeys.rows()[0].extra_keys.empty(), "Hotkey unbind did not clear keyboard inputs");
        key(ui, SDL_SCANCODE_ESCAPE);
        require(ui.page == Page::Controls && ui.selected == 2, "Hotkeys Back lost its parent selection");
    }
    {
        Frontend ui;
        require(ui.take_core_settings_changed(), "Initial core settings were not pending");
        require(!ui.take_core_settings_changed(), "Unchanged settings remained pending");
        ui.page = Page::Video; ui.selected = 2; ui.resolution_values = {"1", "2"};
        key(ui, SDL_SCANCODE_RIGHT);
        require(ui.resolution_index == 1 && ui.take_core_settings_changed(), "Resolution edit did not mark core settings");
        key(ui, SDL_SCANCODE_RIGHT);
        require(!ui.take_core_settings_changed(), "Clamped resolution unnecessarily marked core settings");
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.resolution_index == 0 && ui.take_core_settings_changed(), "Resolution activation did not mark core settings");
        ui.page = Page::Audio; ui.selected = 0;
        key(ui, SDL_SCANCODE_LEFT);
        require(!ui.take_core_settings_changed(), "Volume edit unnecessarily marked core settings");
        ui.page = Page::Advanced; ui.selected = 2;
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.page == Page::CoreOptions && !ui.take_core_settings_changed(), "Opening the core catalog should not change settings");
        ui.core_choices = {{"test_choice", "Test choice", "", {"a", "b"}, {"A", "B"}}};
        ui.cycle_core_choice(0, 1);
        require(ui.take_core_settings_changed(), "Core choice did not mark core settings");
        require(!ui.take_core_settings_changed(), "Consumed core edit remained pending");
    }
    {
        SDL_Window* window = SDL_CreateWindow("Mouse menu test", 1280, 720, SDL_WINDOW_HIDDEN);
        require(window != nullptr, SDL_GetError());
        SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
        require(renderer != nullptr, SDL_GetError());
        {
            TextRenderer font(renderer);
            Frontend ui;
            auto draw = [&] { ui.draw(renderer, font, 1280, 720); };
            ui.open_menu();
            require(ui.needs_redraw(SDL_GetTicks()), "New menu did not request a redraw");
            draw();
            require(!ui.needs_redraw(SDL_GetTicks()), "Unchanged menu requested a redraw");
            ++ui.selected;
            require(ui.needs_redraw(SDL_GetTicks()), "Selection change did not request a redraw");
            draw();
            require(!ui.needs_redraw(SDL_GetTicks()), "Selection redraw did not settle");
            ui.notify("Test notification");
            require(ui.needs_redraw(SDL_GetTicks()), "Notification did not request a redraw");
            draw();
            require(!ui.needs_redraw(SDL_GetTicks()), "Visible notification kept requesting redraws");
            require(ui.needs_redraw(SDL_GetTicks() + 5000), "Expired notification did not request a redraw");
            ui.resume_with_message("");
            auto mouse = [&](Uint32 type, float x, float y, bool moving = true) {
                SDL_Event e{}; e.type = type;
                if (type == SDL_EVENT_MOUSE_MOTION) {
                    e.motion.windowID = SDL_GetWindowID(window);
                    e.motion.x = x; e.motion.y = y; e.motion.xrel = moving ? 1 : 0;
                } else {
                    e.button.windowID = SDL_GetWindowID(window); e.button.button = SDL_BUTTON_LEFT;
                    e.button.x = x; e.button.y = y;
                }
                ui.event(e);
            };
            auto click = [&](float x, float y) {
                mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y);
                mouse(SDL_EVENT_MOUSE_BUTTON_UP, x, y);
            };
            ui.open_menu(); draw();
            mouse(SDL_EVENT_MOUSE_MOTION, 250, 310);
            require(ui.selected == 4, "Mouse hover did not select Configuration");
            key(ui, SDL_SCANCODE_UP);
            mouse(SDL_EVENT_MOUSE_MOTION, 250, 310, false);
            require(ui.selected == 3, "Stationary mouse overrode keyboard selection");
            click(250, 310);
            require(ui.page == Page::Configuration, "Click did not enter Configuration");
            click(250, 310);
            require(ui.page == Page::Configuration, "Stale menu hit boxes activated a different page");
            draw(); click(250, 195);
            require(ui.page == Page::Video, "Mouse did not enter Video");
            draw(); click(250, 160);
            require(ui.fullscreen, "Mouse did not toggle setting");
            mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, 250, 160);
            mouse(SDL_EVENT_MOUSE_BUTTON_UP, 10, 10);
            require(ui.fullscreen, "Releasing outside target activated it");
            click(1050, 695);
            require(ui.page == Page::Configuration, "Bottom Back prompt not clickable");
            draw(); click(250, 160); draw(); click(250, 195); draw();
            require(ui.page == Page::Bindings, "Mouse did not open keyboard bindings");
            SDL_Event wheel{}; wheel.type = SDL_EVENT_MOUSE_WHEEL;
            wheel.wheel.windowID = SDL_GetWindowID(window); wheel.wheel.y = -12;
            ui.event(wheel); draw();
            require(ui.selected == 12, "Wheel did not scroll long binding list");
            wheel.wheel.y = 100; ui.event(wheel); draw();
            require(ui.selected == 0, "Wheel did not clamp at first binding");
            click(250, 160); draw();
            mouse(SDL_EVENT_MOUSE_MOTION, 250, 195);
            ui.event(wheel);
            require(ui.selected == 0, "Mouse changed binding during capture");
            click(1050, 695);
            require(ui.page == Page::Bindings, "Mouse Back did not cancel capture");
            draw(); click(1050, 695);
            require(ui.page == Page::Controls, "Back after capture did not return to Input");
            ui.open_menu(); draw();
            mouse(SDL_EVENT_MOUSE_BUTTON_UP, 250, 160);
            require(ui.paused(), "Release from gameplay activated menu");
            click(250, 160);
            require(!ui.paused(), "Mouse Resume did not resume game");
            click(250, 310);
            require(!ui.paused(), "Gameplay click used stale menu targets");
            ui.open_menu(); draw();
            click(250, 270); draw();
            require(ui.page == Page::Confirm && ui.selected == 0, "Mouse restart skipped confirmation");
            click(250, 195);
            require(ui.take_action() == Action::Reset, "Mouse confirmation did not emit restart");
            require(SDL_SetWindowSize(window, 640, 360), SDL_GetError());
            ui.open_menu(); draw(); // Simulate a drawable twice the window-coordinate size.
            click(125, 80);
            require(!ui.paused(), "Mouse coordinates did not scale to drawable size");
            require(SDL_SetWindowSize(window, 480, 800), SDL_GetError());
            ui.open_menu(); ui.draw(renderer, font, 480, 800);
            click(120, 180);
            require(!ui.paused(), "Portrait menu hit testing failed");
        }
        SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);
    }
    auto first = attach();
    auto* joystick = SDL_OpenJoystick(first);
    require(joystick != nullptr, SDL_GetError());
    {
        Frontend ui;
        ui.discover();
        require(ui.controller_id() == first, "Controller present at startup must open");
        Uint64 now = 0;
        auto tick = [&] {
            SDL_UpdateJoysticks(); SDL_UpdateGamepads();
            ui.update(true, now += 16); ui.sample_gameplay(true);
        };
        auto button = [&](SDL_GamepadButton b, bool down) {
            require(SDL_SetJoystickVirtualButton(joystick, b, down), SDL_GetError()); tick();
        };
        tick();
        // Input can change after menu/hotkey processing (including during a
        // pacing wait). Each core execution must see a fresh gameplay sample.
        ui.update(true, now += 16);
        require(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_EAST, true), SDL_GetError());
        SDL_PumpEvents(); ui.sample_gameplay(true);
        require(ui.gameplay()[RETRO_DEVICE_ID_JOYPAD_A], "Late controller press used the earlier snapshot");
        require(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_EAST, false), SDL_GetError());
        SDL_PumpEvents(); ui.sample_gameplay(true);
        require(!ui.gameplay()[RETRO_DEVICE_ID_JOYPAD_A], "Second gameplay sample retained a released button");
        ui.open_menu();
        require(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_EAST, true), SDL_GetError());
        SDL_PumpEvents(); ui.sample_gameplay(true);
        require(ui.input().buttons == InputSnapshot{}.buttons, "Late gameplay sampling bypassed the menu");
        ui.resume_with_message(""); ui.sample_gameplay(true);
        require(ui.input().buttons == InputSnapshot{}.buttons, "Late gameplay sampling bypassed resume blocking");
        require(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_EAST, false), SDL_GetError());
        tick(); tick();
        button(SDL_GAMEPAD_BUTTON_EAST, true);
        require(ui.gameplay()[RETRO_DEVICE_ID_JOYPAD_A], "East must map to game A");
        button(SDL_GAMEPAD_BUTTON_EAST, false);
        button(SDL_GAMEPAD_BUTTON_SOUTH, true);
        require(ui.gameplay()[RETRO_DEVICE_ID_JOYPAD_B], "South must map to game B");
        button(SDL_GAMEPAD_BUTTON_SOUTH, false);
        require(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 24000), SDL_GetError()); tick();
        require(ui.input().axes[0] > 0, "Stick must map to analog input");
        require(!ui.gameplay()[RETRO_DEVICE_ID_JOYPAD_RIGHT], "Generic stick must not map to D-pad");
        require(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 1000), SDL_GetError()); tick();
        require(ui.input().axes[0] == 0, "Stick deadzone must suppress drift");
        button(SDL_GAMEPAD_BUTTON_GUIDE, true);
        require(!ui.paused(), "Guide must not open the menu");
        button(SDL_GAMEPAD_BUTTON_GUIDE, false);
        for (auto b : {SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_BACK}) {
            auto id = b == SDL_GAMEPAD_BUTTON_START ? RETRO_DEVICE_ID_JOYPAD_START : RETRO_DEVICE_ID_JOYPAD_SELECT;
            button(b, true);
            require(ui.gameplay()[id], "Start/Select must respond immediately on press");
            for (int i = 0; i < 120; ++i) {
                tick();
                require(ui.gameplay()[id] && !ui.paused(), "Individual Start/Select must remain held without opening menu");
            }
            button(b, false);
            require(!ui.gameplay()[id], "Start/Select must release immediately");
        }
        button(SDL_GAMEPAD_BUTTON_START, true);
        require(ui.gameplay()[RETRO_DEVICE_ID_JOYPAD_START], "Start must reach game before the chord forms");
        button(SDL_GAMEPAD_BUTTON_BACK, true);
        require(ui.paused(), "Controller chord must pause");
        for (auto value : ui.gameplay()) require(value == 0, "Menu input leaked into gameplay");
        button(SDL_GAMEPAD_BUTTON_START, false);
        button(SDL_GAMEPAD_BUTTON_BACK, false);
        tick();
        button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
        require(ui.selected == 1, "D-pad must navigate menu");
        button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
        button(SDL_GAMEPAD_BUTTON_EAST, true);
        require(!ui.paused(), "East must resume from main menu");
        tick();
        require(!ui.gameplay()[RETRO_DEVICE_ID_JOYPAD_A], "Held resume button leaked to game");
        button(SDL_GAMEPAD_BUTTON_EAST, false);
        button(SDL_GAMEPAD_BUTTON_EAST, true);
        require(ui.gameplay()[RETRO_DEVICE_ID_JOYPAD_A], "Fresh gameplay press must work after resume");
        button(SDL_GAMEPAD_BUTTON_EAST, false);

        ui.open_menu();
        ui.resume_with_message("State saved");
        require(!ui.paused(), "State success must close the menu");
        tick();
        ui.stick_shortcut = true;
        button(SDL_GAMEPAD_BUTTON_START, true);
        button(SDL_GAMEPAD_BUTTON_BACK, true);
        require(!ui.paused() && ui.gameplay()[RETRO_DEVICE_ID_JOYPAD_START] && ui.gameplay()[RETRO_DEVICE_ID_JOYPAD_SELECT],
                "Alternative shortcut must allow holding both Start and Select");
        button(SDL_GAMEPAD_BUTTON_START, false);
        button(SDL_GAMEPAD_BUTTON_BACK, false);
        button(SDL_GAMEPAD_BUTTON_LEFT_STICK, true);
        require(!ui.paused(), "L3 alone must not open menu");
        button(SDL_GAMEPAD_BUTTON_RIGHT_STICK, true);
        require(ui.paused(), "L3+R3 must still open menu");
        button(SDL_GAMEPAD_BUTTON_LEFT_STICK, false);
        button(SDL_GAMEPAD_BUTTON_RIGHT_STICK, false);
        tick();
        key(ui, SDL_SCANCODE_ESCAPE); tick();
        ui.stick_shortcut = false;
        ui.home_shortcut = true;
        tick();
        button(SDL_GAMEPAD_BUTTON_GUIDE, true);
        require(ui.paused(), "Home shortcut did not open the menu");
        tick(); require(ui.page == Page::Main, "Held Home retriggered shortcut");
        button(SDL_GAMEPAD_BUTTON_GUIDE, false);
        key(ui, SDL_SCANCODE_ESCAPE); tick();
        ui.home_shortcut = false;

        key(ui, SDL_SCANCODE_ESCAPE);
        require(ui.paused(), "Escape must open menu");
        ui.can_save = true;
        key(ui, SDL_SCANCODE_DOWN); key(ui, SDL_SCANCODE_RETURN);
        require(ui.take_action() == Action::Save, "First save should not require overwrite confirmation");
        ui.has_save = true;
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.page == Page::Confirm && ui.selected == 0, "Overwrite must default to Cancel");
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.take_action() == Action::None, "Cancel must not overwrite");
        key(ui, SDL_SCANCODE_RETURN); key(ui, SDL_SCANCODE_DOWN); key(ui, SDL_SCANCODE_RETURN);
        require(ui.take_action() == Action::Save, "Confirmed overwrite should save");
        key(ui, SDL_SCANCODE_DOWN); key(ui, SDL_SCANCODE_RETURN);
        require(ui.page == Page::Confirm, "Loading must require confirmation");
        key(ui, SDL_SCANCODE_ESCAPE);
        require(ui.take_action() == Action::None, "Back must cancel loading");
        key(ui, SDL_SCANCODE_DOWN); key(ui, SDL_SCANCODE_RETURN);
        require(ui.page == Page::Confirm, "Reset must require confirmation");
        key(ui, SDL_SCANCODE_DOWN); key(ui, SDL_SCANCODE_RETURN);
        require(ui.take_action() == Action::Reset, "Confirmed reset must emit reset");

        ui.open_menu(); ui.selected = 4;
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.page == Page::Configuration, "Configuration submenu missing");
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.page == Page::Controls, "Configuration must open Controls");
        key(ui, SDL_SCANCODE_ESCAPE);
        require(ui.page == Page::Configuration && ui.selected == 0, "Controls Back lost its parent selection");
        key(ui, SDL_SCANCODE_DOWN); key(ui, SDL_SCANCODE_RETURN);
        require(ui.page == Page::Video, "Configuration must open Video");
        key(ui, SDL_SCANCODE_ESCAPE);
        require(ui.page == Page::Configuration && ui.selected == 1, "Settings Back lost its parent selection");
        key(ui, SDL_SCANCODE_ESCAPE);
        require(ui.page == Page::Main && ui.selected == 4, "Configuration Back must return to its main row");
        key(ui, SDL_SCANCODE_RETURN);
        key(ui, SDL_SCANCODE_DOWN); key(ui, SDL_SCANCODE_DOWN); key(ui, SDL_SCANCODE_RETURN);
        require(ui.page == Page::Audio, "Configuration must open Audio");
        key(ui, SDL_SCANCODE_ESCAPE);
        require(ui.page == Page::Configuration && ui.selected == 2, "Audio Back lost its parent selection");
        key(ui, SDL_SCANCODE_DOWN); key(ui, SDL_SCANCODE_RETURN);
        require(ui.page == Page::Advanced, "Configuration must open Advanced");
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.vrr, "Advanced VRR toggle ignored");
        key(ui, SDL_SCANCODE_DOWN); key(ui, SDL_SCANCODE_LEFT);
        require(!ui.vsync, "Advanced VSync toggle ignored");
        key(ui, SDL_SCANCODE_DOWN); key(ui, SDL_SCANCODE_RETURN);
        require(ui.page == Page::CoreOptions, "Dolphin Advanced should open More Core options immediately after VSync");
        key(ui, SDL_SCANCODE_ESCAPE);
        require(ui.page == Page::Advanced && ui.selected == 2, "Core catalog Back lost its Advanced selection");
        key(ui, SDL_SCANCODE_ESCAPE);
        require(ui.page == Page::Configuration && ui.selected == 3, "Advanced Back lost its parent selection");
        ui.page = Page::Configuration; ui.selected = 4;
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.take_action() == Action::CreateGameConfig, "Create game config action missing");
        ui.game_config = true;
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.page == Page::Confirm, "Reset inherited settings must require confirmation");
        key(ui, SDL_SCANCODE_ESCAPE);
        require(ui.take_action() == Action::None, "Cancelling game reset emitted action");
        key(ui, SDL_SCANCODE_RETURN); key(ui, SDL_SCANCODE_DOWN); key(ui, SDL_SCANCODE_RETURN);
        require(ui.take_action() == Action::ResetGameConfig, "Reset inherited settings action missing");
        ui.game_config = false;
        const bool initial_fullscreen = ui.fullscreen;
        key(ui, SDL_SCANCODE_F11);
        require(ui.fullscreen != initial_fullscreen, "F11 did not toggle fullscreen");
        key(ui, SDL_SCANCODE_F11);
        require(ui.fullscreen == initial_fullscreen, "F11 did not toggle back");
        SDL_Event fullscreen_event{};
        fullscreen_event.type = SDL_EVENT_WINDOW_ENTER_FULLSCREEN; ui.event(fullscreen_event);
        require(ui.fullscreen, "External fullscreen entry ignored");
        fullscreen_event.type = SDL_EVENT_WINDOW_LEAVE_FULLSCREEN; ui.event(fullscreen_event);
        require(!ui.fullscreen, "External fullscreen exit ignored");
        {
            Frontend wii;
            wii.bindings.configure("wii", {});
            wii.page = Page::Controls; wii.selected = 0;
            require(wii.wii_controller == 0, "Wii should default to Remote and Nunchuck");
            for (int expected = 1; expected < 4; ++expected) {
                key(wii, SDL_SCANCODE_RETURN);
                require(wii.wii_controller == expected && wii.page == Page::Controls, "Wii selector did not cycle");
            }
            key(wii, SDL_SCANCODE_RIGHT);
            require(wii.wii_controller == 0, "Wii selector failed to wrap");
            key(wii, SDL_SCANCODE_LEFT);
            require(wii.wii_controller == 3, "Wii selector left ignored");
            wii.selected = 8; key(wii, SDL_SCANCODE_RETURN);
            require(wii.page == Page::Configuration, "Wii Input Back failed");
        }
        ui.page = Page::Video; ui.selected = 0;
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.fullscreen, "Menu fullscreen toggle stopped working after external changes");
        ui.selected = 1;
        require(ui.scaling == 2, "Menu default scaling must be Sharp Bilinear");
        ui.scaling = 0; // Exercise the full selector cycle from nearest.
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.scaling == 1, "Linear scaling missing");
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.scaling == 2, "Sharp Bilinear scaling missing");
        key(ui, SDL_SCANCODE_RIGHT);
        require(ui.scaling == 3, "Integer scaling missing");
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.scaling == 0, "Scaling selector did not wrap");
        key(ui, SDL_SCANCODE_LEFT);
        require(ui.scaling == 3, "Scaling left navigation ignored");
        ui.selected = 2;
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.aspect_mode == aspect_ratio::Wide && ui.page == Page::Video, "Aspect selector missing without resolution option");
        for (int mode = aspect_ratio::Standard; mode < aspect_ratio::Count; ++mode) {
            key(ui, SDL_SCANCODE_RETURN);
            require(ui.aspect_mode == mode, "Aspect selector order incorrect");
        }
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.aspect_mode == aspect_ratio::Auto, "Aspect selector did not wrap to Auto");
        ui.resolution_values = {"1x", "2x", "3x"};
        ui.page = Page::Video; ui.selected = 3;
        key(ui, SDL_SCANCODE_LEFT);
        require(ui.aspect_mode == aspect_ratio::Stretch, "Aspect selection left did not wrap");
        key(ui, SDL_SCANCODE_RIGHT);
        require(ui.aspect_mode == aspect_ratio::Auto, "Aspect selection right ignored");
        ui.page = Page::Video; ui.selected = 2;
        key(ui, SDL_SCANCODE_RIGHT);
        require(ui.resolution_index == 1, "Resolution increase ignored");
        key(ui, SDL_SCANCODE_LEFT);
        require(ui.resolution_index == 0, "Resolution decrease ignored");
        key(ui, SDL_SCANCODE_LEFT);
        require(ui.resolution_index == 0, "Resolution underflow");
        ui.page = Page::Audio; ui.selected = 0;
        for (int i = 0; i < 20; ++i) key(ui, SDL_SCANCODE_LEFT);
        require(ui.volume == 0, "Volume must clamp at zero");
        for (int i = 0; i < 20; ++i) key(ui, SDL_SCANCODE_RIGHT);
        require(ui.volume == 100, "Volume must clamp at 100");
        key(ui, SDL_SCANCODE_ESCAPE); key(ui, SDL_SCANCODE_ESCAPE); tick();

        ui.page = Page::Main; ui.selected = 5;
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.page == Page::Interface, "Interface submenu missing");
        require(ui.pause_on_focus_loss && ui.hide_cursor, "Unexpected interface defaults");
        key(ui, SDL_SCANCODE_RETURN);
        require(!ui.pause_on_focus_loss, "Focus pause toggle ignored");
        key(ui, SDL_SCANCODE_DOWN); key(ui, SDL_SCANCODE_RETURN);
        require(!ui.hide_cursor, "Cursor toggle ignored");
        key(ui, SDL_SCANCODE_DOWN); key(ui, SDL_SCANCODE_DOWN);
        key(ui, SDL_SCANCODE_RETURN);
        require(ui.swap_confirm_cancel, "Interface confirm/cancel toggle ignored");
        key(ui, SDL_SCANCODE_DOWN); key(ui, SDL_SCANCODE_RIGHT);
        require(!ui.confirm_save_states, "Interface state confirmation toggle ignored");
        require(!ui.show_stats, "Stats overlay must default to off");
        key(ui, SDL_SCANCODE_DOWN); key(ui, SDL_SCANCODE_RIGHT);
        require(ui.show_stats, "Interface stats toggle ignored");
        key(ui, SDL_SCANCODE_RETURN);
        require(!ui.show_stats, "Stats overlay could not be disabled");
        key(ui, SDL_SCANCODE_ESCAPE);
        require(ui.page == Page::Main && ui.selected == 5, "Interface Back lost parent selection");
        tick();
        button(SDL_GAMEPAD_BUTTON_SOUTH, true);
        require(!ui.paused(), "South must cancel with swapped navigation");
        button(SDL_GAMEPAD_BUTTON_SOUTH, false);
        button(SDL_GAMEPAD_BUTTON_EAST, true);
        require(ui.gameplay()[RETRO_DEVICE_ID_JOYPAD_A], "Swapped navigation changed the gameplay A button");
        button(SDL_GAMEPAD_BUTTON_EAST, false);
        ui.open_menu();
        tick();
        ui.selected = 5;
        button(SDL_GAMEPAD_BUTTON_EAST, true);
        require(ui.page == Page::Interface, "East must confirm with swapped navigation");
        button(SDL_GAMEPAD_BUTTON_EAST, false);
        key(ui, SDL_SCANCODE_ESCAPE);
        ui.page = Page::Game;
        SDL_Event focus_lost{}; focus_lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        ui.event(focus_lost);
        require(!ui.paused(), "Focus loss paused despite disabled preference");
        ui.update(false, SDL_GetTicks()); ui.sample_gameplay(false);
        require(ui.input().buttons == InputSnapshot{}.buttons, "Released background input was not neutral");
        require(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_EAST, true), SDL_GetError());
        SDL_UpdateJoysticks(); SDL_UpdateGamepads();
        ui.update(false, SDL_GetTicks()); ui.sample_gameplay(false);
        require(ui.gameplay()[RETRO_DEVICE_ID_JOYPAD_A], "Unfocused gamepad input was ignored with focus pause disabled");
        ui.pause_on_focus_loss = true;
        ui.update(false, SDL_GetTicks()); ui.sample_gameplay(false);
        require(ui.input().buttons == InputSnapshot{}.buttons, "Background input leaked with focus pause enabled");
        require(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_EAST, false), SDL_GetError());
        SDL_UpdateJoysticks(); SDL_UpdateGamepads();
        ui.pause_on_focus_loss = true; ui.event(focus_lost);
        require(ui.paused(), "Focus loss did not pause with preference enabled");
        auto second = attach();
        require(SDL_DetachVirtualJoystick(first), SDL_GetError());
        SDL_Event removed{}; removed.type = SDL_EVENT_GAMEPAD_REMOVED; removed.gdevice.which = first;
        ui.event(removed);
        require(ui.controller_id() == second, "Disconnect must switch to available controller");
        require(SDL_DetachVirtualJoystick(second), SDL_GetError());
        removed.gdevice.which = second; ui.event(removed);
        require(ui.controller_id() == 0 && ui.paused(), "Last controller disconnect must pause");

        auto third = attach();
        SDL_Event added{}; added.type = SDL_EVENT_GAMEPAD_ADDED; added.gdevice.which = third;
        ui.event(added);
        require(ui.controller_id() == third, "Hot-plug must open new controller");
        require(ui.paused(), "Reconnection must not auto-resume");
        require(SDL_DetachVirtualJoystick(third), SDL_GetError());
    }
    SDL_CloseJoystick(joystick);
    SDL_Quit();
}
int main() {
    try { tests(); std::cout << "Controller and menu tests passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; SDL_Quit(); return 1; }
}
