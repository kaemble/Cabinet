#include "game_presentation.h"
#ifdef CABINET_ARMADA_DRM
#include "armada_drm.h"
#endif
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
void check(bool success)
{
    if (!success)
    {
        throw std::runtime_error(SDL_GetError());
    }
}
}

GamePresentation::~GamePresentation()
{
    reset();
}

void GamePresentation::reset()
{
    reset_shaders();
    reset_intermediate();
}

void GamePresentation::reset_shaders()
{
    shader_chain.reset();
}

void GamePresentation::reset_intermediate()
{
    if (intermediate)
    {
        SDL_DestroyTexture(intermediate);
    }
    intermediate = nullptr;
    intermediate_dimensions = {};
}

SDL_FRect GamePresentation::viewport(float width, float height, float aspect, int aspect_mode, unsigned frame_width,
                                     unsigned frame_height, int scaling)
{
    if (scaling == 3 && frame_width && frame_height)
    {
        const float fit = std::min(width / frame_width, height / frame_height);
        const float factor = (fit >= 1) ? std::floor(fit) : fit;
        const float draw_width = frame_width * factor, draw_height = frame_height * factor;
        return {std::floor((width - draw_width) / 2), std::floor((height - draw_height) / 2), draw_width, draw_height};
    }
    float draw_width = width, draw_height = height;
    aspect = aspect_ratio::value(aspect_mode, aspect);
    if (aspect_mode != aspect_ratio::Stretch)
    {
        draw_height = draw_width / aspect;
        if (draw_height > height)
        {
            draw_height = height;
            draw_width = draw_height * aspect;
        }
    }
    return {(width - draw_width) / 2, (height - draw_height) / 2, draw_width, draw_height};
}

SDL_FRect GamePresentation::screen_viewport(const Image& frame, const dual_screen::View& view)
{
    const auto source = dual_screen::pixels(view.source, frame.width, frame.height);
    auto destination = viewport(view.cell.w, view.cell.h, view.aspect, frame.aspect_mode,
                                static_cast<unsigned>(source.w), static_cast<unsigned>(source.h), frame.scaling);
    destination.x += view.cell.x;
    destination.y += view.cell.y;
    return destination;
}

void GamePresentation::draw(SDL_Renderer* renderer, const Image& frame, const SDL_FRect& destination,
                            const SDL_FRect* crop
#ifdef CABINET_ARMADA_DRM
                            ,
                            ArmadaDrm* scanout
#endif
)
{
    auto* texture = frame.texture;
    SDL_FRect source =
        crop ? *crop : SDL_FRect{0, 0, static_cast<float>(frame.width), static_cast<float>(frame.height)};
    SDL_FlipMode flip = frame.flipped ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE;
    source = dual_screen::texture_source(source, frame.height, frame.flipped);
    if (frame.shaders && frame.shaders->active())
    {
        texture = shader_chain.draw(renderer, texture, frame.width, frame.height, *frame.shaders, &source,
                                    frame.flipped);
        source.x = source.y = 0;
        check(SDL_GetTextureSize(texture, &source.w, &source.h));
        flip = SDL_FLIP_NONE;
    }
    else
    {
        reset_shaders();
    }
    const auto filter = (frame.scaling == 1 || frame.scaling == 2) ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST;
#ifdef CABINET_ARMADA_DRM
    if (scanout && scanout->native_vulkan())
    {
        // Native Vulkan applies the integer enlargement in its shader, avoiding
        // SDL's synchronous render-target switches and an intermediate texture.
        int factor = 1;
        if (frame.scaling == 2)
        {
            factor =
                std::max(1, static_cast<int>(std::floor(std::min(destination.w / source.w, destination.h / source.h))));
        }
        check(SDL_SetTextureScaleMode(texture, filter));
        scanout->render_texture(texture, source, destination, flip, factor);
        return;
    }
#endif
    // A 1x intermediate only copies the image. Downscaling and small viewports
    // use bilinear directly; both passes of integer enlargement stay on the GPU.
    if (frame.scaling == 2 && destination.w >= source.w * 2.0f && destination.h >= source.h * 2.0f)
    {
        const int factor =
            std::max(1, static_cast<int>(std::floor(std::min(destination.w / source.w, destination.h / source.h))));
        const int width = static_cast<int>(source.w) * factor, height = static_cast<int>(source.h) * factor;
        if (!intermediate || intermediate_dimensions.x != width || intermediate_dimensions.y != height)
        {
            reset_intermediate();
            intermediate =
                SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
            check(intermediate != nullptr);
            intermediate_dimensions = {width, height};
            check(SDL_SetTextureBlendMode(intermediate, SDL_BLENDMODE_NONE));
        }
        check(SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST));
        auto* previous = SDL_GetRenderTarget(renderer);
        check(SDL_SetRenderTarget(renderer, intermediate));
        const bool rendered = SDL_RenderTextureRotated(renderer, texture, &source, nullptr, 0, nullptr, flip);
        check(SDL_SetRenderTarget(renderer, previous));
        check(rendered);
        texture = intermediate;
        source = {0, 0, static_cast<float>(width), static_cast<float>(height)};
        flip = SDL_FLIP_NONE;
    }
    else
    {
        reset_intermediate();
    }
    check(SDL_SetTextureScaleMode(texture, filter));
#ifdef CABINET_ARMADA_DRM
    if (scanout)
    {
        scanout->render_texture(texture, source, destination, flip);
        return;
    }
#endif
    check(SDL_RenderTextureRotated(renderer, texture, &source, &destination, 0, nullptr, flip));
}
