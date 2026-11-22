#include "frame_pacing.h"
#include "performance_stats.h"
#include <iostream>
#include <stdexcept>
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        {
            const double period = 1e9 / 60;
            SwapIntervalProbe valid, nonblocking, slow;
            for (int i = 0; i < 24; ++i) {
                require(!valid.too_slow(period * .95, period), "A matching swap interval triggered fallback");
                require(!nonblocking.too_slow(100000, period), "A nonblocking presenter rejected timer pacing");
                require(slow.too_slow(period * 2, period) == (i % 8 == 7),
                        "A driver accepting an incorrectly paced interval was not detected");
            }
        }
        {
            PerformanceStats stats(1000);
            require(!stats.sample(500000000), "Overlay sampled before its reporting interval");
            for (uint64_t frame = 1; frame <= 60; ++frame) {
                stats.core_time(5000000);
                stats.present(1000 + frame * 1000000000 / 60, frame % 2 ? 1 : 0);
            }
            const auto sample = stats.sample(1000001000);
            require(sample && std::abs(sample->fps - 30) < .001 &&
                std::abs(sample->present_ms - 1000.0 / 60) < .001 &&
                std::abs(sample->idle_percent - 70) < .001, "Overlay rate, interval, or core idle calculation incorrect");
            const auto idle = stats.sample(2000001000);
            require(idle && idle->fps == 0 && idle->idle_percent == 100,
                "Overlay retained work from its previous sampling window");
        }
        {
            FramePacing clock;
            require(FramePacing::preferred_swap_interval(60, 180, false) == 3 &&
                    FramePacing::preferred_swap_interval(60, 120, false) == 2 &&
                    FramePacing::preferred_swap_interval(60, 179.94, false) == 3 &&
                    FramePacing::preferred_swap_interval(59.7275, 60, false) == 1,
                    "Automatic swap interval did not match whole refresh multiples");
            require(FramePacing::preferred_swap_interval(60, 144, false) == 1 &&
                    FramePacing::preferred_swap_interval(60, 50, false) == 1 &&
                    FramePacing::preferred_swap_interval(60, 180, true) == 1 &&
                    FramePacing::preferred_swap_interval(60, 0, false) == 1,
                    "Unsupported refresh ratios, VRR or unknown displays requested multiple-refresh swaps");
            clock.configure(60, 144, true, false, true, 0);
            int runs = 0;
            for (int tick = 0; tick < 1440; ++tick) {
                const double now = tick * (1e9 / 144);
                if (clock.due(now)) { ++runs; FramePacing::advance(clock.next_core, clock.core_period, now); }
            }
            require(runs >= 599 && runs <= 601, "144 Hz display changed emulation speed");
            clock.configure(60, 50, true, false, true, 0);
            runs = 0;
            for (int tick = 0; tick < 500; ++tick) {
                const double now = tick * (1e9 / 50);
                for (unsigned i = 0; i < clock.core_budget && clock.due(now); ++i) {
                    ++runs; clock.next_core += clock.core_period;
                }
            }
            require(runs >= 598 && runs <= 600, "50 Hz display slowed 60 Hz content");
            // A slightly early display wake followed by ordinary UI work may
            // cross the core deadline. That unexecuted frame must remain due.
            for (bool vrr : {false, true}) {
                clock.configure(60, 179.94, true, vrr, true, 0);
                int frames = 0;
                const double tick_period = vrr ? clock.core_period : clock.present_period;
                const int ticks = static_cast<int>(std::ceil(10e9 / tick_period));
                for (int tick = 0; tick < ticks; ++tick) {
                    const double now = tick * tick_period - 100000;
                    for (unsigned i = 0; i < clock.core_budget && clock.due(now); ++i) {
                        ++frames; clock.next_core += clock.core_period;
                    }
                    clock.finish_core_work(now + 200000, false);
                }
                require(frames >= 598 && frames <= 601, "Small clock jitter discarded emulation frames");
            }
            clock.configure(60, 179.94, true, true, true, 0);
            clock.finish_core_work(100000, false);
            require(clock.due(100000), "An unexecuted deadline was postponed");
            clock.finish_core_work(1e9, false);
            require(clock.next_core >= 1e9 - clock.core_period, "Long stalls retained unlimited debt");
            // A core advertising 60 Hz can return 33 ms of emulation/audio per
            // call. It must settle at 30 evenly spaced runs, not alternate
            // between fast bursts and waiting for a large queue to drain.
            for (double audio_ms : {1000.0 / 60, 1000.0 / 30}) {
                clock.configure(60, 180, false, false, true, 0);
                clock.audio_end = 60e6;
                double queued = 60, previous = 0, shortest = 1e9, longest = 0;
                int runs = 0;
                for (double now = 0; now < 10e9; now += 100000) {
                    queued = std::max(0.0, queued - .1);
                    if (!clock.due(now)) continue;
                    queued += audio_ms;
                    if (runs > 5) { shortest = std::min(shortest, now - previous); longest = std::max(longest, now - previous); }
                    previous = now; ++runs;
                    clock.next_core += clock.core_period;
                    clock.defer_for_audio(now, audio_ms, 60);
                    clock.finish_core_work(now, false);
                    require(queued < 100, "Multi-interval runs filled the high-water buffer");
                }
                require(std::abs(runs - 10000 / audio_ms) < 3, "Audio pacing changed emulation speed");
                require(longest - shortest < 200000, "Audio pacing alternated bursts with long waits");
            }
            // Audio may arrive once every few runs. Its aggregate duration must
            // not slow down a core that still advances one video frame per run.
            clock.configure(60, 180, false, false, true, 0);
            for (int i = 0; i < 600; ++i) {
                const double now = i * clock.core_period;
                require(clock.due(now + 1), "Batched audio delayed ordinary core frames");
                clock.next_core += clock.core_period;
                clock.defer_for_audio(now, i % 6 == 5 ? 100 : 0, 60);
            }
            clock.configure(50, 60, true, false, true, 0);
            require(clock.speed == 1, "PAL content was sped up to 60 Hz");
            clock.configure(59.7275, 60, true, false, true, 0);
            require(clock.speed > 1 && clock.speed < 1.005, "Close refresh-rate match not applied");
            clock.configure(59.7275, 144, true, true, true, 0);
            require(clock.speed == 1 && clock.core_period == clock.present_period, "VRR did not retain content rate");
            for (int i = 0; i < 300; ++i) clock.audio_ratio(100, 30, 1.0 / 60);
            require(clock.ratio > 1 && clock.ratio <= 1.005001, "Audio correction exceeded its bound or moved in the wrong direction");
            for (int i = 0; i < 600; ++i) clock.audio_ratio(0, 30, 1.0 / 60);
            require(clock.ratio < 1 && clock.ratio >= .994999, "Low audio occupancy did not reduce consumption");
            double deadline = 0;
            FramePacing::advance(deadline, 1e9 / 60, 1e9);
            require(deadline > 1e9, "A stall caused burst catch-up frames");
        }
        for (double fps : {20., 30., 60.}) for (double hz : {50., 60., 144., 180.}) for (bool vrr : {false, true}) {
            FramePacing clock; clock.configure(fps, hz, true, vrr, true, 0);
            unsigned runs = 0;
            for (unsigned tick = 0; tick < 10000; ++tick) {
                const double now = tick * 1e6;
                for (unsigned i = 0; i < clock.core_budget && clock.due(now); ++i) {
                    ++runs; clock.next_core += clock.core_period;
                }
                clock.finish_core_work(now, false);
            }
            require(std::abs(double(runs) - fps * 10) <= 1, "Display refresh changed native content speed");
            require(std::abs(clock.present_period - (vrr ? clock.core_period : std::max(clock.core_period, 1e9 / hz))) < .01,
                    "Presentation must not redraw at a faster display's refresh rate");
            // Pause does not accumulate emulation debt; reset on resume.
            clock.audio_end = 100e9; clock.ratio = .8; clock.reset(20e9);
            require(clock.due(20e9) && !clock.due(20e9 - 1) && clock.audio_end == 20e9 && clock.ratio == clock.speed,
                    "Resume retained stale media clocks or rate correction");
            clock.finish_core_work(20e9, true);
            require(!clock.due(20e9) && clock.due(20e9 + clock.core_period), "Audio stall did not defer execution by one interval");
        }
        // Model the real loop, including a blocking swap and the timer's
        // remaining wait. A backend accepting only interval 1 must reach the
        // same content rate as a native multi-refresh swap without double waits.
        for (double fps : {20., 30., 50., 60., 120.}) for (double hz : {50., 60., 144., 180.})
            for (bool native_interval : {false, true}) {
                FramePacing clock; clock.configure(fps, hz, true, false, true, 0);
                const int interval = native_interval ? FramePacing::preferred_swap_interval(fps, hz, false) : 1;
                const double swap_period = 1e9 * interval / hz;
                unsigned runs = 0, presents = 0;
                double now = 0;
                while (now < 10e9) {
                    const double start = now;
                    for (unsigned i = 0; i < clock.core_budget && clock.due(now); ++i) {
                        ++runs; clock.next_core += clock.core_period;
                    }
                    clock.finish_core_work(now, false);
                    now = std::ceil((now + 100000) / swap_period) * swap_period;
                    ++presents;
                    clock.finish_present(now, false, start);
                    now = std::max(now, clock.next_present);
                }
                require(std::abs(double(runs) - fps * 10) <= 2, "Blocking presents changed core execution speed");
                require(std::abs(double(presents) - std::min(fps, hz) * 10) <= 2,
                        "Swap interval fallback overpresented or added a second pacing wait");
            }
        {
            // A small phase difference plus ordinary scheduling jitter must
            // not alternate an idle present with two core runs on the next one.
            FramePacing clock; clock.configure(60, 180, true, false, true, 0);
            require(clock.core_driven && clock.content_clock, "Faster fixed-refresh display did not use the core clock");
            clock.next_core = 1000000;
            double now = 0;
            for (int i = 0; i < 600; ++i) {
                now = std::max(now, std::min(clock.next_core, clock.next_present));
                now += i % 2 ? 2000000 : 0;
                unsigned executed = 0;
                for (unsigned run = 0; run < clock.core_budget && clock.due(now); ++run) {
                    ++executed; clock.next_core += clock.core_period;
                }
                if (i > 0) require(executed == 1, "Separate presentation/core clocks caused a skipped or bunched frame");
                clock.finish_core_work(now, false);
                clock.finish_present(now + 100000, false, now);
                now = std::max(now + 100000, clock.next_present);
            }
            clock.configure(60, 50, true, false, true, 0);
            require(!clock.core_driven, "Slower display lost its bounded catch-up path");
            clock.configure(60, 180, true, true, true, 0);
            require(!clock.core_driven, "Fixed-refresh adjustment changed VRR scheduling");
        }
        std::cout << "Fixed/VRR clocks, multi-interval audio, stalls, and resume passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
