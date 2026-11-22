#include "game_process.h"
#include <sstream>
#include <fstream>
#include "archive_fixture.h"
#include "app_paths.h"
#include "test_support.h"
#include <iostream>
#include <stdexcept>
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Expected executable and ROM");
        TestDirectory directory("cabinet-process");
        require(SDL_Init(0), SDL_GetError());
        require(std::filesystem::equivalent(sibling_runner(), argv[1]), "Default runner must resolve beside the home executable");
        Game game{42, argv[2], "Test game", "gb", bundled_core(systems()[0]).string()};
        game.save_id = "TESTGAMEGB00000000000000";
        game.save_name = "Test game";
        std::ostringstream diagnostics;
        GameProcess process({HomeLogLevel::Info, &diagnostics});
        process.start(argv[1], game, {"--title", "Link's game; $HOME `echo nope`", "--frames", "8", "--data-dir", directory.path.string()});
        require(diagnostics.str().find("Launching cabinet-runner: ") != std::string::npos, "Launch command was not logged before polling");
        require(diagnostics.str().find("--title 'Link'\\''s game; $HOME `echo nope`'") != std::string::npos, "Title was not safely quoted in launch log");
        require(diagnostics.str().find("--rom " + shell_argument(game.path)) != std::string::npos &&
                diagnostics.str().find("--core " + shell_argument(game.core)) != std::string::npos &&
                diagnostics.str().find("--system gb") != std::string::npos, "Full command omitted core, ROM or system");
        require(diagnostics.str().find("--game-id TESTGAMEGB00000000000000") != std::string::npos, "Persistent library identity missing from launch");
        require(diagnostics.str().find("--save-name 'Test game'") != std::string::npos, "Readable folder missing from launch");
        bool refused = false;
        try { process.start(argv[1], game); } catch (const std::exception&) { refused = true; }
        require(refused, "A second concurrent launch must be rejected");
        std::optional<int> code;
        Uint64 deadline = SDL_GetTicks() + 10000;
        while (!code && SDL_GetTicks() < deadline) { code = process.poll(); SDL_Delay(5); }
        require(code.has_value() && *code == 0, "Child game must launch and exit successfully");
        require(diagnostics.str().find("exited with status 0") != std::string::npos, "Exit status not logged");
        require(diagnostics.str().find("[cabinet debug]") == std::string::npos, "Info leaked debug logs");
        require(process.ready(), "Ready notification must identify a loaded core");
        require(process.output().find("video frames") != std::string::npos, "Child must render video");
        process.start(argv[1], game, {"--invalid-option", "value"});
        code.reset(); deadline = SDL_GetTicks() + 10000;
        while (!code && SDL_GetTicks() < deadline) { code = process.poll(); SDL_Delay(5); }
        require(code.has_value() && *code != 0, "Failed child must return an error");
        require(process.error(*code).find("Unknown option") != std::string::npos, "Child error must remain actionable in library");
        require(!process.ready(), "Failed launches must not count as recently played");
        const auto zip = directory.path / "game.zip";
        std::filesystem::create_directories(zip.parent_path());
        std::ifstream rom(argv[2], std::ios::binary);
        const std::string rom_bytes{std::istreambuf_iterator<char>(rom), {}};
        write_zip(zip, {{"nested/Preserved name.gbc", rom_bytes}});
        game.path = zip.string(); game.archive_member = "nested/Preserved name.gbc";
        auto run_zip = [&](const std::string& executable) {
            diagnostics.str(""); diagnostics.clear();
            process.start(executable, game, {"--frames", "8", "--data-dir", zip.parent_path().string()});
            require(process.active() && !process.ready(), "ZIP preparation not tracked as active");
            code.reset(); deadline = SDL_GetTicks() + 10000;
            while (!code && SDL_GetTicks() < deadline) { code = process.poll(); SDL_Delay(5); }
            require(code.has_value(), "ZIP launch never completed");
        };
        run_zip(argv[1]);
        require(*code == 0 && process.ready(), "Extracted ROM did not run successfully");
        require(diagnostics.str().find("Preserved name.gbc") != std::string::npos, "Extracted basename lost");
        const auto prefix = (std::filesystem::temp_directory_path() / "cabinet-rom-").string();
        const auto begin = diagnostics.str().find(prefix);
        require(begin != std::string::npos, "Extracted ROM path missing from command log");
        const auto end = diagnostics.str().find('/', begin + prefix.size());
        require(!std::filesystem::exists(diagnostics.str().substr(begin, end - begin)), "Temporary ROM directory leaked after exit");
        run_zip("/missing/cabinet-runner");
        require(*code != 0 && !process.active(), "Failed ZIP launch remained active");
        game.archive_member = "removed.gbc";
        run_zip(argv[1]);
        require(*code != 0 && process.error(*code).find("missing from ZIP") != std::string::npos, "Missing member did not report extraction error");
        game.archive_member.clear(); game.path = argv[2];
        game.path += ".missing";
        refused = false;
        try { process.start(argv[1], game); } catch (const std::exception&) { refused = true; }
        require(refused, "Missing game must be rejected before spawning");
        SDL_Quit(); std::cout << "Child launch, completion, and error handling passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; SDL_Quit(); return 1; }
}
