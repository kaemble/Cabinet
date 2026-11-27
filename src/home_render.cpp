#include "home_window.h"
#include <cmath>

namespace
{
std::string format_playtime(std::int64_t seconds)
{
    if (seconds == 0)
    {
        return "0 minutes";
    }
    if (seconds < 60)
    {
        return "Less than a minute";
    }
    const auto hours = seconds / 3600;
    const auto minutes = (seconds / 60) % 60;
    std::string result;
    if (hours)
    {
        result = std::to_string(hours) + ((hours == 1) ? " hour" : " hours");
    }
    if (minutes)
    {
        if (!result.empty())
        {
            result += ' ';
        }
        result += std::to_string(minutes) + ((minutes == 1) ? " minute" : " minutes");
    }
    return result;
}
}

void Home::draw_text_input(const SDL_FRect& rect)
{
    text_rect = rect;
    const auto value = modal == Modal::ApiKey ? std::string(key_draft.size(), '*') : input_value();
    const auto prefix = (typing_title && modal == Modal::Edit) ? std::string("Title: ") : std::string{};
    const int size = layout.body;
    const float y = rect.y + (rect.h - size) / 2 - size * .15f;
    const float prefix_width = text.width(prefix, size);
    const float left = rect.x + layout.gap + prefix_width;
    const float width = std::max(1.0f, rect.w - layout.gap * 2 - prefix_width);
    const float caret = text.width(value.substr(0, text_edit.position()), size);
    const float total = text.width(value, size);
    const auto [first, last] = text_edit.selection();
    const float selection_start = text.width(value.substr(0, first), size);
    const float selection_end = text.width(value.substr(0, last), size);
    const float visible_width = std::max(1.0f, width - 2);
    text_scroll = std::clamp(text_scroll, std::max(0.0f, caret - visible_width), caret);
    if (first != last && selection_end - selection_start <= visible_width)
    {
        text_scroll = std::clamp(text_scroll, std::max(0.0f, selection_end - visible_width), selection_start);
    }
    text_scroll = std::min(text_scroll, std::max(0.0f, total - visible_width));
    label(rect.x + layout.gap, y, prefix, size, ink, rect.w - layout.gap * 2);
    const bool clipped = SDL_RenderClipEnabled(renderer);
    SDL_Rect previous{};
    SDL_GetRenderClipRect(renderer, &previous);
    SDL_Rect clip{static_cast<int>(std::ceil(left)), static_cast<int>(rect.y),
                  static_cast<int>(width), static_cast<int>(rect.h)};
    if (clipped)
    {
        SDL_GetRectIntersection(&clip, &previous, &clip);
    }
    SDL_SetRenderClipRect(renderer, &clip);
    if (first != last)
    {
        box({left + selection_start - text_scroll, y - 1, selection_end - selection_start,
             static_cast<float>(size + 4)}, accent);
    }
    label(left - text_scroll, y, value, size, ink, 100000);
    if (caret_visible)
    {
        box({left + caret - text_scroll, y, 1.5f, static_cast<float>(size + 2)}, ink);
    }
    SDL_SetRenderClipRect(renderer, clipped ? &previous : nullptr);
    const SDL_Rect input_area{static_cast<int>(left), static_cast<int>(rect.y),
                              static_cast<int>(width), static_cast<int>(rect.h)};
    SDL_SetTextInputArea(window, &input_area, static_cast<int>(caret - text_scroll));
}

