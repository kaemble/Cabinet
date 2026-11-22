#include "runner_pacing.h"
#include "audio_output.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

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
