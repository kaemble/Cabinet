#include "library.h"
#include "save_storage.h"
#include <sqlite3.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
namespace fs = std::filesystem;
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected scratch directory");
        fs::path dir = fs::path(argv[1]) / ("library-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(dir / "other");
        const auto ds3 = system_index("3ds");
        require(ds3 >= 0 && systems()[ds3].name == "Nintendo 3DS" && systems()[ds3].core == "azahar" && !systems()[ds3].memory_content,
            "Nintendo 3DS catalog missing or incorrectly accepts ZIP content");
        for (const auto* ext : {"3ds","3dsx","cci","cxi","z3dsx","zcci","zcxi","app","axf"}) {
            const auto candidates = identify_systems(std::string("game.") + ext);
            require(std::find(candidates.begin(),candidates.end(),ds3) != candidates.end(), "Nintendo 3DS format missing");
        }
        const auto dc = system_index("dc");
        require(dc >= 0 && systems()[dc].name == "Sega Dreamcast" && systems()[dc].core == "flycast", "Dreamcast catalog missing");
        require(identify_systems("disc.GDI") == std::vector<int>{dc}, "Dreamcast GDI detection failed");
        for (const auto* extension : {"cdi", "cue", "chd", "m3u"}) {
            const auto candidates = identify_systems(std::string("disc.") + extension);
            require(std::find(candidates.begin(), candidates.end(), dc) != candidates.end(), "Dreamcast disc format missing");
        }
        {
            const auto path = dir / "unsupported.sqlite3";
            sqlite3* old = nullptr;
            require(sqlite3_open(path.string().c_str(), &old) == SQLITE_OK, "Cannot create old schema fixture");
            require(sqlite3_exec(old, "PRAGMA user_version=4", nullptr, nullptr, nullptr) == SQLITE_OK,
                    "Cannot set old schema version");
            sqlite3_close(old);
            bool rejected = false;
            try { Library unsupported(path); } catch (const std::exception&) { rejected = true; }
            require(rejected, "Historical schemas must be rejected without migration");
        }
        auto rom = dir / "Same name.gbc", second = dir / "other" / "Same name.gbc";
        std::ofstream(rom) << "test fixture"; std::ofstream(second) << "different fixture";
        int64_t first_id;
        {
            Library library(dir / "library.sqlite3");
            require(library.session_count() == 0, "New library session count is not zero");
            for (int i = 0; i < 3; ++i) library.record_session();
            require(library.session_count() == 3, "Session count did not reach hint limit");
            Game first{0, rom.string(), "Zelda", "gb", bundled_core(systems()[system_index("gb")]).string()};
            first_id = library.add(first);
            const auto stored = library.game(first_id);
            require(stored && stored->id == first_id && stored->title == first.title && stored->path == first.path &&
                    stored->system == first.system && stored->core == first.core && !stored->save_id.empty() && !stored->save_name.empty(),
                    "Single-game lookup must include persisted metadata");
            require(!library.game(-1), "Single-game lookup must report a missing ID");
            require(library.add(first) == first_id && library.games().size() == 1, "Repeated imports must not duplicate paths");
            first.path = second.string(); first.title = "Another game";
            auto second_id = library.add(first);
            require(second_id != first_id && library.games().size() == 2, "Same filename in different folders must remain distinct");
            auto games = library.games();
            auto edited = games.back();
            edited.favorite = true; edited.title = "Link's Adventure"; library.update(edited);
            library.played(first_id);
            const auto played = library.game(first_id);
            require(played && played->last_played > 0 && played->favorite && played->title == edited.title,
                    "Single-game lookup must reflect updated details and launch time");
            games = library.games();
            require(filter_games(games, "gb", LibraryFilter::Favorites, "LINK").size() == 1, "Favorites and case-insensitive search must combine");
            require(filter_games(games, "gba", LibraryFilter::All, "").empty(), "System filtering must exclude other systems");
            require(filter_games(games, "", LibraryFilter::Recent, "").size() == 1, "Recent filter must include only launched games");
        }
        {
            Library reopened(dir / "library.sqlite3");
            require(reopened.games().size() == 2, "Library must persist across restarts");
            auto games = reopened.games();
            auto game = *std::find_if(games.begin(), games.end(), [&](const Game& g) { return g.id == first_id; });
            const auto stable_save_id = game.save_id;
            const auto stable_save_name = game.save_name;
            auto relocated = dir / "renamed.gbc";
            fs::rename(rom, relocated);
            game.path = relocated.string(); reopened.update(game);
            require(reopened.games().size() == 2, "Relocating must preserve library identity");
            require(reopened.games()[0].save_id == stable_save_id || reopened.games()[1].save_id == stable_save_id, "Relocating changed readable save ID");
            require(reopened.games()[0].save_name == stable_save_name || reopened.games()[1].save_name == stable_save_name, "Relocating changed save folder");
            reopened.remove(first_id);
            require(!reopened.game(first_id), "Single-game lookup must report a removed entry");
            require(fs::exists(relocated), "Removing from library must not delete the game file");
            require(reopened.games().size() == 1, "Remove must delete exactly one entry");
            require(identify_systems("GAME.GBC") == std::vector<int>{system_index("gb")}, "System detection must ignore extension case");
            for (const auto& [filename, system] : {std::pair{"game.cso", "psp"}, {"GAME.CSO", "psp"}, {"game.chd", "psp"},
                    {"game.unif", "nes"}, {"game.unf", "nes"}, {"game.wia", "gc"}, {"game.gcz", "gc"},
                    {"game.wia", "wii"}, {"game.toc", "saturn"}, {"game.img", "psx"}, {"game.bs", "snes"}, {"game.dmg", "gb"}}) {
                const auto candidates = identify_systems(filename);
                require(std::find(candidates.begin(), candidates.end(), system_index(system)) != candidates.end(),
                        "Core-supported game format missing from Home catalog");
            }
            for (const auto system : {"md", "sms", "gg"})
            {
                require(systems()[system_index(system)].core == "blastem", "Sega cartridges must default to BlastEm");
            }
            require(systems()[system_index("snes")].core == "bsnes", "SNES must default to bsnes");
            require(systems()[system_index("saturn")].core == "mednafen_saturn", "Saturn must default to Beetle Saturn");
            for (const auto& [filename, system] : {std::pair{"game.mds", "saturn"}, {"game.iso", "saturn"},
                                                 {"game.fig", "snes"}, {"game.swc", "snes"}, {"game.st", "snes"}})
            {
                const auto candidates = identify_systems(filename);
                require(std::find(candidates.begin(), candidates.end(), system_index(system)) == candidates.end(),
                        "Unsupported game format advertised for the default core");
            }
            require(identify_systems("disc.iso").size() > 1, "Ambiguous extensions must offer manual assignment");
            require(identify_systems("unknown.xyz").empty(), "Unknown formats must not be silently assigned");
        }
        { Library reopened(dir / "library.sqlite3");
          require(reopened.session_count() == 3, "Session count did not survive reopening or game removal"); }
        {
            Library collisions(dir / "collisions.sqlite3");
            const auto a = dir / "Name*.gba", b = dir / "Name?.gba";
            std::ofstream(a) << "a"; std::ofstream(b) << "b";
            const auto id = collisions.add({0,a.string(),"A","gba","core.so"});
            collisions.add({0,b.string(),"B","gba","core.so"});
            auto games = collisions.games();
            require(games[0].save_id.size() == 32 && games[0].save_id != games[1].save_id, "Game identities are not independent");
            require(games[0].save_name == "Name_" && games[1].save_name.starts_with("Name_ ["), "Readable folder collision was not disambiguated");
            const auto removed_identity = games[0].save_id;
            collisions.remove(id);
            collisions.add({0,a.string(),"C","gba","core.so"});
            require(collisions.games().back().save_id != removed_identity && collisions.games().back().save_name != "Name_", "Removed game save folder was reused");
        }
        {
            const auto profile = dir / "playtime";
            const auto database = profile / "library.sqlite3";
            int64_t identity;
            {
                Library library(database);
                identity = library.add({0, second.string(), "Timed game", "gb", "core.so"});
                const auto game = library.games().front();
                const auto manifest = profile / "saves" / "gb" / game.save_name / "cabinet-game.txt";
                save_storage::write(manifest, {{"game_id", game.save_id}, {"playtime", "90"}});
                const auto other_id = library.add({0, (dir / "renamed.gbc").string(), "Unplayed game", "gb", "core.so"});
                const auto other = library.games().back();
                save_storage::write(profile / "saves" / "gb" / other.save_name / "cabinet-game.txt",
                                    {{"game_id", other.save_id}, {"playtime", "75"}});
                library.sync_playtime(identity);
                library.sync_playtime(identity);
                require(library.games().back().id == other_id && library.games().back().playtime == 0,
                        "Single-game playtime sync updated another game");
                require(library.games().front().playtime == 90, "Playtime not synchronized idempotently");
                require(library.game(identity)->playtime == 90 && library.game(other_id)->playtime == 0,
                        "Single-game lookup must reflect playtime for only the completed game");
                library.update(game);
                require(library.games().front().playtime == 90, "Editing game erased playtime");
                save_storage::write(manifest, {{"game_id", game.save_id}, {"playtime", "100"}});
            }
            Library reopened(database);
            require(reopened.games().front().id == identity && reopened.games().front().playtime == 100, "Startup did not recover manifest playtime");
        }
        {
            const auto database = dir / "contentless.sqlite3";
            Game original;
            {
                Library library(database);
                const auto id = library.add({0, second.string(), "Existing game", "gb", "core.so"});
                original = *library.game(id);
            }
            sqlite3* old = nullptr;
            require(sqlite3_open(database.string().c_str(), &old) == SQLITE_OK, "Cannot open migration fixture");
            require(sqlite3_exec(old,
                "BEGIN; ALTER TABLE games RENAME TO new_games;"
                "CREATE TABLE games (id INTEGER PRIMARY KEY AUTOINCREMENT, path TEXT NOT NULL, title TEXT NOT NULL, system TEXT NOT NULL, core TEXT NOT NULL, favorite INTEGER NOT NULL DEFAULT 0, last_played INTEGER NOT NULL DEFAULT 0, save_id TEXT NOT NULL DEFAULT '', save_name TEXT NOT NULL DEFAULT '', archive_member TEXT NOT NULL DEFAULT '', playtime INTEGER NOT NULL DEFAULT 0 CHECK(playtime>=0), UNIQUE(path,archive_member));"
                "INSERT INTO games SELECT * FROM new_games; DROP TABLE new_games; PRAGMA user_version=5; COMMIT;",
                nullptr, nullptr, nullptr) == SQLITE_OK, "Cannot create version 5 library fixture");
            sqlite3_close(old);
            std::int64_t first_id, second_id;
            {
                Library library(database);
                const auto migrated = library.game(original.id);
                require(migrated && migrated->path == original.path && migrated->save_id == original.save_id &&
                        migrated->save_name == original.save_name, "Migration changed existing game identity or saves");
                Game first{0, "", "First core", "generic", "first_libretro.so"};
                first_id = library.add(first);
                second_id = library.add({0, "", "Second core", "generic", "second_libretro.so"});
                require(first_id != second_id && library.add(first) == first_id && library.games().size() == 3,
                        "Contentless entries must deduplicate by core and coexist with ROM entries");
                auto edited = *library.game(first_id);
                edited.title = "Renamed core";
                edited.favorite = true;
                library.update(edited);
                require(library.game(first_id)->path.empty() && library.game(first_id)->favorite,
                        "Editing contentless details changed the empty ROM path");
            }
            Library reopened(database);
            const auto first = reopened.game(first_id), second_core = reopened.game(second_id);
            require(first && second_core && first->path.empty() && second_core->path.empty() &&
                    !first->save_name.empty() && first->save_name != second_core->save_name &&
                    first->save_id != second_core->save_id,
                    "Contentless entries must retain independent saves after reopening");
        }
        std::cout << "Library persistence, imports, filters, relocation, and removal passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
