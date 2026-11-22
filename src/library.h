#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;
struct SystemInfo
{
    std::string id, name, short_name, core, core_name;
    std::vector<std::string> extensions;
    bool memory_content = false;
};
const std::vector<SystemInfo>& systems();
int system_index(const std::string& id);
std::vector<int> identify_systems(const std::filesystem::path& path);
std::filesystem::path bundled_core(const SystemInfo& system);

struct Game
{
    std::int64_t id = 0;
    std::string path, title, system, core;
    bool favorite = false;
    std::int64_t last_played = 0;
    std::string save_id, save_name, archive_member;
    std::int64_t playtime = 0;
};

// Match the database's ORDER BY title COLLATE NOCASE,id for cached entries.
bool game_title_less(const Game& a, const Game& b);

class Library
{
public:
    explicit Library(const std::filesystem::path& path);
    ~Library();
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    std::vector<Game> games() const;
    std::optional<Game> game(std::int64_t id) const;
    std::int64_t add(Game game);
    void update(const Game& game);
    void remove(std::int64_t id);
    void set_favorite(std::int64_t id, bool favorite);
    void played(std::int64_t id);
    void sync_playtime();
    void sync_playtime(std::int64_t id);
    std::int64_t session_count() const;
    void record_session();

private:
    void sync_playtime(const Game& game);
    sqlite3* db = nullptr;
    std::filesystem::path core_cache;
};

enum class LibraryFilter
{
    All,
    Favorites,
    Recent
};
std::vector<Game> filter_games(const std::vector<Game>& games, const std::string& system, LibraryFilter filter,
                               const std::string& query);
