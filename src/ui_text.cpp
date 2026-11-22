#include "ui_text.h"
#include <cstdint>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <string_view>

namespace
{
struct PixelTransform
{
    float x = 0, y = 0, sx = 1, sy = 1;
    explicit PixelTransform(SDL_Renderer* renderer)
    {
        auto* window = SDL_GetRenderWindow(renderer);
        int w, h, pw, ph;
        float end_x, end_y;
        if (!window || !SDL_GetWindowSize(window, &w, &h) || !SDL_GetRenderOutputSize(renderer, &pw, &ph) || w <= 0 ||
            h <= 0 || !SDL_RenderCoordinatesToWindow(renderer, 0, 0, &x, &y) ||
            !SDL_RenderCoordinatesToWindow(renderer, 1, 1, &end_x, &end_y))
        {
            throw std::runtime_error("Could not determine native text resolution");
        }
        sx = (end_x - x) * pw / w;
        sy = (end_y - y) * ph / h;
        x *= static_cast<float>(pw) / w;
        y *= static_cast<float>(ph) / h;
    }
};

// Text bypasses the UI transform; restore it before drawing the next UI element.
struct NativePixels
{
    SDL_Renderer* renderer;
    int logical_w = 0, logical_h = 0;
    SDL_RendererLogicalPresentation mode{};
    float sx = 1, sy = 1;
    SDL_Rect viewport{}, clip{};
    bool has_viewport, has_clip;
    NativePixels(SDL_Renderer* renderer, const PixelTransform& transform)
        : renderer(renderer), has_viewport(SDL_RenderViewportSet(renderer)), has_clip(SDL_RenderClipEnabled(renderer))
    {
        SDL_GetRenderLogicalPresentation(renderer, &logical_w, &logical_h, &mode);
        SDL_GetRenderScale(renderer, &sx, &sy);
        SDL_GetRenderViewport(renderer, &viewport);
        SDL_GetRenderClipRect(renderer, &clip);
        SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
        SDL_SetRenderScale(renderer, 1, 1);
        SDL_SetRenderViewport(renderer, nullptr);
        if (has_clip)
        {
            int x = static_cast<int>(std::round(transform.x + clip.x * transform.sx));
            int y = static_cast<int>(std::round(transform.y + clip.y * transform.sy));
            SDL_Rect pixels{x, y, static_cast<int>(std::round(clip.w * transform.sx)),
                            static_cast<int>(std::round(clip.h * transform.sy))};
            SDL_SetRenderClipRect(renderer, &pixels);
        }
        else
        {
            SDL_SetRenderClipRect(renderer, nullptr);
        }
    }
    ~NativePixels()
    {
        SDL_SetRenderLogicalPresentation(renderer, logical_w, logical_h, mode);
        SDL_SetRenderScale(renderer, sx, sy);
        SDL_SetRenderViewport(renderer, has_viewport ? &viewport : nullptr);
        SDL_SetRenderClipRect(renderer, has_clip ? &clip : nullptr);
    }
};
}

// Embedded by CMake so the fonts are available without external asset files.
extern const unsigned char cabinet_regular_font_data[];
extern const unsigned long cabinet_regular_font_size;
extern const unsigned char cabinet_condensed_font_data[];
extern const unsigned long cabinet_condensed_font_size;

