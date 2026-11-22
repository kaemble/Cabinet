#pragma once
#include <cstdint>
#include <SDL3/SDL.h>
#include <memory>

// Constructed exclusively by the --dual-screen branch. No global initialization
// or environment probes run merely because this backend is linked into a runner.
class ArmadaDrm
{
public:
    static bool configure_video(); // Returns true for Armada Game Mode; call only for --dual-screen.
    static std::unique_ptr<ArmadaDrm> try_create(SDL_Renderer* renderer);
    ~ArmadaDrm();
    void pump();
    bool begin_draw();         // False while a previous flip is pending; drop this frame.
    void present();            // Flush the target and queue scanout after rendering completes.
    void after_main_present(); // Submit native Vulkan work after SDL has submitted the source image.
    void clear();
    void dim(std::uint8_t alpha);
    bool native_vulkan() const;
    void render_texture(SDL_Texture* texture, const SDL_FRect& source, const SDL_FRect& destination, SDL_FlipMode flip,
                        int sharp_factor = 1);
    bool gpu_scanout() const;
    bool presentation_pending() const;
    int width() const;
    int height() const;
    int rotation() const; // Clockwise quarter turns from logical to panel coordinates.
    struct Pointer
    {
        float x, y;
        bool down;
    };
    Pointer pointer() const;

private:
    struct Impl;
    explicit ArmadaDrm(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
