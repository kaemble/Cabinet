#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

// Some compositors/drivers accept a swap interval but hold each swap longer
// than the display mode implies. Fall back to interval 1 plus timer pacing.
struct SwapIntervalProbe
{
    double blocked = 0;
    unsigned samples = 0;
    bool too_slow(double wait, double target_period)
    {
        blocked += wait;
        if (++samples < 8)
        {
            return false;
        }
        const bool slow = blocked / samples > target_period * 1.25;
        blocked = 0;
        samples = 0;
        return slow;
    }
};

// Absolute clocks with bounded catch-up after scheduling delays.
struct FramePacing
{
    double next_core = 0, next_present = 0;
    double core_period = 1e9 / 60, present_period = 1e9 / 60;
    double speed = 1, ratio = 1;
    double audio_end = 0;
    unsigned core_budget = 2;
    bool content_clock = true, core_driven = false;
    // Like RetroArch's automatic swap interval: only use a whole refresh
    // multiple when it matches content within the audio rate-control tolerance.
    static int preferred_swap_interval(double fps, double refresh, bool vrr)
    {
        if (vrr || !std::isfinite(fps) || !std::isfinite(refresh) || fps <= 0 || refresh <= 0)
        {
            return 1;
        }
        const double multiple = std::round(refresh / fps);
        if (multiple < 1 || multiple > std::numeric_limits<int>::max())
        {
            return 1;
        }
        return (std::abs(refresh / multiple / fps - 1) <= .005) ? static_cast<int>(multiple) : 1;
    }
    void configure(double fps, double refresh, bool vsync, bool vrr, bool audio, double now)
    {
        double effective = fps;
        if (vsync && !vrr && audio && refresh > 0)
        {
            const double divisor = std::max(1.0, std::round(refresh / fps));
            const double matched = refresh / divisor;
            if (std::abs(matched / fps - 1) <= .005)
            {
                effective = matched;
            }
        }
        // At or above content refresh, present after a due core run instead of
        // ticking a second timer which can wake just before the core deadline.
        core_driven = vsync && !vrr && refresh > 0 && effective <= refresh;
        content_clock = !vsync || vrr || refresh <= 0 || core_driven;
        core_budget =
            1 +
            ((vsync && refresh > 0 && !vrr) ? static_cast<unsigned>(std::max(1.0, std::ceil(effective / refresh))) : 1);
        speed = effective / fps;
        core_period = 1e9 / effective;
        // A faster display can keep scanning out the last image between content
        // frames. Avoid redrawing once per refresh just to keep the loop paced.
        // Slower fixed-refresh displays still bound presentation, not emulation.
        const double present_hz = (vsync && !vrr && refresh > 0) ? std::min(effective, refresh) : effective;
        present_period = 1e9 / present_hz;
        reset(now);
    }
    void reset(double now)
    {
        next_core = next_present = audio_end = now;
        ratio = speed;
    }
    void finish_present(double presented, bool paused, double iteration_start)
    {
        if (paused)
        {
            next_present = presented + present_period;
        }
        else if (core_driven)
        {
            next_present = next_core;
        }
        else if (content_clock)
        {
            next_present = std::min(next_core, presented + present_period);
        }
        else
        {
            advance(next_present, present_period, iteration_start);
        }
    }
    bool due(double now) const
    {
        return now >= next_core;
    }
    static void advance(double& deadline, double period, double now)
    {
        deadline += period;
        if (deadline <= now)
        {
            deadline = now + period;
        }
    }
    void finish_core_work(double now, bool audio_blocked)
    {
        if (audio_blocked)
        {
            next_core = now + core_period;
        }
        // Keep ordinary lateness as emulation debt; dropping even a fraction of
        // a frame here repeatedly starves audio. Discard only a long backlog.
        else if (now - next_core > core_period * 4)
        {
            next_core = now - core_period;
        }
    }
    void defer_for_audio(double now, double produced_ms, double target_ms)
    {
        // A run can advance several video intervals (e.g. Flycast rendering a
        // 30 fps game while advertising 60 Hz). Spread that work over the time
        // its audio represents, rather than filling and draining a high-water
        // buffer in bursts. Ordinary one-frame batches retain the video clock.
        // Advance a media clock rather than timing each run from the device's
        // queue depth, which drops in whole device-buffer chunks.
        if (produced_ms <= 0)
        {
            return;
        }
        audio_end = std::max(audio_end + produced_ms * 1e6 / speed, now);
        next_core = std::max(next_core, audio_end - target_ms * 1e6);
    }
    double audio_ratio(double queued_ms, double target_ms, double elapsed_seconds)
    {
        const double correction = .005 * std::clamp((queued_ms - target_ms) / target_ms, -1.0, 1.0);
        const double alpha = 1 - std::exp(-std::clamp(elapsed_seconds, 0.0, .1) / .25);
        ratio += (speed * (1 + correction) - ratio) * alpha;
        return ratio;
    }
};
