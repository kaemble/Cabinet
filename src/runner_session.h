#pragma once

#include "frontend.h"
#include "hardware_video.h"
#include "vulkan_video.h"
#include "save_manager.h"
#include "runahead.h"
#include "audio_output.h"
#include "async_audio.h"
#include "libretro_core.h"
#include "input_router.h"
#include "core_options.h"
#include "secondary_window.h"
#include "game_presentation.h"
#ifdef CABINET_ARMADA_DRM
#include "armada_drm.h"
#endif
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <tuple>
#include <utility>

// One session per process. All core calls and video callbacks run on the main
// thread for this initial launcher; no concurrent access to the core occurs.
struct Session
{
    LibretroCore core;
    RunAhead runahead;
    bool runahead_active = false, speculative = false, output_enabled = true;
    std::uint64_t serialization_quirks = 0;
    std::uint64_t runahead_input_frame = 0;
    std::map<std::tuple<unsigned, unsigned, unsigned, unsigned>,
             std::pair<std::uint64_t, std::int16_t>> runahead_input;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    GamePresentation presentation, bottom_presentation;
    bool dual_screen = false;
    std::unique_ptr<SecondaryWindow> secondary;
#ifdef CABINET_ARMADA_DRM
    std::unique_ptr<ArmadaDrm> leased_bottom;
#endif
    bool focused() const
    {
        return (window && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)) ||
               (secondary && (SDL_GetWindowFlags(secondary->window) & SDL_WINDOW_INPUT_FOCUS));
    }
    AudioOutput audio;
    AsyncAudio async_audio;
    std::unique_ptr<HardwareVideo> hardware;
    std::unique_ptr<VulkanVideo> vulkan;
    bool prefer_vulkan = false;
    bool support_no_game = false;
    retro_hw_render_callback hw{};
    bool hw_ready = false;
    SDL_PixelFormat texture_format = SDL_PIXELFORMAT_UNKNOWN;
    unsigned width = 0, height = 0;
    SDL_PixelFormat format = SDL_PIXELFORMAT_XRGB1555;
    float aspect = 160.0f / 144.0f;
    bool texture_is_hardware = false;
    unsigned frames = 0;
    std::string callback_error, hardware_error, system_dir;
    CoreOptions core_options;
    bool rebuilding_video = false;
    unsigned hw_width = 0, hw_height = 0;
    double frame_rate = 60;
    retro_frame_time_callback frame_time{};
    std::uint64_t last_frame_time_ns = 0;
    void notify_frame_time(std::uint64_t now)
    {
        if (!frame_time.callback)
        {
            return;
        }
        const auto reference = frame_time.reference > 0
                                   ? frame_time.reference
                                   : static_cast<retro_usec_t>(std::llround(1000000.0 / frame_rate));
        const auto elapsed = !last_frame_time_ns || fast_forward
                                 ? reference
                                 : static_cast<retro_usec_t>((now - last_frame_time_ns) / 1000);
        last_frame_time_ns = now;
        frame_time.callback(elapsed);
    }
    bool fast_forward = false;
    int core_log_level = RETRO_LOG_WARN;
    InputRouter input;
    bool pointer_sampled = false;
    Frontend frontend;
    SaveManager saves;
    GameInputContext input_context() const
    {
        return {core_options.values, frontend.bindings.system(), frontend.paused()};
    }
    SaveCore save_core() const
    {
        return {core.api.get_memory_data, core.api.get_memory_size, core.api.serialize_size, core.api.serialize,
                core.api.unserialize};
    }
    GamePresentation::Image presentation_image() const
    {
        return {texture,
                width,
                height,
                aspect,
                frontend.aspect_mode,
                frontend.scaling,
                texture_is_hardware && hardware && hardware->flipped(),
                &frontend.shaders};
    }
    void draw_view(GamePresentation& view, SDL_Renderer* target, const GamePresentation::Image& frame,
                   const SDL_FRect& destination, const SDL_FRect* crop = nullptr
#ifdef CABINET_ARMADA_DRM
                   , ArmadaDrm* scanout = nullptr
#endif
    )
    {
        auto draw = [&]
        {
            view.draw(target, frame, destination, crop
#ifdef CABINET_ARMADA_DRM
                      , scanout
#endif
            );
        };
        try
        {
            draw();
        }
        catch (const std::exception& error)
        {
            if (!frontend.shaders.active())
            {
                throw;
            }
            for (auto& shader : frontend.shaders.effects)
            {
                shader.enabled = false;
            }
            view.reset();
            frontend.notify(std::string("Shaders disabled: ") + error.what());
            draw();
        }
    }
    ~Session();
};
