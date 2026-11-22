#pragma once
#include <SDL3/SDL.h>
#include "library.h"
#include "home_log.h"
#include <optional>
#include <future>

// One active game per home window. The child owns its SDL window and core.
class GameProcess
{
public:
    ~GameProcess();
    explicit GameProcess(HomeLogger logger = {}) : logger(logger)
    {
    }
    GameProcess(const GameProcess&) = delete;
    GameProcess& operator=(const GameProcess&) = delete;
    void start(const std::string& executable, const Game& game, const std::vector<std::string>& extra = {});
    std::optional<int> poll();
    bool active() const
    {
        return process != nullptr || preparation.valid();
    }
    bool ready() const
    {
        return started;
    }
    const std::string& output() const
    {
        return log;
    }
    std::string error(int code) const;

private:
    void launch(const std::string& executable, const Game& game, const std::vector<std::string>& extra);
    void cleanup();
    std::future<std::filesystem::path> preparation;
    std::filesystem::path extracted;
    Game pending_game;
    std::string pending_executable;
    std::vector<std::string> pending_extra;
    HomeLogger logger;
    SDL_Process* process = nullptr;
    bool started = false;
    std::string log;
};
