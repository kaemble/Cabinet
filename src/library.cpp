#include "library.h"
#include <cstdint>
#include <cstddef>
#include "system_names.h"
#include "save_storage.h"
#include "playtime.h"
#include "rom_archive.h"
#include <sqlite3.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string_view>

namespace fs = std::filesystem;
const std::vector<SystemInfo>& systems()
{
    static const std::vector<SystemInfo> catalog = []
    {
        std::vector<SystemInfo> entries{
            {"gb", system_display_name("gb"), "GB / GBC", "gambatte", "Gambatte", {"gb", "gbc", "dmg"}, true},
            {"dc", system_display_name("dc"), "DC", "flycast", "Flycast", {"gdi", "cdi", "chd", "cue", "m3u", "elf"}},
            {"gba", system_display_name("gba"), "GBA", "mgba", "mGBA", {"gba"}, true},
            {"gc",
             system_display_name("gc"),
             "GC",
             "dolphin",
             "Dolphin",
             {"iso", "gcm", "rvz", "gcz", "ciso", "wia", "tgc", "elf", "dol", "m3u"}},
            {"wii",
             system_display_name("wii"),
             "Wii",
             "dolphin",
             "Dolphin",
             {"iso", "rvz", "wbfs", "wad", "ciso", "gcz", "wia", "elf", "dol", "m3u"}},
            {"gg", system_display_name("gg"), "GG", "blastem", "BlastEm", {"gg"}, true},
            {"nes", system_display_name("nes"), "NES", "fceumm", "FCEUmm", {"nes", "fds", "unf", "unif"}, true},
            {"n64",
             system_display_name("n64"),
             "N64",
             "mupen64plus_next",
             "Mupen64Plus-Next",
             {"n64", "z64", "v64", "bin", "u1"},
             true},
            {"3ds",
             system_display_name("3ds"),
             "3DS",
             "azahar",
             "Azahar",
             {"3ds", "3dsx", "app", "axf", "cci", "cxi", "elf", "z3dsx", "zcci", "zcxi"}},
            {"nds", system_display_name("nds"), "NDS", "melondsds", "melonDS DS", {"nds", "ids", "dsi"}, true},
            {"md", system_display_name("md"), "MD", "blastem", "BlastEm", {"md", "gen", "smd", "bin", "rom"}, true},
            {"sms", system_display_name("sms"), "SMS", "blastem", "BlastEm", {"sms"}, true},
            {"saturn",
             system_display_name("saturn"),
             "SAT",
             "mednafen_saturn",
             "Beetle Saturn",
             {"cue", "ccd", "chd", "toc", "m3u"}},
            {"psp", system_display_name("psp"), "PSP", "ppsspp", "PPSSPP", {"iso", "cso", "pbp", "chd", "elf", "prx"}},
            {"psx",
             system_display_name("psx"),
             "PS1",
             "swanstation",
             "SwanStation",
             {"cue", "chd", "m3u", "pbp", "bin", "img", "iso", "ecm", "mds", "exe", "psexe"}},
            {"snes",
             system_display_name("snes"),
             "SNES",
             "bsnes",
             "bsnes",
             {"sfc", "smc", "bs"},
             true},
            {"pce",
             system_display_name("pce"),
             "PCE",
             "mednafen_pce",
             "Mednafen PCE",
             {"pce", "cue", "chd", "sgx", "ccd", "toc", "m3u"}},
        };
        std::sort(entries.begin(), entries.end(),
                  [](const SystemInfo& a, const SystemInfo& b)
                  {
                      return SDL_strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
                  });
        // Keep the custom category last so Home can hide it when empty.
        entries.push_back({"generic", "Others", "Others", "", "", {}});
        return entries;
    }();
    return catalog;
}
int system_index(const std::string& id)
{
    for (std::size_t i = 0; i < systems().size(); ++i)
    {
        if (systems()[i].id == id)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}
static std::string lower(std::string s)
{
    for (char& c : s)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}
std::vector<int> identify_systems(const fs::path& path)
{
    auto extension = lower(path.extension().string());
    if (!extension.empty())
    {
        extension.erase(0, 1);
    }
    std::vector<int> result;
    for (std::size_t i = 0; i < systems().size(); ++i)
    {
        const auto& exts = systems()[i].extensions;
        if (std::find(exts.begin(), exts.end(), extension) != exts.end())
        {
            result.push_back(static_cast<int>(i));
        }
    }
    return result;
}
static fs::path bundled_core_path(const std::string& name)
{
    const auto filename = name + "_libretro" + CABINET_CORE_SUFFIX;
    if (const char* base = SDL_GetBasePath())
    {
        auto installed = fs::path(base) / CABINET_DATA_SUBDIR / "cores" / filename;
        if (fs::is_regular_file(installed))
        {
            return fs::weakly_canonical(installed);
        }
    }
    return fs::path(CABINET_LOCKED_CORES_DIR) / filename;
}
fs::path bundled_core(const SystemInfo& system)
{
    if (system.core.empty())
    {
        return {};
    }
    return bundled_core_path(system.core);
}

namespace
{
constexpr std::string_view bundled_core_prefix = "bundled:";

bool valid_core_name(std::string_view name)
{
    return !name.empty() && std::all_of(name.begin(), name.end(),
                                      [](unsigned char character)
                                      {
                                          return std::isalnum(character) || character == '_' || character == '-';
                                      });
}

std::string core_reference(const std::string& core)
{
    const fs::path path(core);
    const std::string suffix = "_libretro" CABINET_CORE_SUFFIX;
    const auto filename = path.filename().string();
    if (!path.is_absolute() || !filename.ends_with(suffix))
    {
        return core;
    }
    const auto name = filename.substr(0, filename.size() - suffix.size());
    if (!valid_core_name(name))
    {
        return core;
    }
    const auto directory = path.parent_path();
    std::error_code error;
    const auto canonical_directory = fs::weakly_canonical(directory, error);
    bool bundled = false;
    // An inaccessible external directory should leave an actionable missing-core
    // error at launch, rather than preventing the whole library from loading.
    if (!error)
    {
        const auto build_directory = fs::weakly_canonical(fs::path(CABINET_LOCKED_CORES_DIR), error);
        bundled = !error && canonical_directory == build_directory;
        if (const char* base = SDL_GetBasePath())
        {
            const auto installed_directory = fs::weakly_canonical(fs::path(base) / CABINET_DATA_SUBDIR / "cores", error);
            bundled = bundled || (!error && canonical_directory == installed_directory);
        }
    }
    // Older libraries saved absolute paths into temporary AppImage mounts.
    // Recognize that layout even after unmounting, without guessing from an
    // external core's filename or substituting the system's default core.
    const auto installation = directory.parent_path().parent_path().parent_path();
    bundled = bundled || (directory.filename() == "cores" && directory.parent_path().filename() == "cabinet" &&
                          directory.parent_path().parent_path().filename() == "share" && installation.filename() == "usr" &&
                          installation.parent_path().filename().string().starts_with(".mount_"));
    return bundled ? std::string(bundled_core_prefix) + name : core;
}

std::string resolved_core(const std::string& reference)
{
    const auto normalized = core_reference(reference);
    if (normalized.starts_with(bundled_core_prefix))
    {
        const auto name = normalized.substr(bundled_core_prefix.size());
        if (valid_core_name(name))
        {
            return bundled_core_path(name).string();
        }
    }
    return reference;
}

struct Statement
{
    sqlite3_stmt* stmt = nullptr;
    sqlite3* db;
    Statement(sqlite3* db, const char* sql) : db(db)
    {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            throw std::runtime_error(sqlite3_errmsg(db));
        }
    }
    ~Statement()
    {
        sqlite3_finalize(stmt);
    }
    void text(int i, const std::string& s)
    {
        if (sqlite3_bind_text(stmt, i, s.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
        {
            throw std::runtime_error(sqlite3_errmsg(db));
        }
    }
    void number(int i, std::int64_t n)
    {
        sqlite3_bind_int64(stmt, i, n);
    }
    void finish()
    {
        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            throw std::runtime_error(sqlite3_errmsg(db));
        }
    }
};
void sql(sqlite3* db, const char* text)
{
    if (sqlite3_exec(db, text, nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        throw std::runtime_error(sqlite3_errmsg(db));
    }
}
void validate(const Game& g)
{
    if (g.title.empty() || system_index(g.system) < 0)
    {
        throw std::runtime_error("Choose a title and system first.");
    }
    if (g.path.empty())
    {
        throw std::runtime_error("Choose a game file first.");
    }
    if (g.system == "generic" && (g.core.empty() || !g.archive_member.empty()))
    {
        throw std::runtime_error("Choose a core for the custom file.");
    }
}

}

static std::string reserve_game_id(sqlite3* db)
{
    for (;;)
    {
        Statement random(db, "SELECT lower(hex(randomblob(16)))");
        if (sqlite3_step(random.stmt) != SQLITE_ROW)
        {
            throw std::runtime_error("Cannot allocate game identity");
        }
        const std::string id = reinterpret_cast<const char*>(sqlite3_column_text(random.stmt, 0));
        Statement reserve(db, "INSERT OR IGNORE INTO game_ids(system,save_id) VALUES('',?)");
        reserve.text(1, id);
        reserve.finish();
        if (sqlite3_changes(db))
        {
            return id;
        }
    }
}
static std::string reserve_save_name(sqlite3* db, const Game& game)
{
    const auto base = save_storage::folder_name(
        fs::path(game.archive_member.empty() ? game.path : game.archive_member).stem().string());
    for (unsigned attempt = 0;; ++attempt)
    {
        auto name = base;
        if (attempt)
        {
            name += " [" + game.save_id.substr(0, 8) + ((attempt > 1) ? "-" + std::to_string(attempt) : "") + "]";
        }
        Statement reserve(db, "INSERT OR IGNORE INTO save_names(system,name) VALUES(?,?)");
        reserve.text(1, game.system);
        reserve.text(2, name);
        reserve.finish();
        if (sqlite3_changes(db))
        {
            return name;
        }
    }
}

Library::Library(const fs::path& path) : core_cache(path.parent_path() / "core-info")
{
    if (!path.parent_path().empty())
    {
        fs::create_directories(path.parent_path());
    }
    const bool fresh = !fs::exists(path);
    if (sqlite3_open(path.string().c_str(), &db) != SQLITE_OK)
    {
        std::string error = sqlite3_errmsg(db);
        sqlite3_close(db);
        db = nullptr;
        throw std::runtime_error(error);
    }
    try
    {
        sqlite3_busy_timeout(db, 2000);
        {
            Statement version(db, "PRAGMA user_version");
            if (sqlite3_step(version.stmt) != SQLITE_ROW)
            {
                throw std::runtime_error("Cannot read library schema version.");
            }
            if (!fresh && sqlite3_column_int(version.stmt, 0) != 5)
            {
                throw std::runtime_error(
                    "Unsupported library schema version. Create a new library with this Cabinet version.");
            }
        }
        sql(db, "PRAGMA journal_mode=WAL;");
        if (fresh)
        {
            sql(db, "BEGIN IMMEDIATE");
            try
            {
                sql(db,
                    "CREATE TABLE session_stats (id INTEGER PRIMARY KEY CHECK(id=1), count INTEGER NOT NULL); INSERT INTO session_stats VALUES(1,0);");
                sql(db,
                    "CREATE TABLE games (id INTEGER PRIMARY KEY AUTOINCREMENT, path TEXT NOT NULL, title TEXT NOT NULL, system TEXT NOT NULL, core TEXT NOT NULL, favorite INTEGER NOT NULL DEFAULT 0, last_played INTEGER NOT NULL DEFAULT 0, save_id TEXT NOT NULL DEFAULT '', save_name TEXT NOT NULL DEFAULT '', archive_member TEXT NOT NULL DEFAULT '', playtime INTEGER NOT NULL DEFAULT 0 CHECK(playtime>=0), UNIQUE(path,archive_member));");
                sql(db,
                    "CREATE TABLE game_ids (system TEXT NOT NULL, save_id TEXT NOT NULL, PRIMARY KEY(system,save_id));");
                sql(db,
                    "CREATE TABLE save_names (system TEXT NOT NULL, name TEXT COLLATE NOCASE NOT NULL, PRIMARY KEY(system,name));");
                sql(db, "PRAGMA user_version=5; COMMIT");
            }
            catch (...)
            {
                sql(db, "ROLLBACK");
                throw;
            }
        }
        sync_playtime();
    }
    catch (...)
    {
        sqlite3_close(db);
        db = nullptr;
        throw;
    }
}
Library::~Library()
{
    sqlite3_close(db);
}
static Game read_game(sqlite3_stmt* row)
{
    auto text = [&](int column)
    {
        return std::string(reinterpret_cast<const char*>(sqlite3_column_text(row, column)));
    };
    return {sqlite3_column_int64(row, 0),
            text(1),
            text(2),
            text(3),
            resolved_core(text(4)),
            sqlite3_column_int(row, 5) != 0,
            sqlite3_column_int64(row, 6),
            text(7),
            text(8),
            text(9),
            sqlite3_column_int64(row, 10)};
}
bool game_title_less(const Game& a, const Game& b)
{
    const int title_order = sqlite3_stricmp(a.title.c_str(), b.title.c_str());
    return title_order < 0 || (title_order == 0 && a.id < b.id);
}
std::vector<Game> Library::games() const
{
    Statement q(
        db,
        "SELECT id,path,title,system,core,favorite,last_played,save_id,save_name,archive_member,playtime FROM games ORDER BY title COLLATE NOCASE,id");
    std::vector<Game> result;
    int code;
    while ((code = sqlite3_step(q.stmt)) == SQLITE_ROW)
    {
        result.push_back(read_game(q.stmt));
    }
    if (code != SQLITE_DONE)
    {
        throw std::runtime_error(sqlite3_errmsg(db));
    }
    return result;
}
std::optional<Game> Library::game(std::int64_t id) const
{
    Statement q(
        db,
        "SELECT id,path,title,system,core,favorite,last_played,save_id,save_name,archive_member,playtime FROM games WHERE id=?");
    q.number(1, id);
    const int code = sqlite3_step(q.stmt);
    if (code == SQLITE_DONE)
    {
        return std::nullopt;
    }
    if (code != SQLITE_ROW)
    {
        throw std::runtime_error(sqlite3_errmsg(db));
    }
    return read_game(q.stmt);
}
std::int64_t Library::add(Game game)
{
    validate(game);
    if (rom_archive::is_zip(game.path) && game.system != "generic")
    {
        if (game.archive_member.empty())
        {
            throw std::runtime_error("Choose a ROM inside the ZIP first");
        }
        const auto members = rom_archive::members(game.path);
        if (std::find(members.begin(), members.end(), game.archive_member) == members.end())
        {
            throw std::runtime_error("Selected ROM is missing from ZIP");
        }
        const auto candidates = identify_systems(game.archive_member);
        if (std::find(candidates.begin(), candidates.end(), system_index(game.system)) == candidates.end())
        {
            throw std::runtime_error("Selected ROM does not match this system");
        }
        rom_archive::validate_core(game.core, core_cache, game.system);
    }
    else if (!game.archive_member.empty())
    {
        throw std::runtime_error("This entry requires a ZIP archive");
    }
    if (!fs::is_regular_file(game.path))
    {
        throw std::runtime_error("Game file not found. Choose an existing file.");
    }
    game.path = fs::canonical(game.path).string();
    Statement find(db, "SELECT id FROM games WHERE path=? AND archive_member=?");
    find.text(1, game.path);
    find.text(2, game.archive_member);
    if (sqlite3_step(find.stmt) == SQLITE_ROW)
    {
        return sqlite3_column_int64(find.stmt, 0);
    }
    sql(db, "BEGIN IMMEDIATE");
    try
    {
        const auto save_id = reserve_game_id(db);
        game.save_id = save_id;
        const auto save_name = reserve_save_name(db, game);
        Statement q(
            db,
            "INSERT INTO games(path,title,system,core,favorite,save_id,save_name,archive_member) VALUES(?,?,?,?,?,?,?,?)");
        q.text(1, game.path);
        q.text(2, game.title);
        q.text(3, game.system);
        q.text(4, core_reference(game.core));
        q.number(5, game.favorite);
        q.text(6, save_id);
        q.text(7, save_name);
        q.text(8, game.archive_member);
        q.finish();
        const auto id = sqlite3_last_insert_rowid(db);
        sql(db, "COMMIT");
        return id;
    }
    catch (...)
    {
        sql(db, "ROLLBACK");
        throw;
    }
}
void Library::update(const Game& game)
{
    validate(game);
    sql(db, "BEGIN IMMEDIATE");
    try
    {
        std::string save_id, save_name, path;
        bool content_changed = false;
        {
            Statement existing(db, "SELECT system,save_id,save_name,path,archive_member,core FROM games WHERE id=?");
            existing.number(1, game.id);
            if (sqlite3_step(existing.stmt) != SQLITE_ROW)
            {
                throw std::runtime_error("Game not found");
            }
            save_id = reinterpret_cast<const char*>(sqlite3_column_text(existing.stmt, 1));
            if (game.system == reinterpret_cast<const char*>(sqlite3_column_text(existing.stmt, 0)))
            {
                save_name = reinterpret_cast<const char*>(sqlite3_column_text(existing.stmt, 2));
            }
            path = reinterpret_cast<const char*>(sqlite3_column_text(existing.stmt, 3));
            content_changed =
                game.path != path ||
                game.archive_member != reinterpret_cast<const char*>(sqlite3_column_text(existing.stmt, 4)) ||
                core_reference(game.core) != core_reference(reinterpret_cast<const char*>(sqlite3_column_text(existing.stmt, 5)));
        }
        if (content_changed)
        {
            if (rom_archive::is_zip(game.path) && game.system != "generic")
            {
                if (game.archive_member.empty())
                {
                    throw std::runtime_error("Choose a ROM inside the ZIP first");
                }
                const auto members = rom_archive::members(game.path);
                if (std::find(members.begin(), members.end(), game.archive_member) == members.end())
                {
                    throw std::runtime_error("Selected ROM is missing from ZIP");
                }
                const auto candidates = identify_systems(game.archive_member);
                if (std::find(candidates.begin(), candidates.end(), system_index(game.system)) == candidates.end())
                {
                    throw std::runtime_error("Selected ROM does not match this system");
                }
                rom_archive::validate_core(game.core, core_cache, game.system);
            }
            else if (!game.archive_member.empty())
            {
                throw std::runtime_error("This entry requires a ZIP archive");
            }
            path = fs::weakly_canonical(game.path).string();
        }
        if (save_name.empty())
        {
            auto copy = game;
            copy.save_id = save_id;
            save_name = reserve_save_name(db, copy);
        }
        Statement q(
            db,
            "UPDATE games SET path=?,title=?,system=?,core=?,favorite=?,save_id=?,save_name=?,archive_member=? WHERE id=?");
        q.text(1, path);
        q.text(2, game.title);
        q.text(3, game.system);
        q.text(4, core_reference(game.core));
        q.number(5, game.favorite);
        q.text(6, save_id);
        q.text(7, save_name);
        q.text(8, game.archive_member);
        q.number(9, game.id);
        q.finish();
        sql(db, "COMMIT");
    }
    catch (...)
    {
        sql(db, "ROLLBACK");
        throw;
    }
}
void Library::set_favorite(std::int64_t id, bool favorite)
{
    Statement update(db, "UPDATE games SET favorite=? WHERE id=?");
    update.number(1, favorite);
    update.number(2, id);
    update.finish();
}
void Library::remove(std::int64_t id)
{
    Statement q(db, "DELETE FROM games WHERE id=?");
    q.number(1, id);
    q.finish();
}
void Library::sync_playtime()
{
    for (const auto& game : games())
    {
        sync_playtime(game);
    }
}
void Library::sync_playtime(std::int64_t id)
{
    Statement query(db, "SELECT system,save_name,save_id,playtime FROM games WHERE id=?");
    query.number(1, id);
    const int result = sqlite3_step(query.stmt);
    if (result == SQLITE_DONE)
    {
        return;
    }
    if (result != SQLITE_ROW)
    {
        throw std::runtime_error(sqlite3_errmsg(db));
    }
    Game game;
    game.id = id;
    game.system = reinterpret_cast<const char*>(sqlite3_column_text(query.stmt, 0));
    game.save_name = reinterpret_cast<const char*>(sqlite3_column_text(query.stmt, 1));
    game.save_id = reinterpret_cast<const char*>(sqlite3_column_text(query.stmt, 2));
    game.playtime = sqlite3_column_int64(query.stmt, 3);
    sync_playtime(game);
}
void Library::sync_playtime(const Game& game)
{
    const auto manifest = core_cache.parent_path() / "saves" / game.system / game.save_name / "cabinet-game.txt";
    const auto metadata = save_storage::read(manifest);
    if (!metadata.contains("game_id") || metadata.at("game_id") != game.save_id || !metadata.contains("playtime"))
    {
        return;
    }
    const auto seconds = parse_playtime(metadata.at("playtime"));
    if (!seconds || *seconds <= game.playtime)
    {
        return;
    }
    Statement update(db, "UPDATE games SET playtime=? WHERE id=?");
    update.number(1, *seconds);
    update.number(2, game.id);
    update.finish();
}
void Library::played(std::int64_t id)
{
    Statement q(db, "UPDATE games SET last_played=unixepoch() WHERE id=?");
    q.number(1, id);
    q.finish();
}
std::vector<Game> filter_games(const std::vector<Game>& games, const std::string& system, LibraryFilter filter,
                               const std::string& query)
{
    std::vector<Game> result;
    const auto normalized_query = lower(query);
    for (const auto& game : games)
    {
        if (!system.empty() && game.system != system)
        {
            continue;
        }
        if (filter == LibraryFilter::Favorites && !game.favorite)
        {
            continue;
        }
        if (filter == LibraryFilter::Recent && !game.last_played)
        {
            continue;
        }
        if (lower(game.title).find(normalized_query) == std::string::npos)
        {
            continue;
        }
        result.push_back(game);
    }
    if (filter == LibraryFilter::Recent)
    {
        std::stable_sort(result.begin(), result.end(),
                         [](const Game& a, const Game& b)
                         {
                             return a.last_played > b.last_played;
                         });
    }
    return result;
}

std::int64_t Library::session_count() const
{
    Statement query(db, "SELECT count FROM session_stats WHERE id=1");
    if (sqlite3_step(query.stmt) != SQLITE_ROW)
    {
        throw std::runtime_error(sqlite3_errmsg(db));
    }
    return sqlite3_column_int64(query.stmt, 0);
}
void Library::record_session()
{
    sql(db, "UPDATE session_stats SET count=count+1 WHERE id=1");
}
