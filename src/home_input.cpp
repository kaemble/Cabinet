#include "home_window.h"

bool Home::discover()
{
    if (pad)
    {
        return false;
    }
    int count = 0;
    auto ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count && !pad; ++i)
    {
        pad = SDL_OpenGamepad(ids[i]);
    }
    SDL_free(ids);
    block_controller();
    return pad != nullptr;
}
void Home::start_text(bool title)
{
    typing_title = title;
    search = !title;
    text_edit.begin(input_value());
    text_scroll = 0;
    reset_caret();
    SDL_StartTextInput(window);
}

void Home::reset_caret()
{
    caret_visible = true;
    next_caret = SDL_GetTicks() + 500;
    needs_redraw = true;
}

void Home::stop_text()
{
    SDL_StopTextInput(window);
    search = typing_title = text_dragging = false;
    next_caret = 0;
    if (modal == Modal::Edit && !editing && !api_key.empty() && artwork_title != draft.title)
    {
        import_artwork();
    }
}

void Home::place_caret(const SDL_FRect& rect, float x, bool extend)
{
    const auto value = modal == Modal::ApiKey ? std::string(key_draft.size(), '*') : input_value();
    const float prefix = (typing_title && modal == Modal::Edit) ? text.width("Title: ", layout.body) : 0;
    const float offset = x - rect.x - layout.gap - prefix + text_scroll;
    std::size_t position = 0;
    float previous = 0;
    while (position < value.size())
    {
        const auto end = TextEdit::next(value, position);
        const float width = text.width(value.substr(0, end), layout.body);
        if (offset < (previous + width) / 2)
        {
            break;
        }
        position = end;
        previous = width;
    }
    text_edit.place(position, extend);
    text_rect = rect;
    reset_caret();
}

void Home::event(SDL_Event event)
{
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
    {
        block_controller();
        text_dragging = false;
    }
    switch (event.type)
    {
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_SHOWN:
    case SDL_EVENT_RENDER_TARGETS_RESET:
    case SDL_EVENT_RENDER_DEVICE_RESET:
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_DROP_FILE:
        needs_redraw = true;
        break;
    default:
        break;
    }
    if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
    {
        request_close();
        return;
    }
    if (event.type == SDL_EVENT_GAMEPAD_ADDED && discover())
    {
        notify("Controller connected");
    }
    if (event.type == SDL_EVENT_GAMEPAD_REMOVED && pad && event.gdevice.which == SDL_GetGamepadID(pad))
    {
        SDL_CloseGamepad(pad);
        pad = nullptr;
        notify(discover() ? "Controller disconnected. Using the next controller."
                          : "Controller disconnected. Keyboard and mouse are ready.");
    }
    if (event.type == SDL_EVENT_DROP_FILE)
    {
        if (modal == Modal::None)
        {
            import_file(event.drop.data);
        }
        else
        {
            notify("Finish or cancel the current dialog before importing another game.");
        }
    }
    if (event.type == SDL_EVENT_TEXT_INPUT && (search || typing_title))
    {
        auto& target = input_value();
        const auto previous = target;
        text_edit.insert(target, event.text.text);
        reset_caret();
        if (target != previous)
        {
            if (search)
            {
                refilter();
            }
            else if (modal == Modal::Edit)
            {
                dirty = true;
                if (!editing)
                {
                    ++artwork_generation;
                    queued_artwork.clear();
                }
            }
        }
    }
    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        auto key = event.key.scancode;
        if (key == SDL_SCANCODE_ESCAPE)
        {
            if (!event.key.repeat)
            {
                if (modal == Modal::None && !search && !typing_title)
                {
                    open_menu();
                }
                else
                {
                    back();
                }
            }
        }
        else if (key == SDL_SCANCODE_RETURN && !event.key.repeat)
        {
            activate();
        }
        else if (search || typing_title)
        {
            auto& target = input_value();
            const auto previous = target;
            text_edit.key(target, event.key);
            reset_caret();
            if (target != previous)
            {
                if (search)
                {
                    refilter();
                }
                else if (modal == Modal::Edit)
                {
                    dirty = true;
                    if (!editing)
                    {
                        ++artwork_generation;
                        queued_artwork.clear();
                    }
                }
            }
        }
        else
        {
            if (key == SDL_SCANCODE_UP)
            {
                navigate(0, -1);
            }
            if (key == SDL_SCANCODE_DOWN)
            {
                navigate(0, 1);
            }
            if (key == SDL_SCANCODE_LEFT)
            {
                navigate(-1, 0);
            }
            if (key == SDL_SCANCODE_RIGHT)
            {
                navigate(1, 0);
            }
            if (key == SDL_SCANCODE_TAB && modal == Modal::None)
            {
                focus = (focus + 1) % 3;
            }
            if (key == SDL_SCANCODE_I && modal == Modal::None)
            {
                start_browse(BrowsePurpose::Import);
            }
            if (key == SDL_SCANCODE_F && modal == Modal::None)
            {
                start_text(false);
            }
        }
    }
    if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        update_layout();
        hits.clear();
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_WHEEL)
    {
        int direction = (event.wheel.y > 0) ? -1 : (event.wheel.y < 0) ? 1 : 0;
        if (modal == Modal::Browse)
        {
            navigate(0, direction * 3);
        }
        else if (modal == Modal::None)
        {
            float mx, my;
            SDL_GetMouseState(&mx, &my);
            if (mx < layout.side)
            {
                focus = 0;
                sidebar_scroll += direction * (layout.system_height + 4) * 3;
            }
            else if (!visible.empty())
            {
                focus = 1;
                navigate(0, direction);
            }
        }
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT)
    {
        text_dragging = false;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION && text_dragging && (search || typing_title))
    {
        SDL_ConvertEventToRenderCoordinates(renderer, &event);
        place_caret(text_rect, event.motion.x, true);
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
    {
        SDL_ConvertEventToRenderCoordinates(renderer, &event);
        const SDL_FPoint point{event.button.x, event.button.y};
        for (auto it = hits.rbegin(); it != hits.rend(); ++it)
        {
            if (SDL_PointInRectFloat(&point, &it->rect))
            {
                const int kind = it->kind, index = it->index;
                if (kind == 0)
                {
                    stop_text();
                    sidebar = index;
                    focus = 0;
                    refilter();
                }
                if (kind == 1)
                {
                    stop_text();
                    selected = index;
                    focus = 1;
                    activate();
                }
                if (kind == 2)
                {
                    toolbar = index;
                    focus = 2;
                    if (index == 1)
                    {
                        if (!search)
                        {
                            start_text(false);
                        }
                        place_caret(it->rect, point.x, SDL_GetModState() & SDL_KMOD_SHIFT);
                        text_dragging = true;
                    }
                    else
                    {
                        stop_text();
                        activate();
                    }
                }
                if (kind == 3)
                {
                    choice = index;
                    if ((modal == Modal::Edit || modal == Modal::ApiKey || modal == Modal::SteamGrid) && index == 0)
                    {
                        if (!typing_title)
                        {
                            start_text(true);
                        }
                        place_caret(it->rect, point.x, SDL_GetModState() & SDL_KMOD_SHIFT);
                        text_dragging = true;
                    }
                    else
                    {
                        stop_text();
                        activate();
                    }
                }
                if (kind == 4)
                {
                    browser.select(index);
                    activate();
                }
                if (kind == 5)
                {
                    back();
                }
                if (kind == 6)
                {
                    activate();
                }
                if (kind == 9)
                {
                    clear_search();
                }
                if (kind == 10)
                {
                    open_menu();
                }
                if (kind == 7)
                {
                    focus = 0;
                    const float view = layout.height - layout.bar_height;
                    const float total = layout.systems_top + system_count() * (layout.system_height + 4) + layout.gap;
                    const float track = view - 8, thumb = track * view / total;
                    sidebar_scroll =
                        std::clamp((point.y - 4 - thumb / 2) / (track - thumb), 0.0f, 1.0f) * (total - view);
                }
                if (kind == 8)
                {
                    focus = 1;
                    selected = index;
                }
                break;
            }
        }
    }
}