TextRenderer::TextRenderer(SDL_Renderer* renderer, Font font) : renderer(renderer)
{
    if (FT_Init_FreeType(&library))
    {
        throw std::runtime_error("Could not initialize font renderer");
    }
    const auto* data = (font == Font::Condensed) ? cabinet_condensed_font_data : cabinet_regular_font_data;
    const auto size = (font == Font::Condensed) ? cabinet_condensed_font_size : cabinet_regular_font_size;
    if (FT_New_Memory_Face(library, data, static_cast<FT_Long>(size), 0, &face))
    {
        FT_Done_FreeType(library);
        library = nullptr;
        throw std::runtime_error("Cabinet font could not be loaded");
    }
}
TextRenderer::~TextRenderer()
{
    for (auto& [name, texture] : icons)
    {
        SDL_DestroyTexture(texture);
    }
    for (auto& [key, glyph] : glyphs)
    {
        if (glyph.texture)
        {
            SDL_DestroyTexture(glyph.texture);
        }
    }
    FT_Done_Face(face);
    FT_Done_FreeType(library);
}
TextRenderer::Glyph& TextRenderer::glyph(std::uint32_t code, int size)
{
    auto key = std::make_pair(size, code);
    if (auto found = glyphs.find(key); found != glyphs.end())
    {
        return found->second;
    }
    FT_Set_Pixel_Sizes(face, 0, size);
    if (FT_Load_Char(face, code, FT_LOAD_RENDER))
    {
        throw std::runtime_error("Could not render text glyph");
    }
    auto slot = face->glyph;
    Glyph g;
    g.width = slot->bitmap.width;
    g.height = slot->bitmap.rows;
    g.left = slot->bitmap_left;
    g.top = slot->bitmap_top;
    g.advance = slot->advance.x >> 6;
    if (g.width && g.height)
    {
        SDL_Surface* surface = SDL_CreateSurface(g.width, g.height, SDL_PIXELFORMAT_RGBA32);
        if (!surface)
        {
            throw std::runtime_error(SDL_GetError());
        }
        for (int y = 0; y < g.height; ++y)
        {
            auto* row = static_cast<std::uint8_t*>(surface->pixels) + y * surface->pitch;
            for (int x = 0; x < g.width; ++x)
            {
                row[x * 4] = row[x * 4 + 1] = row[x * 4 + 2] = 255;
                row[x * 4 + 3] = slot->bitmap.buffer[y * slot->bitmap.pitch + x];
            }
        }
        g.texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_DestroySurface(surface);
        if (!g.texture)
        {
            throw std::runtime_error(SDL_GetError());
        }
        SDL_SetTextureBlendMode(g.texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(g.texture, SDL_SCALEMODE_NEAREST);
    }
    return glyphs.emplace(key, g).first->second;
}
float TextRenderer::width(const std::string& text, int size)
{
    PixelTransform transform(renderer);
    const int pixels = std::max(1, static_cast<int>(std::round(size * transform.sy)));
    float result = 0;
    const char* p = text.c_str();
    while (*p)
    {
        result += glyph(SDL_StepUTF8(&p, nullptr), pixels).advance;
    }
    return result / transform.sx;
}
void TextRenderer::draw(float x, float y, const std::string& text, int size, SDL_Color color, float max_width)
{
    PixelTransform transform(renderer);
    if (transform.sx <= 0 || transform.sy <= 0 || max_width <= 0)
    {
        return;
    }
    const int pixels = std::max(1, static_cast<int>(std::round(size * transform.sy)));
    auto measure = [&](const std::string& value)
    {
        float width = 0;
        const char* p = value.c_str();
        while (*p)
        {
            width += glyph(SDL_StepUTF8(&p, nullptr), pixels).advance;
        }
        return width;
    };
    const float limit = max_width * transform.sx;
    const bool truncated = measure(text) > limit;
    const float ellipsis = truncated ? measure("…") : 0;
    if (ellipsis > limit)
    {
        return;
    }
    x = std::round(transform.x + x * transform.sx);
    y = std::round(transform.y + y * transform.sy);
    const float end = x + limit - ellipsis;
    NativePixels native(renderer, transform);
    auto render = [&](Glyph& g)
    {
        if (!g.texture)
        {
            return;
        }
        SDL_SetTextureColorMod(g.texture, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(g.texture, color.a);
        SDL_FRect dest{x + g.left, y + pixels - g.top, static_cast<float>(g.width), static_cast<float>(g.height)};
        SDL_RenderTexture(renderer, g.texture, nullptr, &dest);
    };
    const char* p = text.c_str();
    while (*p)
    {
        auto& g = glyph(SDL_StepUTF8(&p, nullptr), pixels);
        if (x + g.advance > end)
        {
            if (truncated)
            {
                render(glyph(0x2026, pixels));
            }
            break;
        }
        render(g);
        x += g.advance;
    }
}

SDL_Surface* load_prompt_icon(std::string_view name);
void TextRenderer::icon(float x, float y, float size, const std::string& name)
{
    auto found = icons.find(name);
    if (found == icons.end())
    {
        SDL_Surface* surface = load_prompt_icon(name);
        if (!surface)
        {
            throw std::runtime_error("Could not load prompt icon: " + name);
        }
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_DestroySurface(surface);
        if (!texture)
        {
            throw std::runtime_error(SDL_GetError());
        }
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
        found = icons.emplace(name, texture).first;
    }
    SDL_FRect destination{x, y, size, size};
    SDL_RenderTexture(renderer, found->second, nullptr, &destination);
}
