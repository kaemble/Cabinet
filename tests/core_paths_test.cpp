#include "library.h"
#include "test_support.h"
#include <SDL3/SDL.h>
#include <sqlite3.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

static void require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

class CoreDatabase
{
public:
    explicit CoreDatabase(const fs::path& path)
    {
        require(sqlite3_open(path.string().c_str(), &db) == SQLITE_OK, "Cannot open test database");
    }
    ~CoreDatabase()
    {
        sqlite3_close(db);
    }
    std::string core(std::int64_t id)
    {
        sqlite3_stmt* query = nullptr;
        require(sqlite3_prepare_v2(db, "SELECT core FROM games WHERE id=?", -1, &query, nullptr) == SQLITE_OK,
                "Cannot prepare core lookup");
        sqlite3_bind_int64(query, 1, id);
        require(sqlite3_step(query) == SQLITE_ROW, "Missing test game");
        const std::string result = reinterpret_cast<const char*>(sqlite3_column_text(query, 0));
        sqlite3_finalize(query);
        return result;
    }
    void core(std::int64_t id, const std::string& value)
    {
        sqlite3_stmt* query = nullptr;
        require(sqlite3_prepare_v2(db, "UPDATE games SET core=? WHERE id=?", -1, &query, nullptr) == SQLITE_OK,
                "Cannot prepare legacy core fixture");
        sqlite3_bind_text(query, 1, value.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(query, 2, id);
        require(sqlite3_step(query) == SQLITE_DONE, "Cannot write legacy core fixture");
        sqlite3_finalize(query);
    }
    std::vector<std::vector<std::string>> metadata()
    {
        sqlite3_stmt* query = nullptr;
        require(sqlite3_prepare_v2(db,
                                  "SELECT id,path,system,favorite,last_played,save_id,save_name,archive_member,playtime FROM games ORDER BY id",
                                  -1, &query, nullptr) == SQLITE_OK,
                "Cannot prepare metadata snapshot");
        std::vector<std::vector<std::string>> result;
        while (sqlite3_step(query) == SQLITE_ROW)
        {
            std::vector<std::string> row;
            for (int column = 0; column < sqlite3_column_count(query); ++column)
            {
                row.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(query, column)));
            }
            result.push_back(std::move(row));
        }
        sqlite3_finalize(query);
        return result;
    }

private:
    sqlite3* db = nullptr;
};

static void child(const std::string& phase, const fs::path& directory)
{
    const auto profile = directory / "profile/library.sqlite3";
    const auto& system = systems()[system_index("gb")];
    const auto default_core = bundled_core(system);
    auto alternative_system = system;
    alternative_system.core = "mgba";
    const auto alternative = bundled_core(alternative_system);
    const auto external = directory / "external" / default_core.filename();
    const auto missing_external = directory / "missing/share/cabinet/cores" / default_core.filename();
    Library library(profile);
    CoreDatabase database(profile);
    if (phase == "import")
    {
        const auto rom = directory / "game.gbc";
        require(fs::is_regular_file(default_core), "Installed core discovery failed");
        const auto id = library.add({0, rom.string(), "Default", "gb", default_core.string()});
        require(database.core(id) == "bundled:gambatte", "New import persisted an installation path");
        require(library.game(id)->core == default_core.string(), "Home did not receive a usable core path");
        auto game = *library.game(id);
        game.title = "Edited default";
        game.favorite = true;
        library.update(game);
        library.played(id);
        require(database.core(id) == "bundled:gambatte", "Editing replaced the core identifier with a path");
        // Model a library written by an older Cabinet version, including a
        // manually selected bundled core that differs from the system default.
        game.path = (directory / "legacy.gbc").string();
        game.title = "Legacy";
        const auto legacy = library.add(game);
        database.core(legacy, default_core.string());
        game.path = (directory / "alternative.gbc").string();
        game.title = "Alternative";
        game.core = alternative.string();
        const auto alternate_id = library.add(game);
        database.core(alternate_id, alternative.string());
        game.path = (directory / "external.gbc").string();
        game.title = "External";
        game.core = external.string();
        const auto external_id = library.add(game);
        require(database.core(external_id) == external.string(), "External core was converted by filename");
        game.path = (directory / "missing.gbc").string();
        game.title = "Missing external";
        game.core = missing_external.string();
        library.add(game);
        return;
    }
    const auto games = library.games();
    require(games.size() == 5, "Loading changed library entries");
    for (const auto& game : games)
    {
        const auto reference = database.core(game.id);
        if (game.title == "External" || game.title == "Missing external")
        {
            const auto expected = game.title == "External" ? external : missing_external;
            require(game.core == expected.string() && reference == expected.string(), "Loading replaced an external core");
        }
        else
        {
            const bool alternate = game.title == "Alternative";
            if (game.title == "Edited default")
            {
                require(reference == "bundled:gambatte", "Loading changed the stored core identifier");
            }
            else
            {
                require(reference.find(".mount_CabinetOld") != std::string::npos, "Loading rewrote a stored core path");
            }
            require(game.core == (alternate ? alternative : default_core).string(), "Core did not resolve in the new installation");
            require(fs::is_regular_file(game.core), "Resolved core does not exist");
        }
        require(library.game(game.id)->core == game.core, "List and single-game lookups resolved different cores");
        auto edited = game;
        edited.title += " edited";
        library.update(edited);
        auto expected_reference = reference;
        if (game.title != "External" && game.title != "Missing external")
        {
            expected_reference = game.title == "Alternative" ? "bundled:mgba" : "bundled:gambatte";
        }
        require(database.core(game.id) == expected_reference, "Metadata edit did not preserve core selection");
    }
    if (phase == "relocated")
    {
        const auto first = library.games().front();
        auto edited = first;
        edited.core = external.string();
        library.update(edited);
        require(database.core(edited.id) == external.string(), "Selecting an external core retained bundled selection");
        edited.core = default_core.string();
        library.update(edited);
        require(database.core(edited.id) == "bundled:gambatte", "Selecting a bundled core retained external selection");
    }
}

