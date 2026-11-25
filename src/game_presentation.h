#pragma once

#include "aspect_ratio.h"
#include "dual_screen.h"
#include "shader_chain.h"
#include <SDL3/SDL.h>

#ifdef CABINET_ARMADA_DRM
class ArmadaDrm;
#endif

// Composes one game view. Source images are borrowed; effect textures and Sharp
// Bilinear intermediates belong to this object. Reset it before destroying its renderer.
class GamePresentation
{
public:
    struct Image
    {
        SDL_Texture* texture = nullptr;
        unsigned width = 0, height = 0;
        float aspect = 1;
        int aspect_mode = aspect_ratio::Auto, scaling = 2;
        bool flipped = false;
        const shaders::Stack* shaders = nullptr;
    };

    GamePresentation() = default;
    ~GamePresentation();
    GamePresentation(const GamePresentation&) = delete;
    GamePresentation& operator=(const GamePresentation&) = delete;

    // Rendering and pointer input must use the same drawable-pixel viewport.
    static SDL_FRect viewport(float width, float height, float aspect, int aspect_mode, unsigned frame_width = 0,
                              unsigned frame_height = 0, int scaling = 0);
    static SDL_FRect screen_viewport(const Image& frame, const dual_screen::View& view);

    void draw(SDL_Renderer* renderer, const Image& frame, const SDL_FRect& destination, const SDL_FRect* crop = nullptr
#ifdef CABINET_ARMADA_DRM
              ,
              ArmadaDrm* scanout = nullptr
#endif
    );
    void reset();
    void reset_shaders();
    SDL_Point intermediate_size() const
    {
        return intermediate_dimensions;
    }

private:
    void reset_intermediate();
    ShaderChain shader_chain;
    SDL_Texture* intermediate = nullptr;
    SDL_Point intermediate_dimensions{};
};
