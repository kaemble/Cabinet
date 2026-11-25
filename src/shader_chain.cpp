#include "shader_chain.h"
#include "shader_pass.h"
#include <stdexcept>
#include <string_view>

struct ShaderChain::Impl
{
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* normalized = nullptr;
    unsigned width = 0, height = 0;
    std::vector<std::unique_ptr<ShaderPass>> passes;
    unsigned frame_count = 0;
    ~Impl()
    {
        passes.clear();
        SDL_DestroyTexture(normalized);
    }
};

ShaderChain::ShaderChain() = default;
ShaderChain::~ShaderChain() = default;

bool ShaderChain::supported(SDL_Renderer* renderer)
{
    const std::string_view name = SDL_GetRendererName(renderer);
    return name == "opengl" || name == "opengles2" || name == "vulkan";
}

void ShaderChain::reset()
{
    impl.reset();
}

bool ShaderChain::allocated() const
{
    return impl != nullptr;
}

SDL_Texture* ShaderChain::draw(SDL_Renderer* renderer, SDL_Texture* input, unsigned width, unsigned height,
                               const shaders::Stack& settings)
{
    if (!settings.active())
    {
        reset();
        return input;
    }
    if (!supported(renderer))
    {
        throw std::runtime_error("Shaders require an OpenGL, OpenGL ES, or Vulkan renderer");
    }
    if (impl && impl->renderer != renderer)
    {
        reset();
    }
    if (!impl)
    {
        impl = std::make_unique<Impl>();
        impl->renderer = renderer;
    }
    auto& state = *impl;
    const bool vulkan = std::string_view(SDL_GetRendererName(renderer)) == "vulkan";
    const char* property = vulkan ? SDL_PROP_TEXTURE_VULKAN_TEXTURE_NUMBER
                           : std::string_view(SDL_GetRendererName(renderer)) == "opengl"
                               ? SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER
                               : SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER;
    const auto input_props = SDL_GetTextureProperties(input);
    const auto format = SDL_GetNumberProperty(input_props, SDL_PROP_TEXTURE_FORMAT_NUMBER, 0);
    if (!SDL_GetNumberProperty(input_props, property, 0) || (!vulkan && format != SDL_PIXELFORMAT_RGBA32))
    {
        // SDL's CPU-format conversion wrappers hide their native textures.
        // Its GL copy shaders can also swizzle channels. Normalize both cases
        // on the GPU only when an effect is enabled.
        if (!state.normalized || state.width != width || state.height != height)
        {
            SDL_DestroyTexture(state.normalized);
            state.normalized =
                SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, width, height);
            if (!state.normalized)
            {
                throw std::runtime_error(SDL_GetError());
            }
            SDL_SetTextureBlendMode(state.normalized, SDL_BLENDMODE_NONE);
            state.width = width;
            state.height = height;
        }
        auto* previous = SDL_GetRenderTarget(renderer);
        SDL_ScaleMode filter;
        SDL_GetTextureScaleMode(input, &filter);
        SDL_SetTextureScaleMode(input, SDL_SCALEMODE_NEAREST);
        const SDL_FRect source{0, 0, float(width), float(height)};
        const bool target_set = SDL_SetRenderTarget(renderer, state.normalized);
        const bool copied = target_set && SDL_RenderTexture(renderer, input, &source, nullptr);
        const bool restored = SDL_SetRenderTarget(renderer, previous);
        SDL_SetTextureScaleMode(input, filter);
        if (!copied || !restored)
        {
            throw std::runtime_error(SDL_GetError());
        }
        input = state.normalized;
    }
    std::size_t pass = 0;
    const unsigned frame_count = state.frame_count++;
    for (const auto& shader : settings.effects)
    {
        if (!shader.enabled)
        {
            continue;
        }
        if (pass == state.passes.size())
        {
            state.passes.push_back(nullptr);
        }
        if (!state.passes[pass] || state.passes[pass]->effect != shader.effect)
        {
            state.passes[pass] = vulkan ? create_vulkan_shader_pass(renderer, shader.effect)
                                      : create_gl_shader_pass(renderer, shader.effect);
        }
        input = state.passes[pass++]->draw(input, width, height, shader, frame_count);
    }
    state.passes.resize(pass);
    return input;
}
