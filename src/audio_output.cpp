#include "audio_output.h"
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

static void check_audio(bool success)
{
    if (!success)
    {
        throw std::runtime_error(SDL_GetError());
    }
}

AudioOutput::~AudioOutput()
{
    close();
}

AudioOutput::AudioOutput(AudioOutput&& other) noexcept
{
    *this = std::move(other);
}

AudioOutput& AudioOutput::operator=(AudioOutput&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    close();
    stream_ = std::exchange(other.stream_, nullptr);
    sample_rate_ = std::exchange(other.sample_rate_, 44100);
    applied_volume_ = std::exchange(other.applied_volume_, -1);
    started_ = std::exchange(other.started_, false);
    throttled_ = std::exchange(other.throttled_, false);
    produced_ms_ = std::exchange(other.produced_ms_, 0);
    return *this;
}

bool AudioOutput::open(unsigned sample_rate, int volume)
{
    close();
    sample_rate_ = sample_rate;
    SDL_AudioSpec spec{SDL_AUDIO_S16, 2, static_cast<int>(sample_rate)};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (stream_)
    {
        set_volume(volume);
    }
    return stream_ != nullptr;
}

void AudioOutput::close()
{
    if (stream_)
    {
        SDL_DestroyAudioStream(stream_);
    }
    stream_ = nullptr;
    sample_count_ = 0;
    applied_volume_ = -1;
    started_ = throttled_ = false;
    produced_ms_ = 0;
}

void AudioOutput::set_sample_rate(double sample_rate)
{
    flush_samples();
    if (sample_rate == 0)
    {
        close(); // Silent cores, such as 2048, have no audio stream.
    }
    if (stream_ && sample_rate > 0)
    {
        SDL_AudioSpec spec{SDL_AUDIO_S16, 2, static_cast<int>(sample_rate)};
        SDL_SetAudioStreamFormat(stream_, &spec, nullptr);
    }
    sample_rate_ = static_cast<unsigned>(std::lround(sample_rate));
}

void AudioOutput::set_volume(int volume)
{
    if (stream_ && volume != applied_volume_ && SDL_SetAudioStreamGain(stream_, volume / 100.0f))
    {
        applied_volume_ = volume;
    }
}

void AudioOutput::set_frequency_ratio(float ratio)
{
    if (stream_)
    {
        check_audio(SDL_SetAudioStreamFrequencyRatio(stream_, ratio));
    }
}

bool AudioOutput::device_format(SDL_AudioSpec& spec, int& frames) const
{
    return stream_ && SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream_), &spec, &frames);
}

std::size_t AudioOutput::submit(const std::int16_t* samples, std::size_t frames, bool fast_forward)
{
    // Preserve order when a core mixes the two libretro callbacks.
    flush_samples();
    write_batch(samples, frames, fast_forward);
    return frames;
}

void AudioOutput::flush_samples()
{
    if (sample_count_)
    {
        const auto frames = std::exchange(sample_count_, 0) / 2;
        write_batch(samples_.data(), frames, false);
    }
}

void AudioOutput::write_batch(const std::int16_t* samples, std::size_t frames, bool fast_forward)
{
    // Pace the producer in the main loop instead of dropping batches when
    // the core advances more than one display interval in retro_run().
    if (stream_ && !fast_forward)
    {
        check_audio(SDL_PutAudioStreamData(stream_, samples, static_cast<int>(frames * 4)));
    }
    if (stream_)
    {
        produced_ms_ += frames * 1000.0 / sample_rate_;
    }
}

int AudioOutput::queued_bytes() const
{
    return stream_ ? SDL_GetAudioStreamQueued(stream_) : 0;
}

double AudioOutput::queue_ms() const
{
    if (!stream_)
    {
        return 0;
    }
    SDL_AudioSpec output{};
    if (!SDL_GetAudioStreamFormat(stream_, nullptr, &output))
    {
        return 0;
    }
    const double queued = std::max(0, queued_bytes()) * 1000.0 / (sample_rate_ * 4.0);
    const int bytes = SDL_AUDIO_BYTESIZE(output.format) * output.channels;
    return std::max(queued, ((bytes && output.freq > 0) ? std::max(0, SDL_GetAudioStreamAvailable(stream_)) * 1000.0 /
                                                              (bytes * double(output.freq))
                                                        : 0));
}

bool AudioOutput::ready(double frame_rate, bool fast_forward)
{
    if (!stream_ || fast_forward)
    {
        return true;
    }
    const double queued = queue_ms();
    const double target = std::max(60.0, 3000.0 / frame_rate);
    if (queued > std::max(120.0, target * 3))
    {
        throttled_ = true;
    }
    else if (queued <= target)
    {
        throttled_ = false;
    }
    return !throttled_;
}

void AudioOutput::reset(bool pause_device)
{
    sample_count_ = 0;
    if (stream_)
    {
        SDL_ClearAudioStream(stream_);
        if (pause_device)
        {
            SDL_PauseAudioStreamDevice(stream_);
        }
    }
    started_ = throttled_ = false;
}

bool AudioOutput::prime(double target_ms)
{
    if (!stream_ || started_ || queue_ms() < target_ms)
    {
        return false;
    }
    check_audio(SDL_ResumeAudioStreamDevice(stream_));
    started_ = true;
    return true;
}