void Home::box(SDL_FRect rect, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
}
void Home::button(SDL_FRect rect, const std::string& title, bool active, int kind, int index, const std::string& icon)
{
    if (active)
    {
        if (kind == 0 && (focus != 0 || modal != Modal::None))
        {
            box(rect, {40, 49, 71, 255});
        }
        else
        {
            box(rect, accent);
            box({rect.x + 3, rect.y + 3, rect.w - 6, rect.h - 6}, {40, 49, 71, 255});
        }
    }
    else
    {
        box(rect, {38, 38, 38, 255});
    }
    const SDL_Color color = active ? ink : muted;
    const int size = layout.body;
    const float icon_size = size * 1.3f;
    const float inset = icon.empty() ? 0 : icon_size + layout.gap;
    if (!icon.empty())
    {
        text.icon(rect.x + layout.gap, rect.y + (rect.h - icon_size) / 2, icon_size, icon);
    }
    if ((kind == 2 && index == 1 && search) || (kind == 3 && index == 0 && typing_title))
    {
        draw_text_input(rect);
    }
    else
    {
        label(rect.x + layout.gap + inset, rect.y + (rect.h - size) / 2 - size * .15f, title, size, color,
              rect.w - inset - ((kind == 0 && index >= 3) ? layout.gap * 3 : layout.gap * 2));
    }
    hits.push_back({rect, kind, index});
}

void Home::Layout::resize(int window_width, int window_height)
{
    width = static_cast<float>(window_width);
    height = static_cast<float>(window_height);
    const float scale = std::min(window_width / 1280.0f, window_height / 720.0f);
    body = std::max(18, static_cast<int>(18 * scale));
    small = std::max(16, static_cast<int>(21 * scale));
    heading = std::max(24, static_cast<int>(32 * scale));
    gap = std::max(12.0f, 12 * scale);
    bar_height = (window_width < 760) ? small * 2.0f + 34 : std::max(44.0f, 44 * scale);
    side = window_width * .20f;
    left = side + gap;
    content = width - left - gap;
    header_height = std::max(48.0f, heading * 1.25f);
    toolbar_height = std::max(40.0f, body * 2.4f);
    grid_top = gap * 3 + header_height + toolbar_height;
    row_height = std::max(49.0f, body * 2.8f);
    system_height = body * 1.9f;
    systems_top = gap * 4 + body * 2 + 3 * (system_height + 4);
    columns =
        std::max(1, static_cast<int>((content + gap) / (std::max(240.0f, 190 * scale) + gap)));
    const float available = height - bar_height - grid_top - gap;
    const float caption_height = gap + body + std::max(12, body * 2 / 3) + 4;
    const float cell_width = (content - (columns - 1) * gap) / columns;
    card_width = std::min(cell_width, std::max(1.0f, available - caption_height));
    card_height = card_width + caption_height;
    grid_rows = std::max(1, static_cast<int>((available + gap) / (card_height + gap)));
    list_rows =
        std::max(1, static_cast<int>((available - body + gap) / (row_height + gap)));
}

void Home::update_layout()
{
    int width = 0, height = 0;
    if (!SDL_GetWindowSize(window, &width, &height) || width <= 0 || height <= 0)
    {
        return;
    }
    if (width != layout.width || height != layout.height)
    {
        measured_playtime_width = 0;
    }
    layout.resize(width, height);
    // Layout uses window points; SDL maps these to all available output pixels.
    SDL_SetRenderLogicalPresentation(renderer, width, height, SDL_LOGICAL_PRESENTATION_STRETCH);
    const float view_height = layout.height - layout.bar_height;
    const float total_height = layout.systems_top + system_count() * (layout.system_height + 4) + layout.gap;
    if (sidebar_tracked != sidebar || sidebar_view_height != view_height)
    {
        const float selected_y = (sidebar < 3) ? layout.gap * 2 + layout.body + sidebar * (layout.system_height + 4)
                                               : layout.systems_top + (sidebar - 3) * (layout.system_height + 4);
        if (selected_y < sidebar_scroll + layout.gap)
        {
            sidebar_scroll = selected_y - layout.gap;
        }
        if (selected_y + layout.system_height > sidebar_scroll + view_height - layout.gap)
        {
            sidebar_scroll = selected_y + layout.system_height - view_height + layout.gap;
        }
        if (sidebar == 0)
        {
            sidebar_scroll = 0;
        }
        sidebar_tracked = sidebar;
        sidebar_view_height = view_height;
    }
    sidebar_scroll = std::clamp(sidebar_scroll, 0.0f, std::max(0.0f, total_height - view_height));
}

