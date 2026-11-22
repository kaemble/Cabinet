#pragma once

#include "frame_pacing.h"
#include <SDL3/SDL.h>
#include <cstdint>

class AudioOutput;

// Owns the runner's pacing policy and its SDL/audio integration. FramePacing
// remains the deterministic clock model; callers only report loop milestones.
class RunnerPacing
{
public:
    struct Settings
    {
        double frame_rate = 60;
        bool vsync = true, vrr = false, fast_forward = false, audio = false;
        bool operator==(const Settings&) const = default;
    };

    struct Iteration
    {
        std::uint64_t started;
        double queued = 0;
        bool audio_underflow = false;
    };

    RunnerPacing(SDL_Window* window, SDL_Renderer* renderer, bool log_timing);
    void initialize_presenter(bool vsync, bool fast_forward);
    void event(const SDL_Event& event);
    void update(const Settings& requested);
    void reset();

    Iteration begin_iteration(bool paused, AudioOutput& audio);
    bool core_due(unsigned executed) const;
    void finish_core(AudioOutput& audio, double produced_before);
    void finish_core_work(AudioOutput& audio, bool audio_blocked);
    std::uint64_t begin_swap() const;
    void observe_present(std::uint64_t presented, std::uint64_t swap_started, bool paused);
    void finish_present(std::uint64_t presented, bool paused, std::uint64_t iteration_started);
    std::uint64_t menu_deadline(std::uint64_t now, bool frame_limited) const;

    double core_deadline() const
    {
        return clock.next_core;
    }
    double core_period() const
    {
        return clock.core_period;
    }

private:
    SDL_Window* window;
    SDL_Renderer* renderer;
    bool log_timing;
    Settings applied;
    FramePacing clock;
    SwapIntervalProbe swap_probe;
    bool configured = false, display_changed = true;
    double refresh = 0, audio_target = 60;
    int swap_interval = 0;
    std::uint64_t display_check = 0, audio_check = 0;
};
