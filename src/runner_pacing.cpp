#include "runner_pacing.h"
#include <cstdint>
#include "audio_output.h"
#include <algorithm>
#include <iostream>

RunnerPacing::RunnerPacing(SDL_Window* window, SDL_Renderer* renderer, bool log_timing)
    : window(window), renderer(renderer), log_timing(log_timing)
{
}

void RunnerPacing::initialize_presenter(bool vsync, bool fast_forward)
{
    // The bootstrap black frame precedes core graphics initialization and audio
    // setup. Apply its swap policy now; configure the content clock afterwards.
    const bool enabled = vsync && !fast_forward && SDL_SetRenderVSync(renderer, 1);
    if (!enabled)
    {
        SDL_SetRenderVSync(renderer, 0);
    }
}

void RunnerPacing::event(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
        event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN || event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN)
    {
        display_changed = true;
    }
}

void RunnerPacing::update(const Settings& requested)
{
    if (configured && !display_changed && requested == applied && SDL_GetTicks() < display_check)
    {
        return;
    }
    const auto* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
    const double hertz = mode ? ((mode->refresh_rate_denominator > 0 && mode->refresh_rate_numerator > 0)
                                     ? double(mode->refresh_rate_numerator) / mode->refresh_rate_denominator
                                     : mode->refresh_rate)
                              : 0;
    if (!configured || display_changed || hertz != refresh || requested != applied)
    {
        if (!configured)
        {
            audio_check = SDL_GetTicksNS();
        }
        applied = requested;
        swap_interval = 0;
        swap_probe = {};
        if (applied.vsync && !applied.fast_forward && (hertz > 0 || applied.vrr))
        {
            // Long swap intervals would block callback audio between video frames.
            const int preferred = applied.asynchronous_audio
                                      ? 1 : FramePacing::preferred_swap_interval(applied.frame_rate, hertz, applied.vrr);
            if (SDL_SetRenderVSync(renderer, preferred))
            {
                swap_interval = preferred;
            }
            else if (preferred != 1 && SDL_SetRenderVSync(renderer, 1))
            {
                swap_interval = 1;
            }
        }
        const bool vsync = swap_interval != 0;
        if (!vsync)
        {
            SDL_SetRenderVSync(renderer, 0);
        }
        refresh = hertz;
        audio_target = applied.asynchronous_audio ? 60.0 : std::max(60.0, 3000.0 / applied.frame_rate);
        clock.configure(applied.frame_rate, refresh, vsync, applied.vrr,
                        applied.audio && !applied.asynchronous_audio, SDL_GetTicksNS());
        if (log_timing)
        {
            std::cout << "[timing] content_hz=" << applied.frame_rate << " display_hz=" << refresh << " vsync=" << vsync
                      << " vrr_mode=" << applied.vrr << " fast_forward=" << applied.fast_forward
                      << " speed=" << clock.speed << " swap_interval=" << swap_interval
                      << " present_hz=" << 1e9 / clock.present_period << " core_paced=" << clock.core_driven << '\n';
        }
        configured = true;
        display_changed = false;
    }
    display_check = SDL_GetTicks() + 1000;
}

void RunnerPacing::reset()
{
    clock.reset(SDL_GetTicksNS());
}

RunnerPacing::Iteration RunnerPacing::begin_iteration(bool paused, AudioOutput& audio, std::uint64_t audio_wake)
{
    const double now = SDL_GetTicksNS();
    const double wake = std::min({clock.next_core, clock.next_present, static_cast<double>(audio_wake)});
    if (!paused && !applied.fast_forward &&
        (clock.content_clock || audio_wake != std::numeric_limits<std::uint64_t>::max()) && wake > now)
    {
        SDL_DelayPrecise(static_cast<std::uint64_t>(wake - now));
    }
    Iteration result{SDL_GetTicksNS()};
    if (!paused)
    {
        result.queued = (!applied.fast_forward || log_timing) ? audio.queue_ms() : 0;
        if (audio && !applied.fast_forward)
        {
            result.audio_underflow = log_timing && audio.started() && result.queued < 1;
            audio.set_frequency_ratio(applied.asynchronous_audio
                                          ? 1.0 : clock.audio_ratio(result.queued, audio_target,
                                                                   (result.started - audio_check) / 1e9));
        }
        audio_check = result.started;
    }
    return result;
}

bool RunnerPacing::core_due(unsigned executed) const
{
    return (applied.fast_forward || clock.due(SDL_GetTicksNS())) &&
           executed < (applied.fast_forward ? 1 : clock.core_budget);
}

void RunnerPacing::finish_core(AudioOutput& audio, double produced_before)
{
    clock.next_core += clock.core_period;
    if (audio && !applied.fast_forward && !applied.asynchronous_audio)
    {
        clock.defer_for_audio(SDL_GetTicksNS(), audio.produced_ms() - produced_before, audio_target);
    }
}

void RunnerPacing::finish_core_work(AudioOutput& audio, bool audio_blocked)
{
    // Bound catch-up on slow displays without discarding ordinary emulation
    // debt. Multi-interval audio retains its media clock across core calls.
    if (!applied.fast_forward)
    {
        clock.finish_core_work(SDL_GetTicksNS(), audio_blocked);
    }
    if (!applied.fast_forward && audio.prime(audio_target) && !applied.asynchronous_audio)
    {
        clock.audio_end = SDL_GetTicksNS() + audio.queue_ms() * 1e6 / clock.speed;
    }
}

std::uint64_t RunnerPacing::begin_swap() const
{
    return (swap_interval > 1) ? SDL_GetTicksNS() : 0;
}

void RunnerPacing::observe_present(std::uint64_t presented, std::uint64_t swap_started, bool paused)
{
    if (swap_interval <= 1 || paused || !swap_probe.too_slow(presented - swap_started, clock.present_period))
    {
        return;
    }
    const int previous_interval = swap_interval;
    const bool vsync = SDL_SetRenderVSync(renderer, 1);
    swap_interval = vsync ? 1 : 0;
    if (!vsync)
    {
        SDL_SetRenderVSync(renderer, 0);
        clock.configure(applied.frame_rate, refresh, false, applied.vrr,
                        applied.audio && !applied.asynchronous_audio, presented);
    }
    if (log_timing)
    {
        std::cout << "[timing] swap_interval_fallback=" << previous_interval << " swap_interval=" << swap_interval
                  << " present_hz=" << 1e9 / clock.present_period << '\n';
    }
}

void RunnerPacing::finish_present(std::uint64_t presented, bool paused, std::uint64_t iteration_started,
                                 std::uint64_t audio_wake)
{
    // Count the swap's blocking time towards the deadline; sleeping for a full
    // frame here as well would halve the rate on a blocking VSync backend.
    clock.finish_present(presented, paused, iteration_started);
    const double wake = std::min(clock.next_present, static_cast<double>(audio_wake));
    if (!paused && !applied.fast_forward && wake > presented)
    {
        SDL_DelayPrecise(static_cast<std::uint64_t>(wake - presented));
    }
}

std::uint64_t RunnerPacing::menu_deadline(std::uint64_t now, bool frame_limited) const
{
    // --frames counts paused loop iterations too. Bounded captures/tests must
    // advance at the presentation cadence even when the menu has no changes.
    if (frame_limited)
    {
        return std::min(display_check, now + std::uint64_t(std::max(1.0, clock.present_period / 1e6)));
    }
    return display_check;
}
