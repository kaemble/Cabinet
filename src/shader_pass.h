#pragma once

#include "shader_settings.h"
#include <SDL3/SDL.h>
#include <array>
#include <memory>

// Source scale and half-texel bounds, followed by animation and the native texture offset.
inline std::array<float, 8> shader_source_values(SDL_Texture* input, const SDL_FRect& source, bool flipped,
                                                unsigned frame_count)
{
    float width, height;
    SDL_GetTextureSize(input, &width, &height);
    return {source.w / width, (flipped ? -source.h : source.h) / height, 0.5f / source.w, 0.5f / source.h,
            float(frame_count % 65536), source.x / width, (source.y + (flipped ? source.h : 0)) / height, 0};
}

class ShaderPass
{
  public:
    explicit ShaderPass(shaders::Effect effect) : effect(effect)
    {
    }
    const shaders::Effect effect;
    virtual ~ShaderPass() = default;
    virtual SDL_Texture* draw(SDL_Texture* input, unsigned width, unsigned height, const shaders::Shader& settings,
                              unsigned frame_count, const SDL_FRect& source, bool flipped) = 0;
};

std::unique_ptr<ShaderPass> create_gl_shader_pass(SDL_Renderer* renderer, shaders::Effect effect);
std::unique_ptr<ShaderPass> create_vulkan_shader_pass(SDL_Renderer* renderer, shaders::Effect effect);
