#include "libretro_core.h"
#include <iostream>
#include <stdexcept>
#include <string>

static std::string calls;
static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
static void install_test_api(LibretroCore& core) {
    core.api.init = [] { calls += "init;"; };
    core.api.deinit = [] { calls += "deinit;"; };
    core.api.load_game = [](const retro_game_info* game) {
        calls += "load;";
        return game != nullptr;
    };
    core.api.unload_game = [] { calls += "unload;"; };
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected test core library");
        {
            LibretroCore core;
            install_test_api(core);
            core.initialize(); core.initialize();
            require(!core.load_game(nullptr) && !core.loaded(), "Failed load marked game loaded");
            core.shutdown(); core.shutdown();
            require(calls == "init;load;deinit;", "Failed load unloaded nonexistent content or shutdown repeated");
        }
        calls.clear();
        {
            LibretroCore core;
            install_test_api(core);
            core.initialize();
            retro_game_info game{};
            require(core.load_game(&game) && core.loaded(), "Successful load not tracked");
            core.shutdown();
            require(!core.loaded(), "Shutdown left content marked loaded");
            core.close(); core.close();
            require(!core.api.run && !core.api.init, "Close retained library entry points");
        }
        require(calls == "init;load;unload;deinit;", "Unload/deinit order changed or cleanup repeated");
        calls.clear();
        {
            LibretroCore core;
            install_test_api(core);
            core.initialize();
            retro_game_info game{};
            require(core.load_game(&game), "Test content failed to load");
        }
        require(calls == "init;load;unload;deinit;", "Destructor failed to clean up a loaded core");
        {
            LibretroCore core;
            bool rejected = false;
            try { core.open("/cabinet-nonexistent-core.so"); }
            catch (const std::exception&) { rejected = true; }
            require(rejected && !core.api.run, "Missing core retained entry points");
            for (int i = 0; i < 2; ++i) {
                core.open(argv[1]);
                retro_system_info info{};
                core.api.get_system_info(&info);
                require(info.library_name && std::string(info.library_name) == "Cabinet test core", "Wrong core information");
                core.api.set_environment([](unsigned, void*) { return false; });
                core.initialize();
                const char content = 0;
                retro_game_info game{nullptr, &content, sizeof(content), nullptr};
                require(core.load_game(&game), "Loaded library failed to load test content");
                core.close();
                require(!core.loaded() && !core.api.run, "Closing library retained loaded state");
            }
        }
        std::cout << "Core loading, failure cleanup, shutdown ordering, and reopening passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
