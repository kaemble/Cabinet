#include "async_audio.h"
#include "audio_output.h"
#include <SDL3/SDL.h>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

static AsyncAudio* source;
static AudioOutput* output;
static unsigned calls = 0, replacement_calls = 0, scalar_calls = 0;
static bool produce = true, remove_during_callback = false;
static unsigned batch_frames = 480;
static std::vector<bool> states, replacement_states;

static void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

static void render_audio()
{
    ++calls;
    if (produce)
    {
        static const std::array<std::int16_t, 960> samples{};
        output->submit(samples.data(), batch_frames, false);
    }
    if (remove_during_callback)
    {
        source->configure({});
    }
}

int main()
{
    try
    {
        require(SDL_Init(SDL_INIT_AUDIO), SDL_GetError());
        {
            SDL_AudioSpec format{SDL_AUDIO_S16, 2, 48000};
            AudioOutput audio(SDL_CreateAudioStream(&format, &format));
            require(static_cast<bool>(audio), SDL_GetError());
            audio.set_sample_rate(48000);
            AsyncAudio notifications;
            source = &notifications;
            output = &audio;
            const retro_audio_callback callbacks{render_audio, [](bool active) { states.push_back(active); }};
            notifications.configure(callbacks);
            require(notifications.registered() && states.empty(), "Registration activated audio before the device was ready");
            notifications.pump(audio, SDL_GetTicksNS());
            require(calls == 0, "Inactive audio callback ran");
            notifications.set_enabled(true);
            notifications.set_enabled(true);
            require(states == std::vector<bool>{true}, "Repeated enable duplicated state notifications");
            notifications.pump(audio, notifications.next_wake());
            require(calls == 6 && audio.produced_ms() == 60, "Callback requests did not fill the audio queue");
            notifications.pump(audio, notifications.next_wake());
            require(calls == 6, "A full queue still requested audio");
            notifications.set_enabled(false);
            require(states == std::vector<bool>({true, false}), "Pause did not disable the producer");
            require(notifications.next_wake() == std::numeric_limits<std::uint64_t>::max(), "Inactive source kept waking the runner");
            audio.reset(true);
            notifications.pump(audio, SDL_GetTicksNS());
            require(calls == 6, "Paused source produced audio");
            notifications.set_enabled(true);
            produce = false;
            notifications.pump(audio, notifications.next_wake());
            require(calls == 7, "A non-producing callback spun");
            notifications.pump(audio, 0);
            require(calls == 7, "Empty producer ignored its retry deadline");
            produce = true;
            batch_frames = 1;
            const auto before_tiny = calls;
            notifications.pump(audio, notifications.next_wake());
            require(calls - before_tiny >= 2880 && calls - before_tiny <= 4096,
                    "Small callback batches could not fill the target queue");
            batch_frames = 480;
            notifications.configure({[]
                                     {
                                         ++replacement_calls;
                                         render_audio();
                                     }, [](bool active) { replacement_states.push_back(active); }});
            notifications.set_enabled(true);
            require(states.back() == false && replacement_states == std::vector<bool>{true},
                    "Replacement did not deactivate the old callback and activate the new one");
            audio.reset(true);
            notifications.pump(audio, notifications.next_wake());
            require(replacement_calls == 6, "Replacement callback did not produce audio");
            audio.reset(true);
            remove_during_callback = true;
            const auto before_remove = replacement_calls;
            notifications.pump(audio, notifications.next_wake());
            require(replacement_calls == before_remove + 1 && !notifications.registered(),
                    "Unregistered callback was invoked again during the pump");
            notifications.set_enabled(true);
            require(replacement_states == std::vector<bool>({true, false}), "Unregistration did not stop the old producer");
            remove_during_callback = false;
            notifications.configure(callbacks);
            notifications.set_enabled(true);
            notifications.clear();
            require(states.back() == false && !notifications.registered(), "Shutdown left an active callback");
        }
        {
            SDL_AudioSpec format{SDL_AUDIO_S16, 2, 48000};
            auto* stream = SDL_CreateAudioStream(&format, &format);
            require(stream, SDL_GetError());
            AudioOutput audio(stream);
            audio.set_sample_rate(48000);
            output = &audio;
            AsyncAudio notifications;
            notifications.configure({[]
            {
                const auto sample = static_cast<std::int16_t>(++scalar_calls);
                output->submit_sample(sample, -sample, false);
                const std::int16_t pair[]{sample, sample};
                output->submit(pair, 1, false);
                for (int i = 0; i < 1025; ++i)
                {
                    output->submit_sample(-sample, sample, false);
                }
            }, nullptr});
            notifications.set_enabled(true);
            notifications.pump(audio, notifications.next_wake());
            require(scalar_calls == 3, "Callback audio did not count buffered samples toward the queue target");
            std::vector<std::int16_t> actual(scalar_calls * 1027 * 2);
            const auto bytes = static_cast<int>(actual.size() * sizeof(actual[0]));
            require(SDL_GetAudioStreamData(stream, actual.data(), bytes) == bytes, "Buffered callback samples were lost");
            auto value = actual.begin();
            for (std::int16_t sample = 1; sample <= static_cast<int>(scalar_calls); ++sample)
            {
                require(*value++ == sample && *value++ == -sample, "Scalar audio did not precede batch audio");
                require(*value++ == sample && *value++ == sample, "Batch audio was reordered");
                for (int i = 0; i < 1025; ++i)
                {
                    require(*value++ == -sample && *value++ == sample, "Scalar audio changed across a buffer flush");
                }
            }
        }
        SDL_Quit();
        std::cout << "Callback audio demand, lifecycle, replacement and empty/tiny batches passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
