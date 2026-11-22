#pragma once
#include <cstdint>
#include <SDL3/SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <map>
#include <string>

class TextRenderer
{
public:
    enum class Font
    {
        Regular,
        Condensed
    };
    explicit TextRenderer(SDL_Renderer* renderer, Font font = Font::Regular);
    ~TextRenderer();
    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;
    void draw(float x, float y, const std::string& text, int size, SDL_Color color, float max_width = 10000);
    void icon(float x, float y, float size, const std::string& name);
    float width(const std::string& text, int size);

private:
    struct Glyph
    {
        SDL_Texture* texture = nullptr;
        int width = 0, height = 0, left = 0, top = 0, advance = 0;
    };
    SDL_Renderer* renderer;
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    std::map<std::pair<int, std::uint32_t>, Glyph> glyphs;
    std::map<std::string, SDL_Texture*> icons;
    Glyph& glyph(std::uint32_t code, int size);
};
