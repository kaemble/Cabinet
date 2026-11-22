#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>

// New core video rate, presentation intervals, and time outside retro_run.
// The runner constructs this collector only while the overlay is enabled.
class PerformanceStats
{
public:
    struct Sample
    {
        double fps, present_ms, idle_percent;
    };
    explicit PerformanceStats(std::uint64_t now) : started_(now)
    {
    }
    void core_time(std::uint64_t duration)
    {
        core_ns_ += duration;
    }
    void present(std::uint64_t now, unsigned video_frames)
    {
        if (last_present_)
        {
            interval_ns_ += now - last_present_;
            ++intervals_;
        }
        last_present_ = now;
        video_frames_ += video_frames;
    }
    std::optional<Sample> sample(std::uint64_t now)
    {
        const auto elapsed = now - started_;
        if (elapsed < 1000000000ull)
        {
            return {};
        }
        Sample value{video_frames_ * 1e9 / elapsed, intervals_ ? interval_ns_ / double(intervals_) / 1e6 : 0,
                     std::clamp(100.0 * (1.0 - core_ns_ / double(elapsed)), 0.0, 100.0)};
        started_ = now;
        core_ns_ = interval_ns_ = 0;
        video_frames_ = intervals_ = 0;
        return value;
    }

private:
    std::uint64_t started_, last_present_ = 0, core_ns_ = 0, interval_ns_ = 0;
    unsigned video_frames_ = 0, intervals_ = 0;
};