void Home::draw()
{
    update_layout();
    const auto& l = layout;
    hits.clear();
    SDL_SetRenderDrawColor(renderer, 24, 24, 24, 255);
    SDL_RenderClear(renderer);
    const float bottom = l.height - l.bar_height, right = l.width - l.gap;
    box({0, 0, l.side, bottom}, {32, 32, 32, 255});
    const float side_width = std::max(1.0f, l.side - l.gap * 2);
    const SDL_Rect sidebar_clip{0, 0, static_cast<int>(l.side), static_cast<int>(bottom)};
    SDL_SetRenderClipRect(renderer, &sidebar_clip);
    label(l.gap * 2, l.gap - sidebar_scroll, "LIBRARY", std::max(11, l.body * 2 / 3), muted, side_width);
    const std::array<std::string, 3> filters{"All games", "Favorites", "Recently played"};
    for (int i = 0; i < 3; ++i)
    {
        button({l.gap, l.gap * 2 + l.body + i * (l.system_height + 4) - sidebar_scroll, side_width, l.system_height},
               filters[i], sidebar == i, 0, i);
    }
    label(l.gap * 2, l.systems_top - l.body - l.gap - sidebar_scroll, "SYSTEMS", std::max(11, l.body * 2 / 3), muted,
          side_width);
    for (int i = 0; i < system_count(); ++i)
    {
        const auto& system = systems()[i];
        const auto found = system_counts.find(system.id);
        const int count = found == system_counts.end() ? 0 : found->second;
        std::string name = (l.side < 200) ? system.short_name : system.name;
        if (name.size() > 24)
        {
            name = system.short_name == "GB / GBC" ? "Game Boy / Color"
                   : system.short_name == "NES"    ? "Nintendo/Famicom"
                   : system.short_name == "MD"     ? "Genesis/Mega Drive"
                                                   : "TurboGrafx/PC Engine";
        }
        const float y = l.systems_top + i * (l.system_height + 4) - sidebar_scroll;
        button({l.gap, y, side_width, l.system_height}, name, sidebar == i + 3, 0, i + 3);
        label(l.gap + side_width - l.gap * 1.6f, y + (l.system_height - l.body) / 2, std::to_string(count),
              std::max(11, l.body * 2 / 3), muted, l.gap * 1.5f);
    }
    SDL_SetRenderClipRect(renderer, nullptr);
    // Hit targets obey the same clipping as the scrolling pane.
    for (auto& hit : hits)
    {
        const float end = std::clamp(hit.rect.y + hit.rect.h, 0.0f, bottom);
        hit.rect.y = std::clamp(hit.rect.y, 0.0f, bottom);
        hit.rect.h = std::max(0.0f, end - hit.rect.y);
    }
    const float sidebar_total = l.systems_top + system_count() * (l.system_height + 4) + l.gap;
    if (sidebar_total > bottom)
    {
        const float track_height = bottom - 8;
        const float thumb = track_height * bottom / sidebar_total;
        const float x = l.side - l.gap / 2 - 2.5f;
        box({x, 4 + (track_height - thumb) * sidebar_scroll / (sidebar_total - bottom), 5, thumb}, {96, 96, 96, 255});
        hits.push_back({{l.side - l.gap, 0, l.gap, bottom}, 7, 0});
    }
    auto scrollbar = [&](float x, float y, float height, int total, int shown, int first, int kind, int stride)
    {
        if (total <= shown)
        {
            return;
        }
        const float thumb = std::max(16.0f, height * shown / total);
        box({x, y + (height - thumb) * first / (total - shown), 5, thumb}, {96, 96, 96, 255});
        for (int i = 0; i < total; ++i)
        {
            hits.push_back({{x - 3, y + height * i / total, 11, height / total}, kind, i * stride});
        }
    };
    const std::string title = (sidebar >= 3) ? systems()[sidebar - 3].name : filters[sidebar];
    const float add_width = text.width("+ Add game", l.body) + l.gap * 2;
    label(l.left + l.gap, l.gap, title, l.heading, ink,
          (sidebar >= 3) ? l.content - add_width - l.gap * 2 : l.content - l.gap);
    if (sidebar >= 3)
    {
        button({right - add_width, l.gap, add_width, l.header_height}, "+ Add game", focus == 2 && toolbar == 0, 2, 0);
    }
    const float toolbar_y = l.gap * 2 + l.header_height;
    const float view_size = l.toolbar_height;
    button({l.left, toolbar_y, l.content - view_size - l.gap, l.toolbar_height},
           query.empty() ? "Search..." : query,
           search || (focus == 2 && toolbar == 1), 2, 1);
    {
        const int view = list_view ? 0 : 1;
        const float x = right - view_size;
        button({x, toolbar_y, view_size, view_size}, "", focus == 2 && toolbar == 2, 2, 2);
        const float unit = view_size / 7;
        const SDL_Color color = (focus == 2 && toolbar == 2) ? ink : muted;
        if (view == 0)
        {
            for (int row = 0; row < 2; ++row)
            {
                for (int col = 0; col < 2; ++col)
                {
                    box({x + unit * (1.5f + col * 2.2f), toolbar_y + unit * (1.5f + row * 2.2f), unit * 1.8f,
                         unit * 1.8f},
                        color);
                }
            }
        }
        else
        {
            for (int row = 0; row < 3; ++row)
            {
                box({x + unit * 1.3f, toolbar_y + unit * (1.4f + row * 1.5f), unit, unit}, color);
                box({x + unit * 2.8f, toolbar_y + unit * (1.6f + row * 1.5f), unit * 2.8f, unit * .6f}, color);
            }
        }
    }
    if (visible.empty())
    {
        const float y = l.grid_top + l.gap;
        label(l.left + l.gap, y, all.empty() ? "Your collection starts here" : "No games in this view", l.body, ink,
              l.content - l.gap * 2);
        label(l.left + l.gap, y + l.body * 2,
              (sidebar < 3 && all.empty()) ? "Select a system to add your first game."
              : all.empty()                ? "Add a game file to build your library."
                                           : "Try another system, filter, or search.",
              l.body, muted, l.content - l.gap * 2);
    }
    else
    {
        const int cols = list_view ? 1 : l.columns, rows = list_view ? l.list_rows : l.grid_rows;
        const float cell_width = (l.content - (cols - 1) * l.gap) / cols;
        const float card_width = l.card_width;
        int row = selected / cols;
        if (row < first_row)
        {
            first_row = row;
        }
        if (row >= first_row + rows)
        {
            first_row = row - rows + 1;
        }
        first_row = std::clamp(first_row, 0, std::max(0, (static_cast<int>(visible.size()) + cols - 1) / cols - rows));
        const int detail = std::max(12, l.body * 2 / 3);
        float playtime_width = 104;
        if (list_view)
        {
            if (!measured_playtime_width)
            {
                measured_playtime_width = playtime_width;
                for (const auto& g : visible)
                {
                    measured_playtime_width = std::max(measured_playtime_width, text.width(format_playtime(g.playtime), detail));
                }
            }
            playtime_width = std::min(measured_playtime_width, l.content * .45f);
            label(right - playtime_width - l.gap, l.grid_top, "Playtime", detail, muted, playtime_width);
        }
        // Navigation counts complete rows, but drawing also includes the next
        // row wherever it intersects the pane, as a preview of more games.
        const SDL_Rect library_clip{static_cast<int>(l.left), static_cast<int>(l.grid_top), static_cast<int>(l.content),
                                    static_cast<int>(bottom - l.grid_top)};
        SDL_SetRenderClipRect(renderer, &library_clip);
        const std::size_t first_game_hit = hits.size();
        const int drawn_rows = rows + (list_view ? 0 : 1);
        for (int i = first_row * cols; i < static_cast<int>(visible.size()) && i < (first_row + drawn_rows) * cols; ++i)
        {
            if (!list_view && l.grid_top + (i / cols - first_row) * (l.card_height + l.gap) >= bottom)
            {
                break;
            }
            const auto& g = visible[i];
            int sys = system_index(g.system);
            const bool missing = !fs::is_regular_file(g.path), active = i == selected && focus == 1;
            const std::string status = missing     ? "File missing"
                                       : (sys < 0) ? "Unknown system"
                                                   : systems()[sys].short_name;
            if (list_view)
            {
                SDL_FRect rect{l.left, l.grid_top + l.body + (i - first_row) * (l.row_height + l.gap), l.content,
                               l.row_height};
                button(rect, "", active, 1, i);
                label(rect.x + l.gap, rect.y + 4, g.title, l.body, ink, rect.w - l.gap * 2);
                label(rect.x + l.gap, rect.y + l.body + 7, status, detail, muted,
                      rect.w - playtime_width - l.gap * 4 - detail);
                if (g.favorite)
                {
                    text.icon(right - playtime_width - l.gap * 2 - detail, rect.y + l.body + 7, detail, "star");
                }
                label(right - playtime_width - l.gap, rect.y + l.body + 7, format_playtime(g.playtime), detail, muted,
                      playtime_width);
            }
            else
            {
                const float x = l.left + (i % cols) * (cell_width + l.gap);
                const float y = l.grid_top + (i / cols - first_row) * (l.card_height + l.gap);
                SDL_FRect rect{x, y, card_width, l.card_height};
                if (active)
                {
                    button(rect, "", true, 1, i);
                }
                else
                {
                    hits.push_back({rect, 1, i});
                }
                const float art = card_width - l.gap * 2;
                const SDL_FRect image{x + l.gap, y + l.gap, art, art};
                if (!boxart.draw(g.save_id, image))
                {
                    text.icon(image.x, image.y, art, "placeholder");
                }
                grid_titles.draw(x + l.gap, y + l.card_height - l.gap - l.body - detail - 4, g.title, l.body, ink,
                                 card_width - l.gap * 2);
                label(x + l.gap, y + l.card_height - l.gap - detail, status, detail, muted,
                      card_width - l.gap * 3 - l.body);
                if (g.favorite)
                {
                    text.icon(x + card_width - l.gap - l.body * 1.3f, y + l.card_height - l.gap - l.body * 1.3f,
                              l.body * 1.3f, "star");
                }
            }
        }
        SDL_SetRenderClipRect(renderer, nullptr);
        for (std::size_t i = first_game_hit; i < hits.size(); ++i)
        {
            hits[i].rect.h = std::max(0.0f, std::min(hits[i].rect.h, bottom - hits[i].rect.y));
        }
        scrollbar(l.width - l.gap / 2 - 2.5f, l.grid_top, bottom - l.grid_top - l.gap,
                  (static_cast<int>(visible.size()) + cols - 1) / cols, rows, first_row, 8, cols);
    }
    if (modal != Modal::None)
    {
        draw_modal();
    }
    box({0, bottom, l.width, l.bar_height}, {16, 16, 16, 255});
    const bool compact = l.width < 760;
    const float margin = std::max(24.0f, l.width * .045f), icon_size = l.small * 1.35f, gap = l.small * .35f;
    const float between = l.small, lift = l.small * .15f;
    struct Prompt
    {
        const char* icon;
        const char* title;
        int kind;
    };
    std::vector<Prompt> prompts;
    if (pad && can_clear_search())
    {
        prompts.push_back({"controller_buttons_north", "Clear", 9});
    }
    if (modal == Modal::None || modal == Modal::Menu)
    {
        prompts.push_back({pad ? "" : "keyboard_escape", pad ? "Start: Menu" : "Menu", 10});
    }
    if (modal == Modal::Browse)
    {
        prompts.push_back({pad ? "controller_dpad_horizontal" : "keyboard_arrows_horizontal",
                           compact ? "Letter" : "Jump by letter", -1});
    }
    if (modal == Modal::Edit && !typing_title && choice == 1 && draft.system != "generic")
    {
        prompts.push_back({pad ? "controller_dpad_horizontal" : "keyboard_arrows_horizontal",
                           compact ? "Change" : "Change system", -1});
    }
    if (typing_title)
    {
        prompts.push_back({"keyboard_arrows_horizontal", compact ? "Move" : "Move cursor", -1});
    }
    prompts.push_back({pad ? (swap_confirm_cancel ? "controller_buttons_south" : "controller_buttons_east")
                          : "keyboard_escape",
                       modal == Modal::Browse ? "Cancel" : "Back", 5});
    prompts.push_back({pad ? (swap_confirm_cancel ? "controller_buttons_east" : "controller_buttons_south")
                          : "keyboard_enter",
                       (search || typing_title) ? "Done" : modal == Modal::Browse ? "Open" : "Select", 6});
    auto prompt_width = [&](const Prompt& prompt)
    {
        return (*prompt.icon ? icon_size + gap : 0) + text.width(prompt.title, l.small);
    };
    float x = l.width - margin + between;
    for (const auto& prompt : prompts)
    {
        x -= prompt_width(prompt) + between;
    }
    const float prompt_y = (compact ? l.height - l.small - 14 : bottom + (l.bar_height - l.small) / 2 - 2) - lift;
    label(margin, bottom + (compact ? 6 : (l.bar_height - l.small) / 2 - 2) - lift,
          process.active() ? (process.ready() ? "Game running" : "Preparing game...")
          : game()         ? game()->title
                           : "",
          l.small, ink, compact ? l.width - margin * 2 : x - margin - between);
    for (const auto& prompt : prompts)
    {
        const float width = prompt_width(prompt);
        if (prompt.kind >= 0)
        {
            hits.push_back({{x, prompt_y - 4, width, icon_size + 8}, prompt.kind, 0});
        }
        const float inset = *prompt.icon ? icon_size + gap : 0;
        if (*prompt.icon)
        {
            text.icon(x, prompt_y + lift - (icon_size - l.small) / 2 + 2, icon_size, prompt.icon);
        }
        label(x + inset, prompt_y, prompt.title, l.small);
        x += width + between;
    }
    if (!message.empty() && SDL_GetTicks() < message_until)
    {
        const float width = l.content - l.gap * 2;
        std::vector<std::string> lines;
        std::string line;
        const char* next = message.c_str();
        while (*next)
        {
            const char* begin = next;
            const auto code = SDL_StepUTF8(&next, nullptr);
            if (code == '\n')
            {
                lines.push_back(line);
                line.clear();
                continue;
            }
            const std::string character(begin, next);
            if (line.empty() && (code == ' ' || code == '\t'))
            {
                continue;
            }
            if (!line.empty() && text.width(line + character, l.body) > width)
            {
                const auto space = line.find_last_of(" \t");
                if (space == std::string::npos)
                {
                    lines.push_back(line);
                    line.clear();
                }
                else
                {
                    lines.push_back(line.substr(0, space));
                    line.erase(0, space + 1);
                }
                if (line.empty() && (code == ' ' || code == '\t'))
                {
                    continue;
                }
            }
            line += character;
        }
        if (!line.empty() || lines.empty())
        {
            lines.push_back(line);
        }
        const float line_height = l.body * 1.35f;
        const float text_height = l.body + (lines.size() - 1) * line_height;
        const float height = std::max(l.toolbar_height, text_height + l.gap * 2);
        const float top = bottom - height - l.gap;
        box({l.left, top, l.content, height}, {48, 48, 48, 255});
        float y = top + (height - text_height) / 2 - l.body * .15f;
        for (const auto& value : lines)
        {
            label(l.left + l.gap, y, value, l.body, ink, width);
            y += line_height;
        }
    }
}