static void run_child(const fs::path& executable, const char* phase, const fs::path& directory)
{
    const auto executable_text = executable.string();
    const auto directory_text = directory.string();
    const char* arguments[] = {executable_text.c_str(), phase, directory_text.c_str(), nullptr};
    auto process = SDL_CreateProcess(arguments, false);
    require(process != nullptr, SDL_GetError());
    int status = -1;
    const bool waited = SDL_WaitProcess(process, true, &status);
    SDL_DestroyProcess(process);
    require(waited && status == 0, "Core persistence child failed");
}

int main(int argc, char** argv)
{
    try
    {
        require(SDL_Init(0), SDL_GetError());
        if (argc == 3)
        {
            child(argv[1], argv[2]);
        }
        else
        {
            require(argc == 1, "Unexpected arguments");
            TestDirectory scratch("cabinet core paths");
            const auto directory = scratch.path;
            const auto old_installation = directory / ".mount_CabinetOld/usr";
            const auto new_installation = directory / "native installation/usr";
            const auto& system = systems()[system_index("gb")];
            // Also exercise the ordinary development/build-tree path, before
            // launching the copies laid out as installed Cabinet executables.
            const auto database_path = directory / "development.sqlite3";
            const auto rom = directory / "game.gbc";
            std::ofstream(rom) << "fixture";
            {
                Library library(database_path);
                const auto core = bundled_core(system).string();
                const auto id = library.add({0, rom.string(), "Development", "gb", core});
                CoreDatabase database(database_path);
                require(database.core(id) == "bundled:gambatte", "Development import did not use a stable core reference");
                require(library.game(id)->core == core, "Development core path changed");
            }
            for (const auto& installation : {old_installation, new_installation})
            {
                fs::create_directories(installation / "bin");
                fs::create_directories(installation / "share/cabinet/cores");
                fs::copy_file(fs::path(SDL_GetBasePath()) / fs::path(argv[0]).filename(), installation / "bin/core_paths_test");
                std::ofstream(installation / "share/cabinet/cores" / bundled_core(system).filename()) << "core fixture";
                auto alternative = system;
                alternative.core = "mgba";
                std::ofstream(installation / "share/cabinet/cores" / bundled_core(alternative).filename()) << "alternative fixture";
            }
            fs::create_directories(directory / "external");
            std::ofstream(directory / "external" / bundled_core(system).filename()) << "external fixture";
            for (const auto* name : {"legacy", "alternative", "external", "missing"})
            {
                std::ofstream(directory / (std::string(name) + ".gbc")) << "fixture";
            }
            run_child(old_installation / "bin/core_paths_test", "import", directory);
            std::vector<std::vector<std::string>> before;
            {
                CoreDatabase database(directory / "profile/library.sqlite3");
                before = database.metadata();
                require(database.core(2).find(".mount_CabinetOld") != std::string::npos,
                        "Legacy fixture lost its original mount path before relocation");
            }
            fs::remove_all(old_installation.parent_path());
            run_child(new_installation / "bin/core_paths_test", "relocated", directory);
            {
                CoreDatabase database(directory / "profile/library.sqlite3");
                require(database.metadata() == before, "Core resolution or edit changed game identity, saves, or history");
            }
        }
        SDL_Quit();
        std::cout << "Core persistence and relocation passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
