#pragma once
#include <cstdint>
#include <SDL3/SDL.h>
#include <libretro.h>
#include <memory>

// The core owns its GL state; SDL owns a separate context for menus and text.
class HardwareVideo
{
public:
    HardwareVideo();
    ~HardwareVideo();
    HardwareVideo(const HardwareVideo&) = delete;
    HardwareVideo& operator=(const HardwareVideo&) = delete;
    static bool supports(retro_hw_context_type type);
    void create_context(const retro_hw_render_callback& callback, SDL_Window* share_window = nullptr,
                        SDL_GLContext share_context = nullptr);
    SDL_Renderer* create_renderer(SDL_Window* window);
    void attach_renderer(SDL_Window* window, SDL_Renderer* renderer, SDL_GLContext context);
    SDL_GLContext renderer_context() const;
    SDL_Texture* wrap_texture();
    void finish_frame();
    bool flipped() const;
    void create_framebuffer(unsigned width, unsigned height);
    void make_current();
    std::uintptr_t framebuffer() const;
    void validate_frame(unsigned width, unsigned height) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
