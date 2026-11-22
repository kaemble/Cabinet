#pragma once

#include "performance_stats.h"
#include <cstdint>
#include <optional>
#include <string>

// Observes execution and presentation without participating in frame pacing.
// Console and overlay measurements keep independent reporting windows.
class RunnerDiagnostics
{
public:
    explicit RunnerDiagnostics(bool enable_timing);
    bool timing_enabled() const
    {
        return timing.has_value();
    }
    bool active() const
    {
        return timing.has_value() || overlay.has_value();
    }

    void update_overlay(bool enabled, bool paused, std::string& text);
    void reset_workload();
    void pause_changed();
    void begin_iteration(bool paused, unsigned frames);
    void audio_queue(double milliseconds);
    void audio_underflow();
    void audio_gated();
    std::uint64_t begin_core(bool fast_forward, double deadline, double period);
    void end_core(std::uint64_t started);
    std::uint64_t begin_present() const;
    void end_present(std::uint64_t now, std::uint64_t started, unsigned executed, bool paused, unsigned frames);
    void report(bool final_report = false);

private:
    struct TimingWindow
    {
        unsigned presents = 0, runs = 0, repeats = 0, late = 0, underflows = 0, gated = 0, intervals = 0, paused = 0;
        unsigned idle_presents = 0, catchup_presents = 0, videos = 0;
        double core_sum = 0, core_max = 0, present_sum = 0, present_max = 0;
        double interval_sum = 0, interval_max = 0, queue_min = 1e9, queue_max = 0, interval_squared_sum = 0;
        std::uint64_t last_present = 0, report_at = 0;
    };

    std::optional<TimingWindow> timing;
    std::optional<PerformanceStats> overlay;
    unsigned before_frames = 0;
};
