#include "runner_pacing.h"
#include "audio_output.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

static void require(bool Condition, const char* Message)
{
    if (!Condition) throw std::runtime_error(Message);
}

static void test_pacing()
{
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> Window(
        SDL_CreateWindow("Pacing test", 480, 420, 0), SDL_DestroyWindow);
    require(Window != nullptr, SDL_GetError());
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> Renderer(
        SDL_CreateRenderer(Window.get(), nullptr), SDL_DestroyRenderer);
    require(Renderer != nullptr, SDL_GetError());

    AudioOutput Audio;
    RunnerPacing Pacing(Window.get(), Renderer.get(), false);
    RunnerPacing::Settings Settings;
    Settings.vsync = false;
    Pacing.initialize_presenter(false, false);
    Pacing.update(Settings);

    const auto Initial = Pacing.core_deadline();
    Pacing.finish_core(Audio, 0);
    const auto Advanced = Pacing.core_deadline();
    require(std::abs(Advanced - Initial - 1e9 / 60) < 1,
        "Finishing a core run did not advance its content clock");
    Pacing.update(Settings);
    require(Pacing.core_deadline() == Advanced, "Unchanged settings reset the content clock");

    Settings.frame_rate = 30;
    Pacing.update(Settings);
    require(std::abs(Pacing.core_period() - 1e9 / 30) < 1, "Core rate change did not reconfigure pacing");
    for (unsigned Index = 0; Index < 100; ++Index) Pacing.finish_core(Audio, 0);
    const auto BeforeEvent = Pacing.core_deadline();
    SDL_Event Event{};
    Event.type = SDL_EVENT_WINDOW_DISPLAY_CHANGED;
    Pacing.event(Event);
    Pacing.update(Settings);
    require(Pacing.core_deadline() < BeforeEvent && Pacing.core_deadline() <= SDL_GetTicksNS(),
        "Display change did not discard the previous display's deadlines");

    for (unsigned Index = 0; Index < 100; ++Index) Pacing.finish_core(Audio, 0);
    Pacing.reset();
    require(Pacing.core_due(0), "Resuming retained a future core deadline");
    Pacing.finish_core_work(Audio, true);
    require(!Pacing.core_due(0), "Audio gating did not defer core execution");

    Settings.fast_forward = true;
    Pacing.update(Settings);
    for (unsigned Index = 0; Index < 100; ++Index) Pacing.finish_core(Audio, 0);
    require(Pacing.core_due(0) && !Pacing.core_due(1),
        "Fast-forward must bypass clock deadlines while bounding each iteration to one run");
    const auto Iteration = Pacing.begin_iteration(false, Audio);
    Pacing.finish_present(SDL_GetTicksNS(), false, Iteration.started);

    Settings.fast_forward = false;
    Pacing.update(Settings);
    require(Pacing.core_due(0), "Leaving fast-forward retained its accumulated deadline");
    const auto Now = SDL_GetTicks();
    require(Pacing.menu_deadline(Now, true) <= Now + 33 &&
        Pacing.menu_deadline(Now, false) <= Now + 1000,
        "Menu waits lost their capture cadence or periodic display check");

    SDL_AudioSpec format{SDL_AUDIO_S16, 2, 48000};
    auto* stream = SDL_CreateAudioStream(&format, &format);
    require(stream != nullptr, SDL_GetError());
    AudioOutput callback_audio(stream);
    callback_audio.set_sample_rate(48000);
    std::vector<std::int16_t> samples(48000);
    callback_audio.submit(samples.data(), samples.size() / 2, false);
    Settings.frame_rate = 5;
    Settings.audio = Settings.asynchronous_audio = true;
    Pacing.update(Settings);
    const auto before_callback = Pacing.core_deadline();
    Pacing.finish_core(callback_audio, 0);
    require(std::abs(Pacing.core_deadline() - before_callback - 200000000) < 1,
            "Callback-produced samples deferred the video clock");
    const auto now = SDL_GetTicksNS();
    Pacing.finish_present(now, false, now, now);
    callback_audio.set_frequency_ratio(1.1f);
    const auto audio_iteration = Pacing.begin_iteration(false, callback_audio, SDL_GetTicksNS() + 5000000);
    require(audio_iteration.started < Pacing.core_deadline() && !Pacing.core_due(0),
            "Audio wake waited for the next video frame");
    require(SDL_GetAudioStreamFrequencyRatio(stream) == 1.0f,
            "Callback audio received frame-based frequency correction");
}

int main()
{
    try
    {
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        test_pacing();
        SDL_Quit();
        std::cout << "Pacing reconfiguration, audio gating, resume, and fast-forward passed\n";
        return 0;
    }
    catch (const std::exception& Error)
    {
        SDL_Quit();
        std::cerr << Error.what() << '\n';
        return 1;
    }
}
