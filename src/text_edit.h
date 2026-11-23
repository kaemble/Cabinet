#pragma once
#include <SDL3/SDL.h>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

class TextEdit
{
public:
    void begin(const std::string& text);
    void place(std::size_t position, bool extend = false);
    void key(std::string& text, const SDL_KeyboardEvent& event);
    void insert(std::string& text, std::string_view value);
    std::size_t position() const;
    std::pair<std::size_t, std::size_t> selection() const;
    static std::size_t next(std::string_view text, std::size_t position);

private:
    std::size_t cursor = 0, anchor = 0;
    static std::size_t previous(std::string_view text, std::size_t position);
    static std::size_t word(std::string_view text, std::size_t position, int direction);
    void erase_selection(std::string& text);
};