void Home::draw_modal()
{
    hits.clear();
    const auto purpose = browser.purpose();
    const float available_height = layout.height - layout.bar_height - layout.gap * 2;
    const bool editing_details = modal == Modal::Edit && (editing || draft.system == "generic" || !api_key.empty());
    const float editor_height = api_key.empty() ? 640.0f : 720.0f;
    const float scale = std::min(layout.body / 16.0f, available_height / (editing_details ? editor_height : 540.0f));
    const float dw = std::min(800.0f, (layout.width - layout.gap * 2) / scale);
    const float modal_height = (modal == Modal::Menu) ? 380.0f
                               : (modal == Modal::Settings) ? 280.0f
                               : editing_details ? editor_height : 590.0f;
    const float dh = (modal == Modal::Browse) ? available_height / scale
                                             : std::min(modal_height, available_height / scale);
    const float left = (layout.width - dw * scale) / 2;
    const float top = (layout.height - layout.bar_height - dh * scale) / 2;
    const float x = 24, inner = dw - 48;
    auto line = [&](float y, const std::string& value, int size, SDL_Color color = ink, float max_width = 0)
    {
        label(left + x * scale, top + y * scale, value, std::max(14, static_cast<int>(size * scale)), color,
              (max_width > 0 ? max_width : inner) * scale);
    };
    auto button =
        [&](SDL_FRect rect, const std::string& value, bool active, int kind, int index, const std::string& icon = {})
    {
        Home::button({left + rect.x * scale, top + rect.y * scale, rect.w * scale, rect.h * scale}, value, active, kind,
                     index, icon);
    };
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    box({0, 0, layout.width, layout.height}, {0, 0, 0, 180});
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    box({left, top, dw * scale, dh * scale}, {32, 32, 32, 255});
    std::string title = (modal == Modal::Menu) ? "Cabinet menu"
                        : (modal == Modal::Settings) ? "Home settings"
                        : (modal == Modal::ApiKey) ? "SteamGridDB API key"
                        : (modal == Modal::SteamGrid) ? "Find SteamGridDB box art"
                        : (modal == Modal::Bios) ? (bios_system == "psx" ? "PlayStation BIOS"
                                                   : bios_system == "ps2" ? "PlayStation 2 BIOS" : "Sega Saturn BIOS")
                        : (modal == Modal::Browse)
                            ? ((purpose == BrowsePurpose::Archive) ? "Choose a ROM inside the ZIP"
                               : (purpose == BrowsePurpose::Bios)  ? "Choose a BIOS file"
                               : (purpose == BrowsePurpose::BoxArt) ? "Choose box art"
                               : (purpose == BrowsePurpose::Core || purpose == BrowsePurpose::Custom) ? "Choose a core"
                               : (purpose == BrowsePurpose::Content) ? "Choose a ROM"
                                                                   : "Choose a file")
                        : (modal == Modal::Edit)    ? editing ? "Edit game details" : "Add to library"
                        : (modal == Modal::Remove)  ? "Remove this game?"
                        : (modal == Modal::Discard) ? "Discard changes?"
                                                    : "Game details";
    line(26, title, 26);

    if (modal == Modal::Browse)
    {
        const auto& folder = browser.current_folder();
        const auto& entries = browser.items();
        const float rows_top = (purpose == BrowsePurpose::BoxArt) ? 140.0f : 114.0f;
        line(78, folder.string(), 13, muted);
        if (purpose == BrowsePurpose::BoxArt)
        {
            line(104, "PNG, JPEG, or BMP. The image is copied when you save changes.", 13, muted);
        }
        if (browser.loading())
        {
            line(rows_top + 14, (purpose == BrowsePurpose::Archive) ? "Reading ZIP..." : "Reading folder...", 16, muted);
        }
        else if (entries.empty())
        {
            line(rows_top + 14, (purpose == BrowsePurpose::Archive) ? "No compatible ROM files found." : "This folder is empty.",
                 16, muted);
        }
        const int browser_rows = std::max(1, static_cast<int>((dh - rows_top - 24) / 50));
        browser.scroll_into_view(browser_rows);
        for (int i = browser.top(); i < static_cast<int>(entries.size()) && i < browser.top() + browser_rows; ++i)
        {
            const auto& entry = entries[i];
            const bool parent = entry.directory && entry.path == folder.parent_path();
            auto name = (purpose == BrowsePurpose::Archive) ? entry.path.generic_string()
                        : parent                            ? ".. (parent folder)"
                                                            : entry.path.filename().string();
            button({x, rows_top + (i - browser.top()) * 50, inner, 38}, name, i == browser.selected(), 4, i,
                   entry.directory ? (parent ? "folder_open" : "folder") : "");
        }
        return;
    }
    if (modal == Modal::Details && game())
    {
        line(77, game()->title, 20, ink);
        int sys = system_index(game()->system);
        line(110,
             ((sys < 0) ? "Unknown system" : systems()[sys].name) + "  /  " +
                 fs::path(game()->core).filename().string(),
             12, muted);
        line(137, game()->path.empty() ? "No ROM (runs without content)" : game()->path, 12, muted);
        if (!game()->archive_member.empty())
        {
            line(160, "ROM: " + game()->archive_member, 12, muted);
        }
        line(184, "Save folder: " + game()->save_name, 12, muted);
        line(208, "Playtime: " + format_playtime(game()->playtime), 14, ink);
        if (!game()->path.empty() && !fs::is_regular_file(game()->path))
        {
            line(163, "File missing. Choose Locate file below.", 14, muted);
        }
    }
    if (modal == Modal::Bios)
    {
        line(78, bios_installed ? "A compatible BIOS is installed." : "This system requires a BIOS file to run games.",
             16);
        line(107,
             bios_system == "psx" ? "Select an unmodified PlayStation BIOS (512 KiB)."
             : bios_system == "ps2" ? "Select a complete PS2 BIOS dump (4–8 MiB)."
                                  : "Select sega_101.bin (Japan) or mpr-17933.bin (US/Europe).",
             14, muted);
        line(136, "The BIOS is checked and copied to Cabinet's system folder.", 14, muted);
        line(165, "Other game regions may need another BIOS. Add more here later.", 13, muted);
    }
    if (modal == Modal::Edit)
    {
        const float width = inner - 112;
        line(78, draft.path + (draft.archive_member.empty() ? "" : " / " + draft.archive_member), 12, muted, width);
        line(107, editing ? "Changes are applied when you save."
                          : (draft.system == "generic") ? "ROM can be skipped for cores that support running without content."
                                                        : "Choose a system and core, then confirm to save.",
             14, muted, width);
        {
            const SDL_FRect preview{left + (dw - 112) * scale, top + 74 * scale, 88 * scale, 110 * scale};
            if (!boxart.draw(draft.save_id, preview, true))
            {
                text.icon(preview.x, preview.y + 11 * scale, 88 * scale, "placeholder");
            }
        }
    }
    if (modal == Modal::ApiKey)
    {
        line(80, "Get a key at steamgriddb.com/profile/preferences/api", 14, muted);
        line(115, api_key.empty() ? "No key configured." : "A key is saved. Enter a new key to replace it.", 14, muted);
        line(145, "Stored locally with access restricted to your user account.", 14, muted);
    }
    if (modal == Modal::SteamGrid)
    {
        line(80, "Choose a game. Alternate square covers are preferred.", 14, muted);
        line(115, artwork_task.valid() ? "Loading from SteamGridDB..." : "Search by title, then choose a matching game.", 14, muted);
    }
    if (modal == Modal::Remove)
    {
        line(90, "This removes the library entry only.", 18);
        line(125, "Your game file and save states will not be deleted.", 15, muted);
    }
    if (modal == Modal::Discard)
    {
        line(90, "Your changes to these details have not been saved.", 16, muted);
    }
    auto rows = modal_rows();
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        std::string value = rows[i];
        const float rows_top = (modal == Modal::Details) ? 236.0f
                               : (modal == Modal::Menu || modal == Modal::Settings) ? 100.0f : 200.0f;
        button({x, rows_top + static_cast<float>(i) * 50, inner, 38}, value, choice == static_cast<int>(i), 3,
               static_cast<int>(i));
    }
}
