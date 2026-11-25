#pragma once

#include "shader_settings.h"
#include <SDL3/SDL.h>
#include <memory>

// Owns only effect resources. Input textures belong to the frame producer.
class ShaderChain
{
  public:
    ShaderChain();
    ~ShaderChain();
    ShaderChain(const ShaderChain&) = delete;
    ShaderChain& operator=(const ShaderChain&) = delete;
    static bool supported(SDL_Renderer* renderer);
    // Crop is in input-texture pixels. Active chains return an upright texture at the last effect’s resolution.
    SDL_Texture* draw(SDL_Renderer* renderer, SDL_Texture* input, unsigned width, unsigned height,
                      const shaders::Stack& settings, const SDL_FRect* crop = nullptr, bool flipped = false,
                      SDL_Point output_size = {});
    void reset();
    bool allocated() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
