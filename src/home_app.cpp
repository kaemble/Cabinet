#include "home_window.h"
#include "home.h"
#include "app_paths.h"
#include "cli_help.h"
#include <charconv>
#include <iostream>
#include <memory>

int run_home(int argc, char** argv)
{
    std::string import, import_system, screenshot, data_override, runner_override;
    int frames = 0;
    int window_width = 1920, window_height = 1080;
    HomeLogger logger;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h")
        {
            print_help(std::cout);
            return 0;
        }
        if (!arg.starts_with("--"))
        {
            throw std::runtime_error(
                "For direct game launch, use cabinet-runner --rom PATH --core PATH. See cabinet --help.");
        }
        if (i + 1 >= argc)
        {
            throw std::runtime_error("Missing value for " + arg);
        }
        if (arg == "--import")
        {
            import = argv[++i];
        }
        else if (arg == "--system")
        {
            import_system = argv[++i];
        }
        else if (arg == "--data-dir")
        {
            data_override = argv[++i];
        }
        else if (arg == "--runner")
        {
            runner_override = argv[++i];
        }
        else if (arg == "--log-level")
        {
            logger.level = parse_home_log_level(argv[++i]);
        }
        else if (arg == "--screenshot")
        {
            screenshot = argv[++i];
        }
        else if (arg == "--window-size")
        {
            const std::string value = argv[++i];
            const auto separator = value.find('x');
            if (separator == std::string::npos)
            {
                throw std::runtime_error("--window-size requires WIDTHxHEIGHT");
            }
            const auto* begin = value.data();
            const auto* end = begin + value.size();
            const auto width = std::from_chars(begin, begin + separator, window_width);
            const auto height = std::from_chars(begin + separator + 1, end, window_height);
            if (width.ec != std::errc{} || width.ptr != begin + separator || height.ec != std::errc{} ||
                height.ptr != end || window_width < 560 || window_height < 600)
            {
                throw std::runtime_error("--window-size requires WIDTHxHEIGHT, at least 560x600");
            }
        }
        else if (arg == "--frames")
        {
            frames = std::stoi(argv[++i]);
            if (frames <= 0)
            {
                throw std::runtime_error("Invalid frame count");
            }
        }
        else
        {
            throw std::runtime_error("Unknown home option: " + arg);
        }
    }
    if (!import.empty() && import_system.empty())
    {
        throw std::runtime_error("--import requires --system ID");
    }
    if (!import_system.empty() && system_index(import_system) < 0)
    {
        throw std::runtime_error("Unknown system: " + import_system);
    }
    if (!screenshot.empty() && !frames)
    {
        throw std::runtime_error("--screenshot requires --frames");
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        throw std::runtime_error(SDL_GetError());
    }
    const auto data = application_data(data_override);
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("Cabinet — Library", window_width, window_height,
                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY),
        SDL_DestroyWindow);
    if (!window)
    {
        throw std::runtime_error(SDL_GetError());
    }
    SDL_SetWindowMinimumSize(window.get(), 560, 600);
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateRenderer(window.get(), nullptr),
                                                                           SDL_DestroyRenderer);
    if (!renderer)
    {
        throw std::runtime_error(SDL_GetError());
    }
    Home home(window.get(), renderer.get(), data,
              (runner_override.empty() ? sibling_runner() : fs::absolute(runner_override)).string(), logger);
    if (!import_system.empty())
    {
        home.select_system(import_system);
    }
    if (!import.empty())
    {
        home.import_file(import, import_system == "generic");
    }
    int ticks = 0;
    bool presented = false;
    auto dispatch = [&](const SDL_Event& event)
    {
        try
        {
            home.event(event);
        }
        catch (const std::exception& error)
        {
            home.report_error(error.what());
        }
    };
    while (home.running && (!frames || ticks < frames))
    {
        SDL_Event event;
        if (presented && !frames)
        {
            const int timeout = home.wait_timeout();
            const bool received = (timeout < 0) ? SDL_WaitEvent(&event) : SDL_WaitEventTimeout(&event, timeout);
            if (received)
            {
                dispatch(event);
            }
        }
        while (SDL_PollEvent(&event))
        {
            dispatch(event);
        }
        if (!home.running)
        {
            break;
        }
        try
        {
            home.tick();
        }
        catch (const std::exception& error)
        {
            home.report_error(error.what());
        }
        const auto flags = SDL_GetWindowFlags(window.get());
        const bool visible = (flags & SDL_WINDOW_INPUT_FOCUS) && !(flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN));
        if (frames || !presented || (home.needs_redraw && visible))
        {
            home.draw();
            home.needs_redraw = false;
            ++ticks;
            if (!screenshot.empty() && ticks == frames)
            {
                auto surface = SDL_RenderReadPixels(renderer.get(), nullptr);
                if (!surface)
                {
                    throw std::runtime_error(SDL_GetError());
                }
                bool saved = SDL_SaveBMP(surface, screenshot.c_str());
                SDL_DestroySurface(surface);
                if (!saved)
                {
                    throw std::runtime_error(SDL_GetError());
                }
            }
            SDL_RenderPresent(renderer.get());
            presented = true;
        }
        if (frames)
        {
            SDL_Delay(16);
        }
    }
    return 0;
}
