#pragma once
#include <libretro.h>
#include <cstdint>
#include <limits>

class AudioOutput;

// Queue-driven libretro audio notifications, serialized with other core calls.
class AsyncAudio
{
public:
    void configure(const retro_audio_callback& callbacks);
    bool registered() const;
    void set_enabled(bool enabled);
    void pump(AudioOutput& output, std::uint64_t now);
    std::uint64_t next_wake() const;
    void clear();

private:
    retro_audio_callback requested{}, callbacks{};
    bool pending = false, enabled = false, pumping = false;
    std::uint64_t next_service = std::numeric_limits<std::uint64_t>::max();
};
