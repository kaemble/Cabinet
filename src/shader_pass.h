#pragma once

#include "shader_settings.h"
#include <SDL3/SDL.h>
#include <memory>

class ShaderPass
{
  public:
    explicit ShaderPass(shaders::Effect effect) : effect(effect)
    {
    }
    const shaders::Effect effect;
    virtual ~ShaderPass() = default;
    virtual SDL_Texture* draw(SDL_Texture* input, unsigned width, unsigned height, const shaders::Shader& settings,
                              unsigned frame_count) = 0;
};

std::unique_ptr<ShaderPass> create_gl_shader_pass(SDL_Renderer* renderer, shaders::Effect effect);
std::unique_ptr<ShaderPass> create_vulkan_shader_pass(SDL_Renderer* renderer, shaders::Effect effect);