void Home::controller_input(bool focused)
{
    if (!focused)
    {
        block_controller();
        return;
    }
    auto b = [&](SDL_GamepadButton button)
    {
        return pad && SDL_GetGamepadButton(pad, button);
    };
    int x = pad ? SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) : 0;
    int y = pad ? SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) : 0;
    std::array<bool, 8> input{b(SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -16000,
                              b(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > 16000,
                              b(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -16000,
                              b(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > 16000,
                              b(SDL_GAMEPAD_BUTTON_SOUTH),
                              b(SDL_GAMEPAD_BUTTON_EAST),
                              b(SDL_GAMEPAD_BUTTON_NORTH),
                              b(SDL_GAMEPAD_BUTTON_START)};
    // Focus can return before a backend reports a fresh axis value. Never
    // rearm auto-repeat from that cached tilt: require neutral menu input first.
    if (controller_blocked)
    {
        previous = input;
        if (std::none_of(input.begin(), input.end(),
                         [](bool down)
                         {
                             return down;
                         }))
        {
            controller_blocked = false;
        }
        return;
    }
    int direction = input[0] ? 1 : input[1] ? 2 : input[2] ? 3 : input[3] ? 4 : 0;
    if (direction && ((!previous[direction - 1] && direction != repeat_direction) ||
                      (direction == repeat_direction && SDL_GetTicks() >= repeat_at)))
    {
        needs_redraw = true;
        navigate((direction == 3) ? -1 : (direction == 4) ? 1 : 0, (direction == 1) ? -1 : (direction == 2) ? 1 : 0);
        repeat_at = SDL_GetTicks() + ((direction != repeat_direction) ? 350 : 120);
    }
    repeat_direction = direction;
    const int confirm = swap_confirm_cancel ? 5 : 4;
    const int cancel = swap_confirm_cancel ? 4 : 5;
    if (input[7] && !previous[7])
    {
        open_menu();
    }
    else if (input[confirm] && !previous[confirm])
    {
        needs_redraw = true;
        activate(true);
    }
    else if (input[cancel] && !previous[cancel])
    {
        needs_redraw = true;
        back();
    }
    else if (input[6] && !previous[6])
    {
        clear_search();
    }
    previous = input;
}
