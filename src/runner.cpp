#include <SDL3/SDL.h>
#include <cstdint>
#include <cstddef>
#include <libretro.h>
#include "runner_session.h"
#include "runner_game_files.h"
#include "runner_settings.h"
#include "ui_text.h"
#include "runner_options.h"
#include "app_paths.h"
#include "save_storage.h"
#include <tuple>
#include "runner_pacing.h"
#include "runner_diagnostics.h"
#include "dual_screen.h"
#include <string_view>

#include <algorithm>
#include <optional>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <mutex>

namespace fs = std::filesystem;

static void check(bool success)
{
    if (!success)
    {
        throw std::runtime_error(SDL_GetError());
    }
}

static Session* current = nullptr;
static bool core_rumble(unsigned port, retro_rumble_effect effect, std::uint16_t strength)
{
    if (!current || port != 0 || (effect != RETRO_RUMBLE_STRONG && effect != RETRO_RUMBLE_WEAK))
    {
        return false;
    }
    if (current->speculative || current->runahead.state_operation)
    {
        return true;
    }
    return current->frontend.request_rumble(effect == RETRO_RUMBLE_STRONG, strength);
}
static void core_log(retro_log_level level, const char* fmt, ...)
{
    // Some cores emit a trace for every emulated register access. Writing
    // these to the terminal can cost more than running the emulation itself.
    if (static_cast<int>(level) < (current ? current->core_log_level : RETRO_LOG_WARN))
    {
        return;
    }
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
}
static std::uintptr_t hardware_framebuffer()
{
    return current->hardware ? current->hardware->framebuffer() : 0;
}
static retro_proc_address_t hardware_proc(const char* name)
{
    return reinterpret_cast<retro_proc_address_t>(SDL_GL_GetProcAddress(name));
}
static bool environment(unsigned command, void* data)
{
    if (current->speculative || current->runahead.state_operation)
    {
        switch (command)
        {
        case RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK:
            return data == nullptr;
        case RETRO_ENVIRONMENT_SET_VARIABLE:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        case RETRO_ENVIRONMENT_SET_GEOMETRY:
            return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            if (data)
            {
                *static_cast<bool*>(data) = false;
            }
            return true;
        }
    }
    switch (command)
    {
    case RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK:
        if (data)
        {
            current->async_audio.configure(*static_cast<const retro_audio_callback*>(data));
        }
        return true;
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS_2:
        if (!data)
        {
            return false;
        }
        current->serialization_quirks = *static_cast<std::uint64_t*>(data);
        return true;
    case RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT:
        if (!data)
        {
            return false;
        }
        *static_cast<int*>(data) = current->runahead.state_operation
                                      ? RETRO_SAVESTATE_CONTEXT_RUNAHEAD_SAME_INSTANCE
                                      : RETRO_SAVESTATE_CONTEXT_NORMAL;
        return true;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
        if (!data)
        {
            return false;
        }
        // Keep video callbacks for duplicate-frame fallback, without presenting
        // intermediate images. Muted audio must still advance the core's DSP.
        *static_cast<int*>(data) = RETRO_AV_ENABLE_VIDEO |
                                  (current->output_enabled ? RETRO_AV_ENABLE_AUDIO : 0) |
                                  (current->runahead.state_operation ? RETRO_AV_ENABLE_FAST_SAVESTATES : 0);
        return true;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        if (!data)
        {
            return false;
        }
        current->support_no_game = *static_cast<const bool*>(data);
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        switch (*static_cast<retro_pixel_format*>(data))
        {
        case RETRO_PIXEL_FORMAT_0RGB1555:
            current->format = SDL_PIXELFORMAT_XRGB1555;
            return true;
        case RETRO_PIXEL_FORMAT_RGB565:
            current->format = SDL_PIXELFORMAT_RGB565;
            return true;
        case RETRO_PIXEL_FORMAT_XRGB8888:
            current->format = SDL_PIXELFORMAT_XRGB8888;
            return true;
        default:
            return false;
        }
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *static_cast<bool*>(data) = true;
        return true;
    case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK:
        if (!data)
        {
            return false;
        }
        current->frame_time = *static_cast<const retro_frame_time_callback*>(data);
        current->last_frame_time_ns = 0;
        return true;
    case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
        if (!data)
        {
            return false;
        }
        *static_cast<bool*>(data) = current->fast_forward;
        return true;
    case RETRO_ENVIRONMENT_GET_THROTTLE_STATE:
        if (!data)
        {
            return false;
        }
        *static_cast<retro_throttle_state*>(data) = {
            static_cast<unsigned>(current->fast_forward ? RETRO_THROTTLE_FAST_FORWARD : RETRO_THROTTLE_NONE),
            current->fast_forward ? 0.0f : static_cast<float>(current->frame_rate)};
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *static_cast<const char**>(data) = current->system_dir.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *static_cast<const char**>(data) = current->saves.directory.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        static_cast<retro_log_callback*>(data)->log = core_log;
        return true;
    case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
        if (!data)
        {
            return false;
        }
        static_cast<retro_rumble_interface*>(data)->set_rumble_state = core_rumble;
        return true;
    // RetroArch's private thread-wait callback is used unconditionally by
    // threaded GLideN64 during unload and state operations. Cabinet's audio
    // callback never blocks the emulation thread, so there are no waits to clear.
    case (0x800000 | 3):
        if (!data)
        {
            return false;
        }
        *static_cast<retro_environment_t*>(data) = [](unsigned, void*)
        {
            return true;
        };
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *static_cast<bool*>(data) = current->core_options.take_update();
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *static_cast<unsigned*>(data) = 2;
        return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        *static_cast<unsigned*>(data) = RETRO_LANGUAGE_ENGLISH;
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        current->core_options.register_options(static_cast<const retro_core_option_definition*>(data),
                                               current->frontend);
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
    {
        auto* options = static_cast<const retro_core_options_intl*>(data);
        current->core_options.register_options(options ? options->us : nullptr, current->frontend);
        return true;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        current->core_options.register_options(static_cast<const retro_core_options_v2*>(data), current->frontend);
        return false; // Definitions accepted; the menu is flat, without category pages.
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
    {
        auto* options = static_cast<const retro_core_options_v2_intl*>(data);
        current->core_options.register_options(options ? options->us : nullptr, current->frontend);
        return false;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
        return data && current->core_options.set_visible(*static_cast<const retro_core_option_display*>(data),
                                                         current->frontend);
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
        current->core_options.update_display =
            data ? static_cast<const retro_core_options_update_display_callback*>(data)->callback : nullptr;
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLE:
        return !data || current->core_options.set_value(*static_cast<const retro_variable*>(data), current->frontend);
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        current->core_options.register_variables(static_cast<retro_variable*>(data), current->frontend);
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
        return current->core_options.get_variable(*static_cast<retro_variable*>(data));
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    {
        current->input.register_controllers(static_cast<const retro_controller_info*>(data),
                                            current->frontend.bindings.system());
        return true;
    }
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        return true;
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
        *static_cast<retro_hw_context_type*>(data) =
            current->prefer_vulkan ? RETRO_HW_CONTEXT_VULKAN : RETRO_HW_CONTEXT_OPENGL_CORE;
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
    {
        const auto& av = *static_cast<const retro_system_av_info*>(data);
        if (!std::isfinite(av.timing.fps) || av.timing.fps <= 0 || !std::isfinite(av.timing.sample_rate) ||
            av.timing.sample_rate < 0 || av.timing.sample_rate > 384000)
        {
            return false;
        }
        try
        {
            current->frame_rate = av.timing.fps;
            if (current->hardware && current->hw_ready && !current->rebuilding_video &&
                (current->hw_width != av.geometry.max_width || current->hw_height != av.geometry.max_height))
            {
                auto& s = *current;
                s.rebuilding_video = true;
                s.hardware->make_current();
                if (s.core_options.resolution_key == "ppsspp_internal_resolution")
                {
                    // PPSSPP handles its internal render-target resize itself after
                    // this callback. Preserve its GL objects and emulation thread;
                    // context_destroy/context_reset here can stop/restart that
                    // thread in the middle of its option update.
                    if (s.texture)
                    {
                        SDL_DestroyTexture(s.texture);
                        s.texture = nullptr;
                    }
                    s.hardware->create_framebuffer(av.geometry.max_width, av.geometry.max_height);
                }
                else
                {
                    if (s.hw.context_destroy)
                    {
                        s.hw.context_destroy();
                    }
                    s.hw_ready = false;
                    const auto ui_context = s.hardware->renderer_context();
                    if (s.texture)
                    {
                        SDL_DestroyTexture(s.texture);
                        s.texture = nullptr;
                    }
                    s.hardware.reset();
                    s.hardware = std::make_unique<HardwareVideo>();
                    s.hardware->create_context(s.hw, s.window, ui_context);
                    s.hardware->attach_renderer(s.window, s.renderer, ui_context);
                    s.hardware->create_framebuffer(av.geometry.max_width, av.geometry.max_height);
                    s.hw_width = av.geometry.max_width;
                    s.hw_height = av.geometry.max_height;
                    s.hw_ready = true;
                    s.hw.context_reset();
                }
                s.hw_width = av.geometry.max_width;
                s.hw_height = av.geometry.max_height;
                s.rebuilding_video = false;
            }
            if (av.geometry.aspect_ratio > 0)
            {
                current->aspect = av.geometry.aspect_ratio;
            }
            current->audio.set_sample_rate(av.timing.sample_rate);
            return true;
        }
        catch (const std::exception& error)
        {
            current->rebuilding_video = false;
            current->callback_error = error.what();
            return false;
        }
    }
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    {
        const auto& geometry = *static_cast<retro_game_geometry*>(data);
        if (!geometry.base_width || !geometry.base_height)
        {
            return false;
        }
        current->aspect = (geometry.aspect_ratio > 0) ? geometry.aspect_ratio
                                                      : static_cast<float>(geometry.base_width) / geometry.base_height;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT:
    {
        if (!data)
        {
            return false;
        }
        auto* n = static_cast<retro_hw_render_context_negotiation_interface*>(data);
        if (n->interface_type != RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN)
        {
            return false;
        }
        n->interface_version = 2;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE:
    {
        if (!data || current->hw_ready)
        {
            return false;
        }
        const auto* n = static_cast<const retro_hw_render_context_negotiation_interface_vulkan*>(data);
        if (n->interface_type != RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN || !n->interface_version)
        {
            return false;
        }
        if (!current->vulkan)
        {
            current->vulkan = std::make_unique<VulkanVideo>();
        }
        current->vulkan->negotiate(n);
        return true;
    }
    case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE:
        if (!data || !current->vulkan || current->hw.context_type != RETRO_HW_CONTEXT_VULKAN)
        {
            return false;
        }
        *static_cast<const retro_hw_render_interface**>(data) = current->vulkan->interface();
        return current->vulkan->interface() != nullptr;
    case RETRO_ENVIRONMENT_SET_HW_RENDER:
    {
        if (!data)
        {
            return false;
        }
        auto& callback = *static_cast<retro_hw_render_callback*>(data);
        if ((!HardwareVideo::supports(callback.context_type) && callback.context_type != RETRO_HW_CONTEXT_VULKAN) ||
            !callback.context_reset || current->hw_ready)
        {
            current->hardware_error = "Unsupported hardware context request " + std::to_string(callback.context_type) +
                                      "; Cabinet supports OpenGL, OpenGL ES and Vulkan";
            std::cerr << "Cabinet: " << current->hardware_error << '\n';
            return false;
        }
        if (callback.context_type == RETRO_HW_CONTEXT_VULKAN && !current->vulkan)
        {
            current->vulkan = std::make_unique<VulkanVideo>();
        }
        callback.get_current_framebuffer = hardware_framebuffer;
        callback.get_proc_address = hardware_proc;
        current->hw = callback;
        current->hardware_error.clear();
        return true;
    }
    default:
        return false;
    }
}

static void video(const void* pixels, unsigned width, unsigned height, std::size_t pitch)
{
    if (!current->callback_error.empty() || current->runahead.state_operation)
    {
        return;
    }
    if (!pixels)
    {
        if (current->vulkan && current->hw_ready)
        {
            try
            {
                current->vulkan->frame(0, 0);
            }
            catch (const std::exception& e)
            {
                current->callback_error = e.what();
            }
        }
        return;
    }
    auto& s = *current;
    struct RestoreCoreContext
    {
        Session& session;
        ~RestoreCoreContext()
        {
            if (!session.hardware)
            {
                return;
            }
            try
            {
                session.hardware->make_current();
            }
            catch (const std::exception& error)
            {
                session.callback_error = error.what();
            }
        }
    } restore{s};
    SDL_PixelFormat frame_format = s.format;
    const bool hardware_frame = pixels == RETRO_HW_FRAME_BUFFER_VALID;
    if (hardware_frame)
    {
        if ((!s.hardware && !s.vulkan) || !s.hw_ready)
        {
            s.callback_error = "Hardware frame without an initialized context";
            return;
        }
        try
        {
            if (s.vulkan)
            {
                if (s.texture && !s.texture_is_hardware)
                {
                    SDL_DestroyTexture(s.texture);
                }
                s.texture = s.vulkan->frame(width, height);
                if (!s.texture)
                {
                    return; // A core may present before exporting its first image.
                }
                s.texture_is_hardware = true;
                s.width = width;
                s.height = height;
                ++s.frames;
                return;
            }
            s.hardware->validate_frame(width, height);
            s.hardware->finish_frame();
            if (!s.texture || !s.texture_is_hardware)
            {
                if (s.texture)
                {
                    SDL_DestroyTexture(s.texture);
                }
                s.texture = s.hardware->wrap_texture();
            }
            s.texture_is_hardware = true;
            s.width = width;
            s.height = height;
            ++s.frames;
        }
        catch (const std::exception& error)
        {
            s.callback_error = error.what();
        }
        return;
    }
    if (!width || !height || pitch > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        s.callback_error = "Invalid video frame from core";
        return;
    }
    SDL_PixelFormat upload_format = frame_format;
#ifdef CABINET_ARMADA_DRM
    if (s.dual_screen && s.leased_bottom && s.leased_bottom->native_vulkan() &&
        frame_format != SDL_PIXELFORMAT_RGBA32 && frame_format != SDL_PIXELFORMAT_BGRA32)
    {
        upload_format = SDL_PIXELFORMAT_BGRA32;
    }
#endif
    if (!s.texture || s.texture_is_hardware || s.width != width || s.height != height ||
        s.texture_format != upload_format)
    {
        if (s.texture && !(s.vulkan && s.texture_is_hardware))
        {
            SDL_DestroyTexture(s.texture);
        }
        s.texture = SDL_CreateTexture(s.renderer, upload_format, SDL_TEXTUREACCESS_STREAMING, static_cast<int>(width),
                                      static_cast<int>(height));
        if (!s.texture)
        {
            s.callback_error = SDL_GetError();
            return;
        }
        SDL_SetTextureScaleMode(s.texture, SDL_SCALEMODE_NEAREST);
        s.texture_is_hardware = false;
        s.width = width;
        s.height = height;
        s.texture_format = upload_format;
        SDL_SetTextureBlendMode(s.texture, SDL_BLENDMODE_NONE);
    }
    // XRGB8888 already has BGRA byte order on little-endian platforms; both
    // presenters ignore alpha. Other core formats use the same CPU upload
    // conversion SDL's opaque fallback texture would otherwise perform.
    bool uploaded = false;
    if (upload_format == frame_format ||
        (frame_format == SDL_PIXELFORMAT_BGRX32 && upload_format == SDL_PIXELFORMAT_BGRA32))
    {
        uploaded = SDL_UpdateTexture(s.texture, nullptr, pixels, static_cast<int>(pitch));
    }
    else
    {
        void* target = nullptr;
        int target_pitch = 0;
        if (SDL_LockTexture(s.texture, nullptr, &target, &target_pitch))
        {
            uploaded = SDL_ConvertPixels(width, height, frame_format, pixels, static_cast<int>(pitch), upload_format,
                                         target, target_pitch);
            SDL_UnlockTexture(s.texture);
        }
    }
    if (!uploaded)
    {
        s.callback_error = SDL_GetError();
        return;
    }
    if (s.secondary)
    {
        try
        {
            s.secondary->upload(pixels, width, height, static_cast<int>(pitch), frame_format);
        }
        catch (const std::exception& error)
        {
            s.callback_error = error.what();
            return;
        }
    }
    if (s.output_enabled)
    {
        ++s.frames;
    }
}

static std::size_t audio_batch(const std::int16_t* samples, std::size_t frames)
{
    if (!current->output_enabled || current->runahead.state_operation)
    {
        return frames;
    }
    try
    {
        return current->audio.submit(samples, frames, current->fast_forward);
    }
    catch (const std::exception& error)
    {
        current->callback_error = error.what();
        return 0;
    }
}
static void set_fast_forward(Session& s, bool enabled)
{
    if (s.fast_forward == enabled)
    {
        return;
    }
    s.async_audio.set_enabled(false);
    s.fast_forward = enabled;
    s.last_frame_time_ns = 0;
    s.audio.reset(true);
    s.audio.set_frequency_ratio(1);
}
static void audio_sample(std::int16_t left, std::int16_t right)
{
    const std::int16_t samples[] = {left, right};
    audio_batch(samples, 1);
}
static void input_poll()
{
    if (current->speculative || current->runahead.state_operation)
    {
        return;
    }
    current->input.begin_poll(current->frontend.input());
    current->pointer_sampled = false;
    current->input.update_screen_swap(current->core_options.values);
}

// Sample only when the core requests pointer input. Descriptor lists are
// optional, so actual queries also cover standalone and dynamically configured cores.
static void sample_pointer()
{
    current->pointer_sampled = true;
#ifdef CABINET_ARMADA_DRM
    if (current->dual_screen && current->leased_bottom)
    {
        const auto pointer = current->leased_bottom->pointer();
        const auto view = dual_screen::window_view(current->frontend.bindings.system(), 1,
                                                   current->leased_bottom->width(), current->leased_bottom->height());
        current->input.update_pointer(current->input_context(), pointer.x, pointer.y, pointer.down, current->focused(),
                                      GamePresentation::screen_viewport(current->presentation_image(), view),
                                      &view.source);
        return;
    }
#endif
    auto* pointer_window = current->secondary ? current->secondary->window : current->window;
    auto* pointer_renderer = current->secondary ? current->secondary->renderer : current->renderer;
    if (pointer_window)
    {
        float x = 0, y = 0;
        const auto buttons = SDL_GetMouseState(&x, &y);
        int w = 0, h = 0;
        if (SDL_GetWindowSize(pointer_window, &w, &h) && w > 0 && h > 0)
        {
            int output_w = w, output_h = h;
            if (pointer_renderer)
            {
                SDL_GetRenderOutputSize(pointer_renderer, &output_w, &output_h);
            }
            if (output_w <= 0 || output_h <= 0)
            {
                output_w = w;
                output_h = h;
            }
            auto viewport =
                GamePresentation::viewport(output_w, output_h, current->aspect, current->frontend.aspect_mode,
                                           current->width, current->height, current->frontend.scaling);
            SDL_FRect touch_source{};
            if (current->dual_screen)
            {
                const auto bottom =
                    dual_screen::window_view(current->frontend.bindings.system(), 1, output_w, output_h);
                viewport = GamePresentation::screen_viewport(current->presentation_image(), bottom);
                touch_source = bottom.source;
            }
            viewport.x *= float(w) / output_w;
            viewport.w *= float(w) / output_w;
            viewport.y *= float(h) / output_h;
            viewport.h *= float(h) / output_h;
            current->input.update_pointer(current->input_context(), x, y, (buttons & SDL_BUTTON_LMASK) != 0,
                                          (SDL_GetWindowFlags(pointer_window) & SDL_WINDOW_INPUT_FOCUS) != 0 &&
                                              (current->input.pointer().drag || SDL_GetMouseFocus() == pointer_window),
                                          viewport, current->dual_screen ? &touch_source : nullptr);
        }
    }
}

static std::int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    const auto key = std::tuple{port, device, index, id};
    if (current->speculative || current->runahead.state_operation)
    {
        const auto found = current->runahead_input.find(key);
        return found == current->runahead_input.end() || found->second.first != current->runahead_input_frame
                   ? 0 : found->second.second;
    }
    if (device == RETRO_DEVICE_POINTER && port == 0 && index == 0 && !current->pointer_sampled)
    {
        sample_pointer();
    }
    auto value = current->input.state(port, device, index, id);
    // The core must keep its canonical composite while Cabinet places its views.
    if (current->dual_screen && port == 0 && device == RETRO_DEVICE_JOYPAD && index == 0)
    {
        const unsigned swap =
            current->frontend.bindings.system() == "nds" ? RETRO_DEVICE_ID_JOYPAD_R3 : RETRO_DEVICE_ID_JOYPAD_L3;
        if (id == swap)
        {
            return 0;
        }
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
        {
            value = static_cast<std::int16_t>(static_cast<std::uint16_t>(value) & ~(1u << swap));
        }
    }
    if (current->runahead_active)
    {
        current->runahead_input[key] = {current->runahead_input_frame, value};
    }
    return value;
}

static void save_state(Session& s)
{
    s.async_audio.set_enabled(false);
    if (s.vulkan)
    {
        s.vulkan->idle();
    }
    s.saves.save_state(s.save_core());
    s.frontend.has_save = true;
}

static void load_state(Session& s, const fs::path& source = {})
{
    s.async_audio.set_enabled(false);
    if (s.vulkan)
    {
        s.vulkan->idle();
    }
    try
    {
        s.saves.load_state(s.save_core(), source);
    }
    catch (const StateRecoveryError&)
    {
        s.frontend.open_menu();
        throw;
    }
}

// Successful state operations resume play; restarts rebuild the session below.
static bool execute_action(Session& session, Action action)
{
    if (action == Action::Quit)
    {
        return false;
    }
    if (session.hardware && (action == Action::Save || action == Action::Load))
    {
        session.hardware->make_current();
    }
    if (action == Action::Save)
    {
        save_state(session);
        session.frontend.resume_with_message("State saved");
    }
    else if (action == Action::Load)
    {
        session.frontend.stop_rumble();
        load_state(session);
        session.frontend.resume_with_message("State loaded");
    }
    return true;
}

static void run_core_frame(Session& session, bool core_has_run)
{
    const bool compatible = session.frontend.runahead_option_key.empty() &&
                            session.frontend.runahead_available && !session.hardware && !session.vulkan &&
                            !session.async_audio.registered() &&
                            !(session.serialization_quirks & RETRO_SERIALIZATION_QUIRK_INCOMPLETE);
    const unsigned ahead = compatible && !session.fast_forward && core_has_run
                               ? session.frontend.runahead_frames : 0;
    session.runahead_active = ahead != 0;
    ++session.runahead_input_frame;
    struct RestoreExecution
    {
        Session& session;
        SDL_PixelFormat retained_format;
        ~RestoreExecution()
        {
            session.format = retained_format;
            session.speculative = false;
            session.output_enabled = true;
            session.runahead_active = false;
        }
    } restore_execution{session, session.format};
    try
    {
        session.runahead.run(session.save_core(), ahead, [&](bool output, bool speculative)
        {
            session.output_enabled = output;
            session.speculative = speculative;
            if (speculative && session.frame_time.callback)
            {
                const auto duration = static_cast<retro_usec_t>(std::llround(1000000.0 / session.frame_rate));
                session.frame_time.callback(session.frame_time.reference > 0 ? session.frame_time.reference : duration);
            }
            session.core.api.run();
            if (!speculative)
            {
                restore_execution.retained_format = session.format;
            }
        });
    }
    catch (const StateRecoveryError&)
    {
        // Never persist speculative battery RAM after a failed rollback.
        session.saves.battery_ready = false;
        session.frontend.stop_rumble();
        throw;
    }
    if (!session.runahead.error.empty())
    {
        session.frontend.notify(session.runahead.error);
        std::cerr << "Cabinet: " << session.runahead.error << '\n';
        session.frontend.runahead_frames = 0;
        session.runahead.error.clear();
    }
}

static int run_game_session(const RunnerOptions& inherited, const RunnerOptions& base,
                            std::optional<RunnerOptions>& restart)
{
    RunnerOptions options = inherited;
    const bool contentless = options.rom.empty();
    const std::string rom_path = contentless ? "" : fs::absolute(options.rom).string();
    const std::string core_path = fs::absolute(options.core).string();
    const std::string screenshot = options.screenshot;
    const unsigned frame_limit = options.frames;
    bool start_paused = options.paused;
    if (!contentless && !fs::is_regular_file(rom_path))
    {
        throw std::runtime_error("ROM file not found: " + rom_path);
    }
    if (!contentless && !fs::file_size(rom_path))
    {
        throw std::runtime_error("ROM is empty");
    }
    std::vector<char> content;

    if (!fs::is_regular_file(core_path))
    {
        throw std::runtime_error("Core file not found: " + core_path);
    }
    if (!options.load_state.empty() && !fs::is_regular_file(options.load_state))
    {
        throw std::runtime_error("Startup state file not found: " + options.load_state);
    }
    const auto input_system = options.system.empty() ? InputBindings::identify(rom_path, core_path) : options.system;
    const bool dual_screen = options.dual_screen && (input_system == "nds" || input_system == "3ds");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
#ifdef CABINET_ARMADA_DRM
    const bool armada_video = dual_screen && ArmadaDrm::configure_video();
#endif
    check(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | (options.no_audio ? 0 : SDL_INIT_AUDIO)));
    Session session;
    session.fast_forward = session.frontend.fast_forward = options.fast_forward;
    session.core_log_level = options.core_log_level;
    current = &session;
    const auto data_dir = application_data(options.data_dir);
    session.system_dir = (options.system_dir.empty() ? data_dir / "system" : fs::absolute(options.system_dir)).string();
    session.saves.directory = (options.save_dir.empty() ? data_dir / "saves" : fs::absolute(options.save_dir)).string();
    session.core.open(core_path.c_str());
    retro_system_info info{};
    session.core.api.get_system_info(&info);
    session.frontend.runahead_available = deterministic_core(core_path);
    if (!contentless && !info.need_fullpath)
    {
        content = read_content(rom_path);
    }
    session.dual_screen = dual_screen;
    const auto files = prepare_game_files(session.saves, options, data_dir, input_system, core_path, rom_path, info, content);
    const auto& game_config_dir = files.game_config_dir;
    apply_session_settings(session, options, game_config_dir, input_system, info.library_name);
    session.core.api.set_environment(environment);
    session.core.api.set_video_refresh(video);
    session.core.api.set_audio_sample(audio_sample);
    session.core.api.set_audio_sample_batch(audio_batch);
    session.core.api.set_input_poll(input_poll);
    session.core.api.set_input_state(input_state);
    session.core.initialize();
    if (contentless && !session.support_no_game)
    {
        throw std::runtime_error("Core does not support running without content. Supply --rom PATH.");
    }
    const auto state_dir = options.state_dir.empty() ? data_dir / "states" : fs::absolute(options.state_dir);
    const auto state_directory = state_dir / files.storage_system / files.save_name;
    if (options.state_file.empty())
    {
        session.saves.state_path = state_directory / files.core_name / (files.core_hash + ".state");
        save_storage::write(state_directory / "cabinet-game.txt", session.saves.metadata);
    }
    else
    {
        session.saves.state_path = fs::absolute(options.state_file);
    }
    retro_game_info game{rom_path.c_str(), info.need_fullpath ? nullptr : content.data(),
                         info.need_fullpath ? 0 : content.size(), nullptr};
    if (!session.core.load_game(contentless ? nullptr : &game))
    {
        throw std::runtime_error(
            session.callback_error.empty()
                ? (session.hardware_error.empty()
                       ? (contentless ? "Core could not start without content."
                                      : "Core could not load this ROM. Check the selected core and required firmware.")
                       : session.hardware_error)
                : session.callback_error);
    }
    if (session.dual_screen)
    {
        const auto layout_key = input_system == "nds" ? "melonds_screen_layout1" : "citra_layout_option";
        if (!session.core_options.defaults.contains(layout_key))
        {
            throw std::runtime_error("--dual-screen requires melonDS DS or Azahar.");
        }
    }
    session.core_options.boot_values = session.core_options.values;
    // serialize_size is not a passive capability query: some cores pause their
    // emulation thread or serialize internal state. Only call it when saving.
    session.frontend.can_save = true;
    if (input_system == "wii")
    {
        session.input.connect_wii_controller(session.frontend.wii_controller,
                                             session.core.api.set_controller_port_device);
    }
    else
    {
        session.core.api.set_controller_port_device(0, session.input.device());
    }
    // Flycast reads expansion-slot options only in its post-load update.
    // Notify it after selecting the controller so the default VMU is attached.
    if (session.core_options.resolution_key == "reicast_internal_resolution" ||
        session.core_options.resolution_key == "flycast_internal_resolution")
    {
        session.core_options.changed = true;
    }
    // Library saves use the persistent readable name; standalone saves retain the core filename.
    const auto battery_name = options.save_name.empty() ? files.core_name : files.save_name;
    session.saves.battery_path = files.save_directory / battery_name;
    session.saves.battery(session.save_core(), true);
    session.frontend.has_save = fs::exists(session.saves.state_path);
    if (!session.callback_error.empty())
    {
        throw std::runtime_error(session.callback_error);
    }
    // PPSSPP can initialize graphics while reporting AV information. Make the
    // negotiated context current first; its framebuffer dimensions arrive below.
    if (session.hw.context_type != RETRO_HW_CONTEXT_NONE && session.hw.context_type != RETRO_HW_CONTEXT_VULKAN)
    {
        session.hardware = std::make_unique<HardwareVideo>();
        session.hardware->create_context(session.hw);
    }
    retro_system_av_info av{};
    session.core.api.get_system_av_info(&av);
    if (!std::isfinite(av.timing.fps) || av.timing.fps <= 0 || !std::isfinite(av.timing.sample_rate) ||
        av.timing.sample_rate < 0 || av.timing.sample_rate > 384000)
    {
        throw std::runtime_error("Core returned invalid audio/video timing");
    }
    if (av.geometry.aspect_ratio > 0)
    {
        session.aspect = av.geometry.aspect_ratio;
    }
    session.frontend.game_title = options.title.empty() ? files.default_title : options.title;
    const auto& title = session.frontend.game_title;
    if (session.hw.context_type != RETRO_HW_CONTEXT_VULKAN)
    {
        session.vulkan.reset();
    }
#ifdef CABINET_ARMADA_DRM
    // A software core can explicitly select Vulkan for presentation too. Supply
    // that renderer's instance so display extensions are enabled before creation.
    if (armada_video && session.hw.context_type == RETRO_HW_CONTEXT_NONE)
    {
        const char* requested_renderer = SDL_getenv("SDL_RENDER_DRIVER");
        if (requested_renderer && std::string_view(requested_renderer) == "vulkan")
        {
            session.vulkan = std::make_unique<VulkanVideo>();
        }
    }
#endif
    session.window = SDL_CreateWindow(title.c_str(), options.width, options.height,
                                      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                          (session.vulkan ? SDL_WINDOW_VULKAN : 0) |
                                          (options.fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
    check(session.window != nullptr);
    SDL_SetWindowMinimumSize(session.window, 480, 420);
    session.renderer = session.vulkan ? session.vulkan->create_renderer(session.window, options.fullscreen
#ifdef CABINET_ARMADA_DRM
                                                                        ,
                                                                        armada_video
#endif
                                                                        )
                       : session.hardware ? session.hardware->create_renderer(session.window)
                                          : SDL_CreateRenderer(session.window, nullptr);
    check(session.renderer != nullptr);
    session.frontend.shaders_supported = ShaderChain::supported(session.renderer);
    if (!session.frontend.shaders_supported)
    {
        const bool active = session.frontend.shaders.active();
        for (auto& shader : session.frontend.shaders.effects)
        {
            shader.enabled = false;
        }
        if (active)
        {
            session.frontend.notify("Shaders disabled: this renderer does not support effects");
        }
    }
    RunnerPacing pacing(session.window, session.renderer, options.timing_stats);
    pacing.initialize_presenter(session.frontend.vsync, session.fast_forward);
    SDL_SetRenderDrawColor(session.renderer, 0, 0, 0, 255);
    if (session.vulkan)
    {
        // Fullscreen sizing and VSync can replace the bootstrap swapchain.
        // Give the final surface an opaque black frame before core graphics
        // initialization, and finish the clear before sharing the GPU queue.
        check(SDL_RenderClear(session.renderer));
        check(SDL_RenderPresent(session.renderer));
        session.vulkan->idle();
    }
    if (session.hw.context_type != RETRO_HW_CONTEXT_NONE)
    {
        if (session.hardware)
        {
            session.hardware->create_framebuffer(av.geometry.max_width, av.geometry.max_height);
        }
        session.hw_width = av.geometry.max_width;
        session.hw_height = av.geometry.max_height;
        session.hw_ready = true;
        session.hw.context_reset();
    }
    session.audio.set_sample_rate(av.timing.sample_rate);
    if (!options.no_audio && av.timing.sample_rate > 0)
    {
        if (session.audio.open(static_cast<unsigned>(std::lround(av.timing.sample_rate)), options.volume))
        {
            if (options.timing_stats)
            {
                SDL_AudioSpec device_spec{};
                int device_frames = 0;
                if (session.audio.device_format(device_spec, device_frames))
                {
                    std::cout << "[timing] audio_device_hz=" << device_spec.freq
                              << " audio_device_frames=" << device_frames
                              << " device_period_ms=" << (1000.0 * device_frames / device_spec.freq) << '\n';
                }
            }
        }
        else
        {
            std::cerr << "Audio unavailable: " << SDL_GetError() << '\n';
        }
    }
    if (session.dual_screen)
    {
#ifdef CABINET_ARMADA_DRM
        // Keep every Armada probe behind the explicit mode flag. Ordinary
        // launches never inspect the OS, connect to Wayland, or open evdev here.
        {
            std::unique_lock<VulkanVideo> lock;
            if (session.vulkan)
            {
                lock = std::unique_lock<VulkanVideo>(*session.vulkan);
            }
            session.leased_bottom = ArmadaDrm::try_create(session.renderer);
        }
        if (!session.leased_bottom)
#endif
        {
            session.secondary = std::make_unique<SecondaryWindow>();
            session.secondary->create(session.window, session.renderer, session.hardware.get(), session.vulkan.get(),
                                      title, options.width, options.width * 3 / 4);
        }
    }
    TextRenderer ui_text(session.renderer);

    std::cout << "Loaded " << info.library_name << ": " << av.geometry.base_width << 'x' << av.geometry.base_height
              << " at " << av.timing.fps << " fps\n"
              << std::flush;
    if (options.timing_stats)
    {
        std::cout << "[timing] video_api="
                  << (session.vulkan     ? "Vulkan"
                      : session.hardware ? "OpenGL"
                                         : "software")
                  << '\n';
    }
    bool running = true;
    bool force_redraw = true;
    bool secondary_dirty = session.secondary != nullptr, secondary_dimmed = false;
    int secondary_scaling = -1, secondary_aspect = -1;
    shaders::Stack secondary_shaders;
    unsigned secondary_frame = 0;
    std::optional<SDL_Event> pending_event;
    bool requested_fullscreen = options.fullscreen;
    unsigned ticks = 0;
    std::uint64_t next_battery_save = SDL_GetTicks() + 10000;
    std::uint64_t next_playtime_save = SDL_GetTicks() + 15000;
    bool was_paused = false;
    bool focus_pause_ready = false;
    bool core_has_run = false;
    bool cursor_hidden = false;
    bool cursor_hiding_enabled = session.frontend.hide_cursor;
    std::uint64_t cursor_visible_until = SDL_GetTicks() + 5000;
    bool menu_hint_pending = options.show_menu_hint && !options.home_shortcut && !options.stick_shortcut;
    bool load_pending = !options.load_state.empty();
    if (session.frontend.runahead_option_key.empty() &&
        (session.hardware || session.vulkan || session.async_audio.registered() ||
         (session.serialization_quirks & RETRO_SERIALIZATION_QUIRK_INCOMPLETE)))
    {
        session.frontend.runahead_available = false;
    }
    session.frame_rate = av.timing.fps;
    RunnerDiagnostics diagnostics(options.timing_stats);
    pacing.update({session.frame_rate, session.frontend.vsync, session.frontend.vrr, session.fast_forward,
                   static_cast<bool>(session.audio), session.async_audio.registered()});
    // Establish the startup controller state silently. SDL also queues ADDED
    // events for existing devices; discover() then treats those as unchanged.
    session.frontend.discover();
    while (running && (!frame_limit || ticks < frame_limit))
    {
#ifdef CABINET_ARMADA_DRM
        if (session.dual_screen && session.leased_bottom)
        {
            session.leased_bottom->pump();
        }
#endif
        SDL_Event event;
        while (pending_event || SDL_PollEvent(&event))
        {
            if (pending_event)
            {
                event = *pending_event;
                pending_event.reset();
            }
            // Hardware cores can emit video during context_reset, before the
            // first run. Arm focus pausing only after presenting while focused,
            // and ignore queued losses superseded by a subsequent focus gain.
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && (!focus_pause_ready || session.focused()))
            {
                continue;
            }
            const auto* event_window = SDL_GetWindowFromEvent(&event);
            const bool bottom_event = session.secondary && event_window == session.secondary->window;
            if (event_window && event_window != session.window && !bottom_event)
            {
                continue;
            }
            if (bottom_event && event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST)
            {
                secondary_dirty = true;
                if (event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN || event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN)
                {
                    continue;
                }
                if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                {
                    SDL_RaiseWindow(session.window);
                }
            }
            if ((event.type == SDL_EVENT_MOUSE_MOTION && (event_window == session.window || bottom_event) &&
                 (event.motion.xrel != 0 || event.motion.yrel != 0)) ||
                event.type == SDL_EVENT_WINDOW_MOUSE_ENTER || event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
            {
                cursor_visible_until = SDL_GetTicks() + 5000;
            }
            pacing.event(event);
            if (event.type == SDL_EVENT_WINDOW_EXPOSED || event.type == SDL_EVENT_WINDOW_RESIZED ||
                event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ||
                event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN || event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN ||
                event.type == SDL_EVENT_RENDER_TARGETS_RESET || event.type == SDL_EVENT_RENDER_DEVICE_RESET)
            {
                force_redraw = true;
            }
            session.frontend.event(event);
            if (event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN || event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN)
            {
                requested_fullscreen = session.frontend.fullscreen;
            }
        }
        pacing.update({session.frame_rate, session.frontend.vsync, session.frontend.vrr, session.fast_forward,
                       static_cast<bool>(session.audio), session.async_audio.registered()});
        session.frontend.update(session.focused(), SDL_GetTicks());
        const bool fast_forward_requested =
            (session.frontend.fast_forward || session.frontend.fast_forward_held) && !session.frontend.paused();
        if (session.fast_forward != fast_forward_requested)
        {
            set_fast_forward(session, fast_forward_requested);
            pacing.update({session.frame_rate, session.frontend.vsync, session.frontend.vrr, session.fast_forward,
                           static_cast<bool>(session.audio), session.async_audio.registered()});
        }
        const bool gameplay_ready = core_has_run && session.frames > 0;
        if (menu_hint_pending && gameplay_ready)
        {
            if (!session.frontend.home_shortcut && !session.frontend.stick_shortcut)
            {
                session.frontend.notify("Press Start + Select to open the in-game menu");
            }
            menu_hint_pending = false;
        }
        // Wait for execution and video: context_reset may emit a startup image
        // before the core can serialize, and retro_run may report duplicates.
        if (load_pending && gameplay_ready)
        {
            if (session.hardware)
            {
                session.hardware->make_current();
            }
            load_state(session, options.load_state);
            load_pending = false;
            // A startup checkpoint defines the beginning of a measured workload;
            // exclude boot and state deserialization from throughput statistics.
            diagnostics.reset_workload();
            if (options.timing_stats)
            {
                std::cout << "[timing] startup_state_loaded\n" << std::flush;
            }
            session.audio.reset(true);
            pacing.reset();
            session.frontend.resume_with_message("State loaded");
        }
        else if (start_paused && gameplay_ready)
        {
            session.frontend.open_menu();
            start_paused = false;
        }
        if (session.core_options.sync_pending(session.frontend))
        {
            session.frontend.notify("Restart game to apply internal resolution");
        }
        if (input_system == "wii")
        {
            const auto device = session.input.wii_device(session.frontend.wii_controller);
            if (session.input.device() != device)
            {
                if (session.hardware)
                {
                    session.hardware->make_current();
                }
                session.input.connect_wii_controller(session.frontend.wii_controller,
                                                     session.core.api.set_controller_port_device);
            }
        }
        const auto action = session.frontend.take_action();
        try
        {
            if (action == Action::CreateGameConfig)
            {
                auto inherited_bindings = session.frontend.bindings;
                auto inherited_hotkeys = session.frontend.hotkeys;
                session.frontend.bindings.copy_to(game_config_dir / "input");
                try
                {
                    session.frontend.hotkeys.copy_to(game_config_dir / "input");
                    settings_store::write_game(game_config_dir, settings_snapshot(session));
                }
                catch (...)
                {
                    session.frontend.bindings = std::move(inherited_bindings);
                    session.frontend.hotkeys = std::move(inherited_hotkeys);
                    throw;
                }
                session.frontend.game_config = true;
                session.frontend.notify("Game configuration created");
            }
            else if (action == Action::ResetGameConfig)
            {
                fs::remove(game_config_dir / "settings.conf");
                session.frontend.game_config = false;
                auto& ui = session.frontend;
                ui.wii_controller = base.wii_controller;
                ui.rumble_enabled = base.rumble;
                ui.stick_deadzone = base.stick_deadzone;
                ui.trigger_deadzone = base.trigger_deadzone;
                ui.fullscreen = base.fullscreen;
                ui.scaling = base.scaling;
                ui.shaders = base.shaders;
                ui.aspect_mode = base.aspect_mode;
                ui.volume = base.volume;
                ui.vsync = base.vsync;
                ui.vrr = base.vrr;
                ui.runahead_frames = base.runahead_frames;
                session.core_options.restore_values(base.core_options, ui);
                ui.bindings.configure(input_system, application_config(options.data_dir) / "input",
                                      base.wii_controller);
                ui.hotkeys.configure_hotkeys(input_system, application_config(options.data_dir) / "input");
                if (!session.core_options.resolution_key.empty())
                {
                    const auto found = base.core_options.find(session.core_options.resolution_key);
                    const auto value = found == base.core_options.end()
                                           ? session.core_options.defaults[session.core_options.resolution_key]
                                           : found->second;
                    const auto entry = std::find(ui.resolution_values.begin(), ui.resolution_values.end(), value);
                    ui.resolution_index = entry == ui.resolution_values.end()
                                              ? 0
                                              : static_cast<int>(entry - ui.resolution_values.begin());
                }
                ui.mark_core_settings_changed();
                ui.notify("Inherited settings restored");
            }
            else if (action == Action::Reset)
            {
                restart = options;
                restart->core_options = session.core_options.values;
                restart->vsync = session.frontend.vsync;
                restart->vrr = session.frontend.vrr;
                restart->runahead_frames = session.frontend.runahead_option_key.empty()
                                              ? session.frontend.runahead_frames : 0;
                restart->fullscreen = session.frontend.fullscreen;
                restart->volume = session.frontend.volume;
                restart->scaling = session.frontend.scaling;
                restart->shaders = session.frontend.shaders;
                restart->wii_controller = session.frontend.wii_controller;
                restart->rumble = session.frontend.rumble_enabled;
                restart->stick_deadzone = session.frontend.stick_deadzone;
                restart->trigger_deadzone = session.frontend.trigger_deadzone;
                restart->aspect_mode = session.frontend.aspect_mode;
                restart->stick_shortcut = session.frontend.stick_shortcut;
                restart->home_shortcut = session.frontend.home_shortcut;
                restart->swap_confirm_cancel = session.frontend.swap_confirm_cancel;
                restart->pause_on_focus_loss = session.frontend.pause_on_focus_loss;
                restart->hide_cursor = session.frontend.hide_cursor;
                restart->confirm_save_states = session.frontend.confirm_save_states;
                restart->show_stats = session.frontend.show_stats;
                restart->fast_forward = session.frontend.fast_forward;
                restart->show_menu_hint = false;
                restart->paused = false;
                restart->load_state.clear();
                running = false;
            }
            else
            {
                running = execute_action(session, action);
            }
        }
        catch (const std::exception& error)
        {
            std::cerr << "Cabinet: " << error.what() << '\n';
            session.frontend.notify(error.what());
        }
        if (!running)
        {
            break;
        }
        const bool paused = session.frontend.paused();
        session.async_audio.set_enabled(session.core.loaded() && session.audio && !paused && !session.fast_forward);
        diagnostics.update_overlay(session.frontend.show_stats, paused, session.frontend.stats_text);
        const bool playing = session.frames > 0 && !paused;
        if (playing || session.saves.playtime.active)
        {
            session.saves.playtime.update(SDL_GetTicks(), playing);
        }
        if (SDL_GetTicks() >= next_playtime_save)
        {
            try
            {
                session.saves.write_playtime();
            }
            catch (const std::exception& error)
            {
                session.frontend.notify(error.what());
            }
            next_playtime_save = SDL_GetTicks() + 15000;
        }
        if (paused != was_paused)
        {
            session.last_frame_time_ns = 0;
            session.input.reset(session.input_context());
            session.audio.reset(paused);
            pacing.reset();
            diagnostics.pause_changed();
            force_redraw = true;
            was_paused = paused;
        }
        // Submit menu requests once; window-manager changes arrive as events.
        if (requested_fullscreen != session.frontend.fullscreen)
        {
            requested_fullscreen = session.frontend.fullscreen;
            if (!SDL_SetWindowFullscreen(session.window, requested_fullscreen))
            {
                requested_fullscreen = (SDL_GetWindowFlags(session.window) & SDL_WINDOW_FULLSCREEN) != 0;
                session.frontend.fullscreen = requested_fullscreen;
                session.frontend.notify("Could not change fullscreen mode");
            }
        }
        if (cursor_hiding_enabled != session.frontend.hide_cursor)
        {
            cursor_hiding_enabled = session.frontend.hide_cursor;
            cursor_visible_until = SDL_GetTicks() + 5000;
        }
        const bool hide_cursor = session.frontend.hide_cursor && SDL_GetTicks() >= cursor_visible_until &&
                                 (SDL_GetMouseFocus() == session.window ||
                                  (session.secondary && SDL_GetMouseFocus() == session.secondary->window)) &&
                                 session.focused();
        if (hide_cursor != cursor_hidden)
        {
            check(hide_cursor ? SDL_HideCursor() : SDL_ShowCursor());
            cursor_hidden = hide_cursor;
        }
        session.audio.set_volume(session.frontend.volume);
        diagnostics.begin_iteration(paused, session.frames);
        if (!paused && session.async_audio.registered())
        {
            session.async_audio.pump(session.audio, SDL_GetTicksNS());
        }
        const auto iteration = pacing.begin_iteration(paused, session.audio, session.async_audio.next_wake());
        if (!paused && session.async_audio.registered())
        {
            session.async_audio.pump(session.audio, SDL_GetTicksNS());
        }
        unsigned executed = 0;
        if (!paused)
        {
            diagnostics.audio_queue(iteration.queued);
            if (iteration.audio_underflow)
            {
                diagnostics.audio_underflow();
            }
            bool audio_blocked = false;
            while (pacing.core_due(executed))
            {
                if (!session.async_audio.registered() && !session.audio.ready(session.frame_rate, session.fast_forward))
                {
                    diagnostics.audio_gated();
                    audio_blocked = true;
                    break;
                }
                if (session.hardware)
                {
                    session.hardware->make_current();
                }
                // Refresh all physical input after the pacing wait. Menu and
                // hotkey events remain queued for the next frontend update.
                SDL_PumpEvents();
                if (session.vulkan)
                {
                    session.vulkan->begin_frame();
                }
                session.frontend.sample_gameplay(session.focused());
                const double audio_before = session.audio.produced_ms();
                const auto core_started =
                    diagnostics.begin_core(session.fast_forward, pacing.core_deadline(), pacing.core_period());
                session.notify_frame_time(SDL_GetTicksNS());
                run_core_frame(session, core_has_run);
                core_has_run = true;
                session.async_audio.set_enabled(session.audio && !session.fast_forward);
                session.async_audio.pump(session.audio, SDL_GetTicksNS());
                diagnostics.end_core(core_started);
                ++executed;
                pacing.finish_core(session.audio, audio_before);
            }
            pacing.finish_core_work(session.audio, audio_blocked);
        }
        if (SDL_GetTicks() >= next_battery_save)
        {
            if (session.hardware)
            {
                session.hardware->make_current();
            }
            try
            {
                session.saves.battery(session.save_core(), false);
            }
            catch (const std::exception& error)
            {
                session.frontend.notify(error.what());
                std::cerr << "Cabinet: " << error.what() << '\n';
            }
            next_battery_save = SDL_GetTicks() + 10000;
        }
        if (!session.callback_error.empty())
        {
            throw std::runtime_error(session.callback_error);
        }
        if (!paused && !executed && session.async_audio.registered() && !force_redraw && !secondary_dirty &&
            !pacing.present_due(SDL_GetTicksNS()))
        {
            diagnostics.report();
            continue;
        }
        ++ticks;
        auto wait_for_menu = [&]
        {
            const auto now = SDL_GetTicks();
            std::uint64_t deadline =
                std::min({session.frontend.next_menu_update(now), next_battery_save, next_playtime_save,
                          std::uint64_t(pacing.menu_deadline(now, frame_limit != 0))});
#ifdef CABINET_ARMADA_DRM
            if (session.dual_screen && session.leased_bottom &&
                (secondary_dirty || session.leased_bottom->presentation_pending()))
            {
                deadline = std::min(deadline, now + 16);
            }
#endif
            if (session.frontend.hide_cursor && !cursor_hidden && cursor_visible_until > now)
            {
                deadline = std::min(deadline, cursor_visible_until);
            }
            SDL_Event wake;
            const int timeout =
                static_cast<int>(std::clamp<std::uint64_t>((deadline > now) ? deadline - now : 1, 1, 1000));
            if (SDL_WaitEventTimeout(&wake, timeout))
            {
                pending_event = wake;
            }
        };
#ifdef CABINET_ARMADA_DRM
        if (session.dual_screen && session.leased_bottom &&
            (secondary_frame != session.frames ||
             secondary_dimmed != (session.frontend.paused() && !session.frontend.video_preview()) ||
             secondary_scaling != session.frontend.scaling || secondary_aspect != session.frontend.aspect_mode ||
             secondary_shaders != session.frontend.shaders))
        {
            secondary_dirty = true;
        }
#endif
        if (paused && !force_redraw && !secondary_dirty && !session.frontend.needs_redraw(SDL_GetTicks()) &&
            !(frame_limit && ticks == frame_limit && !screenshot.empty()))
        {
            diagnostics.report();
            wait_for_menu();
            continue;
        }
        std::unique_lock<VulkanVideo> presentation_lock;
        if (session.vulkan)
        {
            presentation_lock = std::unique_lock<VulkanVideo>(*session.vulkan);
        }
        if (!session.frontend.shaders.active())
        {
            session.bottom_presentation.reset_shaders();
        }
        check(SDL_RenderClear(session.renderer));
        if (session.texture)
        {
            int w, h;
            check(SDL_GetRenderOutputSize(session.renderer, &w, &h));
            const auto dest = GamePresentation::viewport(w, h, session.aspect, session.frontend.aspect_mode,
                                                         session.width, session.height, session.frontend.scaling);
            if (session.dual_screen)
            {
                const auto view = dual_screen::window_view(input_system, 0, w, h);
                const auto source = dual_screen::pixels(view.source, session.width, session.height);
                session.draw_view(session.presentation, session.renderer, session.presentation_image(),
                                  GamePresentation::screen_viewport(session.presentation_image(), view), &source);
            }
            else
            {
                session.draw_view(session.presentation, session.renderer, session.presentation_image(), dest);
            }
        }
        int output_w = 0, output_h = 0;
        check(SDL_GetRenderOutputSize(session.renderer, &output_w, &output_h));
        session.frontend.draw(session.renderer, ui_text, output_w, output_h);
        if (!screenshot.empty() && frame_limit && ticks == frame_limit)
        {
            SDL_Surface* surface = SDL_RenderReadPixels(session.renderer, nullptr);
            check(surface != nullptr);
            bool saved = SDL_SaveBMP(surface, screenshot.c_str());
            SDL_DestroySurface(surface);
            check(saved);
        }
        const auto present_started = diagnostics.begin_present();
        const bool dim_bottom = session.frontend.paused() && !session.frontend.video_preview();
#ifdef CABINET_ARMADA_DRM
        if (session.dual_screen && session.leased_bottom &&
            (force_redraw || secondary_frame != session.frames || secondary_dimmed != dim_bottom ||
             secondary_scaling != session.frontend.scaling || secondary_aspect != session.frontend.aspect_mode ||
             secondary_shaders != session.frontend.shaders) &&
            session.leased_bottom->begin_draw())
        {
            session.leased_bottom->clear();
            if (session.texture)
            {
                const auto view = dual_screen::window_view(input_system, 1, session.leased_bottom->width(),
                                                           session.leased_bottom->height());
                const auto source = dual_screen::pixels(view.source, session.width, session.height);
                session.draw_view(session.bottom_presentation, session.renderer, session.presentation_image(),
                                  GamePresentation::screen_viewport(session.presentation_image(), view),
                                  &source, session.leased_bottom.get());
                if (dim_bottom)
                {
                    session.leased_bottom->dim(165);
                }
            }
            session.leased_bottom->present();
            secondary_frame = session.frames;
            secondary_dimmed = dim_bottom;
            secondary_scaling = session.frontend.scaling;
            secondary_aspect = session.frontend.aspect_mode;
            secondary_shaders = session.frontend.shaders;
        }
#endif
        if (session.secondary &&
            !(SDL_GetWindowFlags(session.secondary->window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) &&
            (secondary_dirty || secondary_frame != session.frames || secondary_dimmed != dim_bottom ||
             secondary_scaling != session.frontend.scaling || secondary_aspect != session.frontend.aspect_mode ||
             secondary_shaders != session.frontend.shaders))
        {
            auto& bottom = *session.secondary;
            bottom.begin_draw();
            check(SDL_SetRenderDrawColor(bottom.renderer, 0, 0, 0, 255));
            check(SDL_RenderClear(bottom.renderer));
            if (auto* image = bottom.texture(session.texture))
            {
                int w = 0, h = 0;
                check(SDL_GetRenderOutputSize(bottom.renderer, &w, &h));
                const auto view = dual_screen::window_view(input_system, 1, w, h);
                const auto source = dual_screen::pixels(view.source, session.width, session.height);
                auto frame = session.presentation_image();
                frame.texture = image;
                session.draw_view(session.bottom_presentation, bottom.renderer, frame,
                                  GamePresentation::screen_viewport(frame, view), &source);
                if (dim_bottom)
                {
                    check(SDL_SetRenderDrawBlendMode(bottom.renderer, SDL_BLENDMODE_BLEND));
                    check(SDL_SetRenderDrawColor(bottom.renderer, 0, 0, 0, 165));
                    check(SDL_RenderFillRect(bottom.renderer, nullptr));
                }
            }
            check(SDL_RenderPresent(bottom.renderer));
            bottom.end_draw();
            secondary_dirty = false;
            secondary_frame = session.frames;
            secondary_dimmed = dim_bottom;
            secondary_scaling = session.frontend.scaling;
            secondary_aspect = session.frontend.aspect_mode;
            secondary_shaders = session.frontend.shaders;
        }
#ifdef CABINET_ARMADA_DRM
        if (session.dual_screen && session.leased_bottom)
        {
            secondary_dirty = secondary_frame != session.frames || secondary_dimmed != dim_bottom ||
                              secondary_scaling != session.frontend.scaling ||
                              secondary_aspect != session.frontend.aspect_mode ||
                              secondary_shaders != session.frontend.shaders;
        }
        else
#endif
        {
            secondary_dirty = false;
        }
        const auto swap_started = pacing.begin_swap();
        check(SDL_RenderPresent(session.renderer));
#ifdef CABINET_ARMADA_DRM
        if (session.dual_screen && session.leased_bottom)
        {
            session.leased_bottom->after_main_present();
        }
#endif
        force_redraw = false;
        if (presentation_lock.owns_lock())
        {
            presentation_lock.unlock();
        }
        if (core_has_run && session.frames > 0 && session.focused())
        {
            focus_pause_ready = true;
        }
        const auto presented = SDL_GetTicksNS();
        pacing.observe_present(presented, swap_started, paused);
        diagnostics.end_present(presented, present_started, executed, paused, session.frames);
        pacing.finish_present(presented, paused, iteration.started, session.async_audio.next_wake());
        if (paused)
        {
            wait_for_menu();
        }
    }
    diagnostics.report(true);
    if (load_pending)
    {
        throw std::runtime_error("Game ended before the startup state could be loaded");
    }
    if (frame_limit && !session.frames)
    {
        throw std::runtime_error("Core produced no video frames");
    }
    if (session.hardware)
    {
        session.hardware->make_current();
    }
    session.saves.battery(session.save_core(), false);
    const auto config = application_config(options.data_dir);
    const auto snapshot = settings_snapshot(session);
    settings_store::write_global(config, snapshot);
    if (session.frontend.game_config)
    {
        settings_store::write_game(game_config_dir, snapshot);
    }
    else
    {
        settings_store::write_system(config, input_system, snapshot);
    }
    std::cout << "Presented " << session.frames << " video frames\n";
    return 0;
}

int run_game(const RunnerOptions& options)
{
    RunnerOptions launch = options;
    for (;;)
    {
        std::optional<RunnerOptions> restart;
        const int result = run_game_session(launch, options, restart);
        if (!restart)
        {
            return result;
        }
        SDL_Quit();
        launch = std::move(*restart);
    }
}
