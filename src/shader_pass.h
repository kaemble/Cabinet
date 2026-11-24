#pragma once

#include "shader_settings.h"
#include <SDL3/SDL.h>
#include <memory>

class ShaderPass
{
  public:
    virtual ~ShaderPass() = default;
    virtual SDL_Texture* draw(SDL_Texture* input, unsigned width, unsigned height, const shaders::Shader& settings) = 0;
};

std::unique_ptr<ShaderPass> create_gl_shader_pass(SDL_Renderer* renderer);
std::unique_ptr<ShaderPass> create_vulkan_shader_pass(SDL_Renderer* renderer);
