#include "game_process.h"
#include <cstddef>
#include <stdexcept>
#include "rom_archive.h"
#include <chrono>
#include <string_view>

void GameProcess::cleanup()
{
    if (!extracted.empty())
    {
        std::error_code error;
        std::filesystem::remove_all(extracted, error);
        extracted.clear();
    }
}
GameProcess::~GameProcess()
{
    if (preparation.valid())
    {
        try
        {
            extracted = preparation.get();
        }
        catch (...)
        {
        }
    }
    if (process)
    {
        // Home normally refuses to close while a game runs. Preserve its ROM
        // if exceptional teardown occurs before the child exits.
        int code = 0;
        const bool exited = SDL_WaitProcess(process, false, &code);
        SDL_DestroyProcess(process);
        if (!exited)
        {
            return;
        }
    }
    cleanup();
}
void GameProcess::start(const std::string& executable, const Game& game, const std::vector<std::string>& extra)
{
    if (active())
    {
        throw std::runtime_error("A game is already running. Close its window before launching another.");
    }
    if (rom_archive::is_zip(game.path) && game.system != "generic")
    {
        if (game.archive_member.empty())
        {
            throw std::runtime_error("No ROM selected inside ZIP");
        }
        pending_executable = executable;
        pending_game = game;
        pending_extra = extra;
        log.clear();
        started = false;
        logger.write(HomeLogLevel::Info, "Preparing game from ZIP: " + game.path + " / " + game.archive_member);
        preparation = std::async(std::launch::async,
                                 [path = game.path, member = game.archive_member]
                                 {
                                     return rom_archive::extract(path, member);
                                 });
        return;
    }
    launch(executable, game, extra);
}
void GameProcess::launch(const std::string& executable, const Game& game, const std::vector<std::string>& extra)
{
    if (!std::filesystem::is_regular_file(executable))
    {
        throw std::runtime_error("cabinet-runner is missing. Put it beside cabinet or use --runner PATH.");
    }
    if (!std::filesystem::is_regular_file(game.path))
    {
        throw std::runtime_error("Game file is missing. Use Locate file in game details.");
    }
    if (game.core.empty() || !std::filesystem::is_regular_file(game.core))
    {
        throw std::runtime_error("Core file is missing. Choose a core in Edit details.");
    }
    std::vector<std::string> args{executable, "--rom", game.path, "--core", game.core, "--system", game.system};
    if (!game.save_id.empty())
    {
        args.push_back("--game-id");
        args.push_back(game.save_id);
    }
    if (!game.save_name.empty())
    {
        args.push_back("--save-name");
        args.push_back(game.save_name);
    }
    args.push_back("--playtime");
    args.push_back(std::to_string(game.playtime));
    args.insert(args.end(), extra.begin(), extra.end());
    if (const char* dual_screen = SDL_getenv("CABINET_DUAL_SCREEN");
        dual_screen && std::string_view(dual_screen) == "1")
    {
        args.push_back("--dual-screen");
    }
    std::vector<const char*> pointers;
    for (const auto& arg : args)
    {
        pointers.push_back(arg.c_str());
    }
    pointers.push_back(nullptr);
    auto props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, pointers.data());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    logger.write(HomeLogLevel::Info, "Launching cabinet-runner: " + shell_command(args));
    process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!process)
    {
        throw std::runtime_error(SDL_GetError());
    }
    log.clear();
    started = false;
}
std::optional<int> GameProcess::poll()
{
    if (preparation.valid())
    {
        if (preparation.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        {
            return {};
        }
        try
        {
            extracted = preparation.get();
            auto game = pending_game;
            game.path = (extracted / std::filesystem::path(game.archive_member).filename()).string();
            launch(pending_executable, game, pending_extra);
        }
        catch (const std::exception& error)
        {
            cleanup();
            log = std::string("Cabinet: ") + error.what() + "\n";
            logger.write(HomeLogLevel::Error, error.what());
            return 1;
        }
    }
    if (!process)
    {
        return {};
    }
    auto drain = [&]
    {
        char buffer[4096];
        auto stream = SDL_GetProcessOutput(process);
        // Bound work per UI tick while continuously draining the child's pipe.
        for (int i = 0; stream && i < 32; ++i)
        {
            std::size_t n = SDL_ReadIO(stream, buffer, sizeof(buffer));
            if (!n)
            {
                break;
            }
            log.append(buffer, n);
            if (!started && (log.find("\nLoaded ") != std::string::npos || log.starts_with("Loaded ")))
            {
                started = true;
                logger.write(HomeLogLevel::Debug, "cabinet-runner reported the game ready");
            }
            if (log.size() > 32768)
            {
                log.erase(0, log.size() - 32768);
            }
        }
    };
    drain();
    int code = 0;
    if (!SDL_WaitProcess(process, false, &code))
    {
        return {};
    }
    drain();
    SDL_DestroyProcess(process);
    process = nullptr;
    cleanup();
    logger.write(HomeLogLevel::Info, "cabinet-runner exited with status " + std::to_string(code));
    if (code != 0)
    {
        logger.write(HomeLogLevel::Error, error(code));
    }
    else if (!started)
    {
        logger.write(HomeLogLevel::Warn, "cabinet-runner exited before reporting a loaded game");
    }
    return code;
}
std::string GameProcess::error(int code) const
{
    auto pos = log.rfind("Cabinet: ");
    if (pos != std::string::npos)
    {
        return log.substr(pos + 9, log.find('\n', pos) - pos - 9);
    }
    return (code < 0) ? "The game process crashed. Your library is still available."
                      : "The game could not start. Check its core and required firmware.";
}
