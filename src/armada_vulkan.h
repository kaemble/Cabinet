#pragma once
#include <SDL3/SDL.h>
#include <memory>
#include <cstdint>

// Native Vulkan composition into a swapchain on the leased panel. Borrows the
// top SDL renderer's instance/device/graphics queue; no second GPU or readback.
class ArmadaVulkan
{
public:
    static std::unique_ptr<ArmadaVulkan> create(SDL_Renderer* renderer, int lease_fd, std::uint32_t connector,
                                                unsigned width, unsigned height);
    ~ArmadaVulkan();
    bool begin(); // Zero-time acquire and fence checks; false drops a bottom frame.
    void draw(SDL_Texture* texture, SDL_FRect source, SDL_FRect destination, SDL_FlipMode flip, int logical_width,
              int logical_height, int turns, int sharp_factor = 1);
    void dim(std::uint8_t alpha);
    void present(); // Call after SDL_RenderPresent, under the shared queue lock.
private:
    struct Impl;
    explicit ArmadaVulkan(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
