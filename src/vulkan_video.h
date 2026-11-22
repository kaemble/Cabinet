#pragma once
#include <SDL3/SDL.h>
#include <libretro_vulkan.h>
#include <memory>

// Vulkan snapshot bridge with fenced frame resources. SDL owns presentation.
class VulkanVideo
{
public:
    VulkanVideo();
    ~VulkanVideo();
    void negotiate(const retro_hw_render_context_negotiation_interface_vulkan* negotiation);
    SDL_Renderer* create_renderer(SDL_Window* window, bool fullscreen = false, bool direct_display = false);
    SDL_Renderer* create_secondary_renderer(SDL_Window* window);
    SDL_Texture* secondary_texture();
    void destroy_secondary_renderer();
    const retro_hw_render_interface* interface() const;
    // Prepare a stable libretro synchronization slot before each core run.
    void begin_frame();
    SDL_Texture* frame(unsigned width, unsigned height);
    void idle();
    // Exclude core submissions around SDL calls which may use the shared queue.
    // This lock is non-recursive: do not call other locking VulkanVideo methods
    // while holding it. Frame-resource waits acquire their own independent lock.
    void lock();
    void unlock();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
