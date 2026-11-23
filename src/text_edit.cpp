#include "text_edit.h"
#include <algorithm>
#include <cctype>

void TextEdit::begin(const std::string& text)
{
    cursor = anchor = text.size();
}

void TextEdit::place(std::size_t position, bool extend)
{
    cursor = position;
    if (!extend)
    {
        anchor = cursor;
    }
}

std::size_t TextEdit::position() const
{
    return cursor;
}

std::pair<std::size_t, std::size_t> TextEdit::selection() const
{
    return std::minmax(cursor, anchor);
}

std::size_t TextEdit::next(std::string_view text, std::size_t position)
{
    if (position < text.size())
    {
        ++position;
        while (position < text.size() && (static_cast<unsigned char>(text[position]) & 0xc0) == 0x80)
        {
            ++position;
        }
    }
    return position;
}

std::size_t TextEdit::previous(std::string_view text, std::size_t position)
{
    if (position > 0)
    {
        --position;
        while (position > 0 && (static_cast<unsigned char>(text[position]) & 0xc0) == 0x80)
        {
            --position;
        }
    }
    return position;
}

std::size_t TextEdit::word(std::string_view text, std::size_t position, int direction)
{
    auto space = [&](std::size_t index)
    {
        return std::isspace(static_cast<unsigned char>(text[index])) != 0;
    };
    if (direction < 0)
    {
        while (position && space(previous(text, position)))
        {
            position = previous(text, position);
        }
        while (position && !space(previous(text, position)))
        {
            position = previous(text, position);
        }
    }
    else
    {
        while (position < text.size() && !space(position))
        {
            position = next(text, position);
        }
        while (position < text.size() && space(position))
        {
            position = next(text, position);
        }
    }
    return position;
}

void TextEdit::erase_selection(std::string& text)
{
    const auto [first, last] = selection();
    text.erase(first, last - first);
    cursor = anchor = first;
}

void TextEdit::insert(std::string& text, std::string_view value)
{
    std::string cleaned;
    for (const unsigned char c : value)
    {
        if (c == '\n' || c == '\r' || c == '\t')
        {
            cleaned += ' ';
        }
        else if (c >= 32 && c != 127)
        {
            cleaned += static_cast<char>(c);
        }
    }
    if (cleaned.empty())
    {
        return;
    }
    const auto [first, last] = selection();
    const auto retained = text.size() - (last - first);
    const auto available = retained < 512 ? 512 - retained : 0;
    std::size_t count = 0;
    while (count < cleaned.size())
    {
        const auto end = next(cleaned, count);
        if (end > available)
        {
            break;
        }
        count = end;
    }
    if (!count)
    {
        return;
    }
    text.replace(first, last - first, cleaned, 0, count);
    cursor = anchor = first + count;
}

void TextEdit::key(std::string& text, const SDL_KeyboardEvent& event)
{
    const bool control = event.mod & SDL_KMOD_CTRL;
    const bool shift = event.mod & SDL_KMOD_SHIFT;
    const auto [first, last] = selection();
    if (control && event.scancode == SDL_SCANCODE_A)
    {
        anchor = 0;
        cursor = text.size();
    }
    else if (control && (event.scancode == SDL_SCANCODE_C || event.scancode == SDL_SCANCODE_X))
    {
        if (first != last && SDL_SetClipboardText(text.substr(first, last - first).c_str()) &&
            event.scancode == SDL_SCANCODE_X)
        {
            erase_selection(text);
        }
    }
    else if (control && event.scancode == SDL_SCANCODE_V)
    {
        if (auto* value = SDL_GetClipboardText())
        {
            insert(text, value);
            SDL_free(value);
        }
    }
    else if (event.scancode == SDL_SCANCODE_HOME || event.scancode == SDL_SCANCODE_END)
    {
        place(event.scancode == SDL_SCANCODE_HOME ? 0 : text.size(), shift);
    }
    else if (event.scancode == SDL_SCANCODE_LEFT || event.scancode == SDL_SCANCODE_RIGHT)
    {
        const bool left = event.scancode == SDL_SCANCODE_LEFT;
        if (first != last && !shift && !control)
        {
            place(left ? first : last);
        }
        else
        {
            place(control ? word(text, cursor, left ? -1 : 1)
                          : left ? previous(text, cursor) : next(text, cursor), shift);
        }
    }
    else if (event.scancode == SDL_SCANCODE_BACKSPACE || event.scancode == SDL_SCANCODE_DELETE)
    {
        if (first == last)
        {
            const bool left = event.scancode == SDL_SCANCODE_BACKSPACE;
            place(control ? word(text, cursor, left ? -1 : 1)
                          : left ? previous(text, cursor) : next(text, cursor), true);
        }
        erase_selection(text);
    }
}
