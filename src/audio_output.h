#pragma once
#include <SDL3/SDL.h>
#include <cstddef>
#include <cstdint>

// Owns the playback stream. All calls are made on the core/runner thread.
class AudioOutput
{
public:
    AudioOutput() = default;
    // Takes ownership; also supports an unbound SDL stream for queue tests.
    explicit AudioOutput(SDL_AudioStream* stream) : stream_(stream)
    {
    }
    ~AudioOutput();
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;
    AudioOutput(AudioOutput&& other) noexcept;
    AudioOutput& operator=(AudioOutput&& other) noexcept;

    bool open(unsigned sample_rate, int volume);
    void close();
    explicit operator bool() const
    {
        return stream_ != nullptr;
    }
    void set_sample_rate(double sample_rate);
    void set_volume(int volume);
    void set_frequency_ratio(float ratio);
    bool device_format(SDL_AudioSpec& spec, int& frames) const;

    std::size_t submit(const std::int16_t* samples, std::size_t frames, bool fast_forward);
    double queue_ms() const;
    int queued_bytes() const;
    bool ready(double frame_rate, bool fast_forward);
    void reset(bool pause_device);
    bool prime(double target_ms);
    bool started() const
    {
        return started_;
    }
    bool throttled() const
    {
        return throttled_;
    }
    double produced_ms() const
    {
        return produced_ms_;
    }

private:
    SDL_AudioStream* stream_ = nullptr;
    unsigned sample_rate_ = 44100;
    int applied_volume_ = -1;
    bool started_ = false, throttled_ = false;
    double produced_ms_ = 0;
};
