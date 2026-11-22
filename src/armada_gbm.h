#pragma once
#include <SDL3/SDL.h>
#include <memory>
#include <cstdint>

// Renderable scanout storage, imported into SDL's own EGL/GL renderer. Never
// constructed for Vulkan/software renderers or outside the Armada lease path.
class ArmadaGbm
{
public:
    static std::unique_ptr<ArmadaGbm> create(SDL_Renderer* renderer, int lease_fd, std::uint32_t plane, int width,
                                             int height);
    ~ArmadaGbm();
    SDL_Texture* target(unsigned buffer) const;
    std::uint32_t framebuffer(unsigned buffer) const;
    // A returned native fence fd belongs to the caller. Otherwise ready()
    // polls the EGL fence without blocking before submitting the KMS commit.
    int finish(bool native_fence);
    bool ready();

private:
    struct Impl;
    explicit ArmadaGbm(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
