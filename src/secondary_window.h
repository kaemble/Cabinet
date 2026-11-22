#pragma once
#include <cstdint>
#include <SDL3/SDL.h>
#include <string>

class HardwareVideo;
class VulkanVideo;

// A second presentation surface for the same core frame. Hardware textures stay
// on the GPU; software frames are uploaded directly from the core callback.
class SecondaryWindow
{
public:
    ~SecondaryWindow();
    void create(SDL_Window* main_window, SDL_Renderer* main_renderer, HardwareVideo* hardware, VulkanVideo* vulkan,
                const std::string& title, int width, int height);
    void upload(const void* pixels, unsigned width, unsigned height, int pitch, SDL_PixelFormat format);
    SDL_Texture* texture(SDL_Texture* primary);
    void begin_draw();
    void end_draw();
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

private:
    SDL_Window* main_window_ = nullptr;
    SDL_Renderer* main_renderer_ = nullptr;
    SDL_GLContext main_context_ = nullptr, context_ = nullptr;
    VulkanVideo* vulkan_ = nullptr;
    SDL_FunctionPointer fence_sync_ = nullptr, wait_sync_ = nullptr, delete_sync_ = nullptr;
    SDL_FunctionPointer flush_ = nullptr, finish_ = nullptr;
    SDL_FunctionPointer get_integer_ = nullptr, bind_texture_ = nullptr, texture_parameter_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    SDL_Texture* primary_ = nullptr;
    std::int64_t native_ = 0;
    unsigned width_ = 0, height_ = 0;
    SDL_PixelFormat format_ = SDL_PIXELFORMAT_UNKNOWN;
    void handoff(SDL_Window* target, SDL_GLContext context);
    void restore_filter(SDL_Texture* state);
};
