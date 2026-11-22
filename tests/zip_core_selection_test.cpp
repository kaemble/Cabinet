#include "archive_fixture.h"
#include "game_process.h"
#include "library.h"
#include "rom_archive.h"
#include "test_support.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

static void require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

int main(int argc, char** argv)
{
    try
    {
        require(argc == 4, "Expected runner, memory core, and path core");
        require(SDL_Init(0), SDL_GetError());
        TestDirectory directory("cabinet-zip-core");
        const auto zip = directory.path / "game.zip";
        write_zip(zip, {{"nested/Game.sfc", "ROM fixture"}});
        Library library(directory.path / "library.sqlite3");
        Game imported{0, zip.string(), "Game", "snes", argv[2]};
        imported.archive_member = "nested/Game.sfc";
        const auto id = library.add(imported);
        auto selected = *library.game(id);
        const auto original = selected;
        selected.core = argv[3];
        library.update(selected);
        const auto saved = *library.game(id);
        require(saved.core == selected.core && saved.path == original.path && saved.archive_member == original.archive_member,
                "Changing core lost the selected ROM or archive");
        require(saved.save_id == original.save_id && saved.save_name == original.save_name,
                "Changing core lost the game's save identity");
        // Reuse the cached need_fullpath=true result: it must not reject a
        // single-file cartridge after a successful first probe.
        rom_archive::validate_core(argv[3], directory.path / "core-info", "snes");
        bool rejected = false;
        try
        {
            rom_archive::validate_core(argv[3], directory.path / "core-info", "psx");
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        require(rejected, "A file-based disc core must still require unpacked content");
        std::ostringstream diagnostics;
        GameProcess process({HomeLogLevel::Info, &diagnostics});
        process.start(argv[1], saved, {"--frames", "4", "--no-audio", "--data-dir", directory.path.string()});
        std::optional<int> status;
        const auto deadline = SDL_GetTicks() + 10000;
        while (!status && SDL_GetTicks() < deadline)
        {
            status = process.poll();
            SDL_Delay(5);
        }
        if (!status || *status != 0)
        {
            throw std::runtime_error("ZIP launch with a file-based core failed: " + process.output());
        }
        require(process.ready(), "File-based core did not load the extracted ROM");
        require(diagnostics.str().find("Game.sfc") != std::string::npos, "Extracted ROM basename was lost");
        require(fs::is_regular_file(zip), "Playing a ZIP changed the original archive");
        const auto prefix = (fs::temp_directory_path() / "cabinet-rom-").string();
        const auto begin = diagnostics.str().find(prefix);
        require(begin != std::string::npos, "Extracted ROM path was not logged");
        const auto end = diagnostics.str().find('/', begin + prefix.size());
        require(!fs::exists(diagnostics.str().substr(begin, end - begin)), "Temporary extracted ROM was not cleaned up");
        selected.core = argv[2];
        library.update(selected);
        require(library.game(id)->core == selected.core, "Switching back to a memory core failed");
        SDL_Quit();
        std::cout << "ZIP core selection, extracted file launch, and cleanup passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
