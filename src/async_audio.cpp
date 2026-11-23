#include "async_audio.h"
#include "audio_output.h"

void AsyncAudio::configure(const retro_audio_callback& value)
{
    requested = value;
    pending = true;
}

bool AsyncAudio::registered() const
{
    return requested.callback != nullptr;
}

void AsyncAudio::set_enabled(bool active)
{
    if (pumping)
    {
        return;
    }
    if (pending)
    {
        const auto previous = callbacks;
        const bool was_enabled = enabled;
        enabled = false;
        callbacks = {};
        if (was_enabled && previous.set_state)
        {
            previous.set_state(false);
        }
        callbacks = requested;
        pending = false;
    }
    const bool next = active && callbacks.callback;
    if (next != enabled)
    {
        enabled = next;
        next_service = next ? 0 : std::numeric_limits<std::uint64_t>::max();
        if (callbacks.set_state)
        {
            callbacks.set_state(next);
        }
    }
    if (!enabled)
    {
        next_service = std::numeric_limits<std::uint64_t>::max();
    }
}

void AsyncAudio::pump(AudioOutput& output, std::uint64_t now)
{
    if (!enabled || pending || pumping || now < next_service || !output)
    {
        return;
    }
    struct PumpGuard
    {
        bool& pumping;
        explicit PumpGuard(bool& pumping) : pumping(pumping)
        {
            pumping = true;
        }
        ~PumpGuard()
        {
            pumping = false;
        }
    } guard(pumping);
    constexpr double target_ms = 60;
    // A non-producing callback must not spin, and tiny batches have bounded work.
    for (unsigned count = 0; count < 4096 && !pending && output.queue_ms() < target_ms; ++count)
    {
        const auto before = output.produced_ms();
        callbacks.callback();
        if (output.produced_ms() <= before)
        {
            break;
        }
    }
    next_service = SDL_GetTicksNS() + 10000000;
}

std::uint64_t AsyncAudio::next_wake() const
{
    return enabled ? next_service : std::numeric_limits<std::uint64_t>::max();
}

void AsyncAudio::clear()
{
    set_enabled(false);
    requested = callbacks = {};
    pending = false;
    next_service = std::numeric_limits<std::uint64_t>::max();
}
