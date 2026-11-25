#include "shader_chain.h"
#include "shader_pass.h"
#include "shader_programs.h"
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace
{
struct EffectPass : ShaderPass
{
    SDL_Renderer* renderer;
    std::vector<std::unique_ptr<ShaderPass>> stages;
    std::vector<SDL_Texture*> lookups;

    EffectPass(SDL_Renderer* renderer, shaders::Effect effect, bool vulkan) : ShaderPass(effect), renderer(renderer)
    {
        const auto& program = shaders::program(effect);
        for (std::size_t stage = 0; stage < program.stages.size(); ++stage)
        {
            stages.push_back(vulkan ? create_vulkan_shader_pass(renderer, effect, stage)
                                    : create_gl_shader_pass(renderer, effect, stage));
        }
    }
    ~EffectPass() override
    {
        stages.clear();
        for (auto* texture : lookups)
        {
            SDL_DestroyTexture(texture);
        }
    }
    SDL_Texture* draw(SDL_Texture* input, unsigned width, unsigned height, const shaders::Shader& settings,
                      unsigned frame_count, const SDL_FRect& source, bool flipped,
                      std::span<const ShaderInput> = {}) override
    {
        const auto& program = shaders::program(effect);
        while (lookups.size() < program.textures.size())
        {
            const auto& data = program.textures[lookups.size()];
            auto* texture =
                SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, data.width, data.height);
            if (!texture)
            {
                throw std::runtime_error(SDL_GetError());
            }
            if (!SDL_UpdateTexture(texture, nullptr, data.pixels.data(), data.width * 4))
            {
                SDL_DestroyTexture(texture);
                throw std::runtime_error(SDL_GetError());
            }
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
            lookups.push_back(texture);
        }
        if (stages.size() == 1 && lookups.empty())
        {
            return stages.front()->draw(input, width, height, settings, frame_count, source, flipped);
        }
        std::array<ShaderInput, shaders::maximum_sampler_count> inputs{};
        std::size_t previous = 0;
        SDL_Texture* result = input;
        for (auto& stage : stages)
        {
            for (std::size_t i = 0; i < lookups.size(); ++i)
            {
                inputs[previous + i] = {lookups[i], program.textures[i].linear};
            }
            result = stage->draw(input, width, height, settings, frame_count, source, flipped,
                                 std::span(inputs.data(), previous + lookups.size()));
            inputs[previous++] = {result, true};
        }
        return result;
    }
};
} // namespace

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
                               const shaders::Stack& settings, const SDL_FRect* crop, bool flipped, SDL_Point output_size)
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
    SDL_FRect source = crop ? *crop : SDL_FRect{0, 0, float(width), float(height)};
    width = static_cast<unsigned>(std::lround(source.w));
    height = static_cast<unsigned>(std::lround(source.h));
    if (!width || !height)
    {
        throw std::runtime_error("Empty shader viewport");
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
        const bool target_set = SDL_SetRenderTarget(renderer, state.normalized);
        const bool copied = target_set && SDL_RenderTextureRotated(renderer, input, &source, nullptr, 0, nullptr,
                                                                   flipped ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE);
        const bool restored = SDL_SetRenderTarget(renderer, previous);
        SDL_SetTextureScaleMode(input, filter);
        if (!copied || !restored)
        {
            throw std::runtime_error(SDL_GetError());
        }
        input = state.normalized;
        source = {0, 0, float(width), float(height)};
        flipped = false;
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
            state.passes[pass] = std::make_unique<EffectPass>(renderer, shader.effect, vulkan);
        }
        if (shaders::definition(shader.effect).output_resolution && output_size.x > 0 && output_size.y > 0)
        {
            width = static_cast<unsigned>(output_size.x);
            height = static_cast<unsigned>(output_size.y);
        }
        input = state.passes[pass++]->draw(input, width, height, shader, frame_count, source, flipped);
        source = {0, 0, float(width), float(height)};
        flipped = false;
    }
    state.passes.resize(pass);
    return input;
}
