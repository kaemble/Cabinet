#pragma once
#include <SDL3/SDL.h>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

class BoxArt
{
public:
    BoxArt(SDL_Renderer* renderer, const std::filesystem::path& data);
    void select(const std::filesystem::path& source);
    void select_data(const std::string& data);
    void save(const std::string& game_id);
    void reset();
    bool pending() const;
    bool draw(const std::string& game_id, const SDL_FRect& area, bool preview = false);
    std::filesystem::path file(const std::string& game_id) const;

private:
    void stage(SDL_Surface* image);
    using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
    using Texture = std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>;
    SDL_Renderer* renderer;
    std::filesystem::path directory;
    Surface selected{nullptr, SDL_DestroySurface};
    Texture preview_texture{nullptr, SDL_DestroyTexture};
    std::map<std::string, Texture> textures;
};

bool boxart_image(const std::filesystem::path& path);
