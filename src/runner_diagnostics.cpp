#include "runner_diagnostics.h"
#include <cstdint>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

RunnerDiagnostics::RunnerDiagnostics(bool enable_timing)
{
    if (enable_timing)
    {
        timing.emplace();
        timing->report_at = SDL_GetTicksNS();
    }
}

void RunnerDiagnostics::update_overlay(bool enabled, bool paused, std::string& text)
{
    if (!enabled)
    {
        overlay.reset();
        text.clear();
    }
    else if (paused)
    {
        overlay.reset();
        text = "FPS: 0.0\nPresent: -- ms\nCore idle: 100.0%";
    }
    else if (!overlay)
    {
        overlay.emplace(SDL_GetTicksNS());
        text = "FPS: --\nPresent: -- ms\nCore idle: --%";
    }
    else if (const auto sample = overlay->sample(SDL_GetTicksNS()))
    {
        std::ostringstream formatted;
        formatted << std::fixed << std::setprecision(1) << "FPS: " << sample->fps
                  << "\nPresent: " << std::setprecision(2) << sample->present_ms
                  << " ms\nCore idle: " << std::setprecision(1) << sample->idle_percent << '%';
        text = formatted.str();
    }
}

void RunnerDiagnostics::reset_workload()
{
    if (timing)
    {
        *timing = {};
        timing->report_at = SDL_GetTicksNS();
    }
}

void RunnerDiagnostics::pause_changed()
{
    if (timing)
    {
        timing->last_present = 0;
    }
}

void RunnerDiagnostics::begin_iteration(bool paused, unsigned frames)
{
    if (active())
    {
        before_frames = frames;
    }
    if (timing && paused)
    {
        ++timing->paused;
    }
}

void RunnerDiagnostics::audio_queue(double milliseconds)
{
    if (!timing)
    {
        return;
    }
    timing->queue_min = std::min(timing->queue_min, milliseconds);
    timing->queue_max = std::max(timing->queue_max, milliseconds);
}

void RunnerDiagnostics::audio_underflow()
{
    if (timing)
    {
        ++timing->underflows;
    }
}

void RunnerDiagnostics::audio_gated()
{
    if (timing)
    {
        ++timing->gated;
    }
}

std::uint64_t RunnerDiagnostics::begin_core(bool fast_forward, double deadline, double period)
{
    if (!active())
    {
        return 0;
    }
    const auto now = SDL_GetTicksNS();
    if (timing && !fast_forward && now - deadline > period * .5)
    {
        ++timing->late;
    }
    return now;
}

void RunnerDiagnostics::end_core(std::uint64_t started)
{
    if (!active())
    {
        return;
    }
    const auto duration = SDL_GetTicksNS() - started;
    if (overlay)
    {
        overlay->core_time(duration);
    }
    if (timing)
    {
        const double milliseconds = duration / 1e6;
        ++timing->runs;
        timing->core_sum += milliseconds;
        timing->core_max = std::max(timing->core_max, milliseconds);
    }
}

std::uint64_t RunnerDiagnostics::begin_present() const
{
    return timing ? SDL_GetTicksNS() : 0;
}

void RunnerDiagnostics::end_present(std::uint64_t now, std::uint64_t started, unsigned executed, bool paused,
                                    unsigned frames)
{
    if (!active())
    {
        return;
    }
    const auto video_frames = frames - before_frames;
    if (overlay)
    {
        overlay->present(now, video_frames);
    }
    if (!timing)
    {
        return;
    }
    auto& window = *timing;
    const double blocking = (now - started) / 1e6;
    window.present_sum += blocking;
    window.present_max = std::max(window.present_max, blocking);
    if (window.last_present)
    {
        const double interval = (now - window.last_present) / 1e6;
        ++window.intervals;
        window.interval_sum += interval;
        window.interval_max = std::max(window.interval_max, interval);
        window.interval_squared_sum += interval * interval;
    }
    window.last_present = now;
    ++window.presents;
    if (!paused)
    {
        if (!executed)
        {
            ++window.idle_presents;
        }
        if (executed > 1)
        {
            ++window.catchup_presents;
        }
    }
    window.videos += video_frames;
    if (!video_frames)
    {
        ++window.repeats;
    }
    report();
}

void RunnerDiagnostics::report(bool final_report)
{
    if (!timing)
    {
        return;
    }
    const auto now = SDL_GetTicksNS();
    auto& window = *timing;
    if (!final_report && now - window.report_at < 5000000000ull)
    {
        return;
    }
    const double seconds = (now - window.report_at) / 1e9;
    if (window.presents)
    {
        std::cout << "[timing] elapsed_s=" << seconds << " runs_per_s=" << ((seconds > 0) ? window.runs / seconds : 0)
                  << " video_fps=" << ((seconds > 0) ? window.videos / seconds : 0) << " presents=" << window.presents
                  << " runs=" << window.runs << " repeats=" << window.repeats
                  << " idle_presents=" << window.idle_presents << " catchup_presents=" << window.catchup_presents
                  << " paused=" << window.paused << " late=" << window.late << " audio_gates=" << window.gated
                  << " empty_queue_samples=" << window.underflows
                  << " core_ms(avg/max)=" << (window.runs ? window.core_sum / window.runs : 0) << '/' << window.core_max
                  << " present_ms(avg/max)=" << window.present_sum / window.presents << '/' << window.present_max
                  << " interval_ms(avg/max)=" << (window.intervals ? window.interval_sum / window.intervals : 0) << '/'
                  << window.interval_max << " interval_jitter_ms="
                  << (window.intervals
                          ? std::sqrt(std::max(0.0, window.interval_squared_sum / window.intervals -
                                                        std::pow(window.interval_sum / window.intervals, 2)))
                          : 0)
                  << " stream_queue_ms(min/max)=" << ((window.queue_min == 1e9) ? 0 : window.queue_min) << '/'
                  << window.queue_max << '\n'
                  << std::flush;
    }
    window = {};
    window.report_at = now;
}
