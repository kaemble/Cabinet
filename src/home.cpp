#include "home.h"
#include "boxart.h"
#include <cstdint>
#include <cstddef>
#include "cli_help.h"
#include "app_paths.h"
#include "library.h"
#include "firmware.h"
#include "game_process.h"
#include "rom_archive.h"
#include "save_storage.h"
#include "ui_text.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <future>
#include <memory>
#include <stdexcept>
#include <iostream>

namespace fs = std::filesystem;
namespace
{
constexpr SDL_Color ink{255, 255, 255, 255}, muted{187, 187, 187, 255}, accent{123, 147, 203, 255};
enum class Modal
{
    None,
    Details,
    Edit,
    Browse,
    Bios,
    Remove,
    Discard
};
enum class BrowsePurpose
{
    Import,
    Locate,
    Core,
    Bios,
    Archive,
    BoxArt
};
struct Entry
{
    fs::path path;
    bool directory;
};
struct Listing
{
    std::vector<Entry> entries;
    std::string error;
};
struct Hit
{
    SDL_FRect rect;
    int kind, index;
};

std::string format_playtime(std::int64_t seconds)
{
    if (seconds == 0)
    {
        return "0 minutes";
    }
    if (seconds < 60)
    {
        return "Less than a minute";
    }
    const auto hours = seconds / 3600;
    const auto minutes = (seconds / 60) % 60;
    std::string result;
    if (hours)
    {
        result = std::to_string(hours) + ((hours == 1) ? " hour" : " hours");
    }
    if (minutes)
    {
        if (!result.empty())
        {
            result += ' ';
        }
        result += std::to_string(minutes) + ((minutes == 1) ? " minute" : " minutes");
    }
    return result;
}

const char* browser_folder_key(BrowsePurpose purpose)
{
    if (purpose == BrowsePurpose::BoxArt)
    {
        return "boxart";
    }
    if (purpose == BrowsePurpose::Core)
    {
        return "core";
    }
    if (purpose == BrowsePurpose::Import || purpose == BrowsePurpose::Locate)
    {
        return "rom";
    }
    return nullptr;
}

std::string browser_initial(const Entry& entry, BrowsePurpose purpose)
{
    const auto name =
        (purpose == BrowsePurpose::Archive) ? entry.path.generic_string() : entry.path.filename().string();
    const char* end = name.c_str();
    SDL_StepUTF8(&end, nullptr);
    return std::string(name.c_str(), end);
}

class Home
{
public:
    Home(SDL_Window* window, SDL_Renderer* renderer, const fs::path& data, std::string executable,
         HomeLogger logger = {})
        : window(window), renderer(renderer), text(renderer), grid_titles(renderer, TextRenderer::Font::Condensed),
          boxart(renderer, data), library(data / "library.sqlite3"), process(logger), logger(logger), executable(std::move(executable)),
          data_dir(data.string())
    {
        all = library.games();
        refilter();
        if (!discover())
        {
            notify("Controller not detected. Keyboard and mouse are ready.");
        }
        logger.write(HomeLogLevel::Debug, "Library data: " + data_dir);
        logger.write(HomeLogLevel::Debug, "Runner executable: " + this->executable);
        logger.write(HomeLogLevel::Debug, "Loaded " + std::to_string(all.size()) + " library entries");
    }
    ~Home()
    {
        if (pad)
        {
            SDL_CloseGamepad(pad);
        }
    }
    void event(SDL_Event event);
    void tick();
    int wait_timeout() const;
    bool needs_redraw = true;
    void controller_input(bool focused);
    void draw();
    struct Layout
    {
        float width = 1200, height = 800, side = 248, left = 280, content = 888;
        float gap = 18, bar_height = 44, header_height = 60, toolbar_height = 44, grid_top = 140;
        float card_width = 224, card_height = 224, row_height = 49, system_height = 32, systems_top = 220;
        int body = 18, small = 16, heading = 32;
        int columns = 3, grid_rows = 2, list_rows = 8;
    } layout;
    void update_layout();
    void import_file(const fs::path& path);
    void select_system(const std::string& id);
    void report_error(const std::string& error)
    {
        notify(error);
        logger.write(HomeLogLevel::Error, error);
    }
    bool running = true;

private:
    SDL_Window* window;
    SDL_Renderer* renderer;
    TextRenderer text;
    TextRenderer grid_titles;
    BoxArt boxart;
    Library library;
    GameProcess process;
    HomeLogger logger;
    std::string executable, data_dir, message, query;
    std::uint64_t message_until = 0;
    std::vector<Game> all, visible;
    std::vector<Hit> hits;
    int sidebar = 0, selected = 0, focus = 0, toolbar = 0, first_row = 0;
    bool list_view = false, search = false, typing_title = false;
    Modal modal = Modal::None;
    int choice = 0;
    Game draft;
    bool bios_for_import = false, bios_installed = false;
    std::string bios_system;
    bool editing = false, dirty = false, exit_after_discard = false;
    BrowsePurpose purpose = BrowsePurpose::Import;
    fs::path folder = fs::current_path();
    save_storage::Metadata browser_folders = save_storage::read(fs::path(data_dir) / "browser.conf");
    std::vector<Entry> entries;
    std::future<Listing> listing;
    bool browsing = false;
    int browse_selected = 0, browse_top = 0;
    float sidebar_scroll = 0;
    int sidebar_tracked = -1;
    float sidebar_view_height = 0;
    SDL_Gamepad* pad = nullptr;
    std::array<bool, 7> previous{};
    bool controller_blocked = true;
    std::uint64_t repeat_at = 0;
    int repeat_direction = 0;
    std::int64_t launched_id = 0;
    bool launched_with_controller = false;
    bool played_recorded = false;
    void cache_game(const Game& changed);
    void refresh(std::int64_t id);
    void refilter(std::int64_t preserve = 0);
    void notify(std::string value)
    {
        needs_redraw = true;
        message = std::move(value);
        message_until = SDL_GetTicks() + 7000;
    }
    bool discover();
    void block_controller()
    {
        controller_blocked = true;
        previous.fill(true);
        repeat_direction = 0;
        repeat_at = 0;
    }
    void navigate(int dx, int dy);
    void activate(bool with_controller = false);
    void back();
    void start_browse(BrowsePurpose intent);
    void remember_folder(BrowsePurpose intent, const fs::path& path);
    void read_folder(const fs::path& path);
    void choose_file(const fs::path& path);
    void edit_game();
    void finish_import();
    void set_system(int direction);
    void stop_text();
    bool can_clear_search() const
    {
        return modal == Modal::None && !query.empty() && (search || (focus == 2 && toolbar == 1));
    }
    void clear_search()
    {
        if (can_clear_search())
        {
            query.clear();
            refilter();
        }
    }
    void close_editor();
    Game* game()
    {
        return visible.empty() ? nullptr : &visible[static_cast<std::size_t>(selected)];
    }
    std::vector<std::string> modal_rows();
    void box(SDL_FRect rect, SDL_Color color);
    void label(float x, float y, const std::string& value, int size = 16, SDL_Color color = ink, float max = 10000)
    {
        text.draw(x, y, value, size, color, max);
    }
    void button(SDL_FRect rect, const std::string& title, bool active, int kind, int index,
                const std::string& icon = {});
    void draw_modal();
};

void Home::cache_game(const Game& changed)
{
    const auto existing = std::find_if(all.begin(), all.end(),
                                       [&](const Game& entry)
                                       {
                                           return entry.id == changed.id;
                                       });
    if (existing != all.end())
    {
        if (existing->title == changed.title)
        {
            *existing = changed;
            return;
        }
        all.erase(existing);
    }
    all.insert(std::lower_bound(all.begin(), all.end(), changed, game_title_less), changed);
}
void Home::refresh(std::int64_t id)
{
    if (const auto changed = library.game(id))
    {
        cache_game(*changed);
    }
    else
    {
        std::erase_if(all,
                      [&](const Game& entry)
                      {
                          return entry.id == id;
                      });
    }
    refilter(id);
}
void Home::refilter(std::int64_t preserve)
{
    needs_redraw = true;
    if (sidebar < 3 && toolbar == 0)
    {
        toolbar = 1;
    }
    if (!preserve && game())
    {
        preserve = game()->id;
    }
    auto filter = (sidebar == 1)   ? LibraryFilter::Favorites
                  : (sidebar == 2) ? LibraryFilter::Recent
                                   : LibraryFilter::All;
    visible = filter_games(all, (sidebar >= 3) ? systems()[sidebar - 3].id : "", filter, query);
    selected = 0;
    for (std::size_t i = 0; i < visible.size(); ++i)
    {
        if (visible[i].id == preserve)
        {
            selected = static_cast<int>(i);
        }
    }
    first_row = 0;
}
bool Home::discover()
{
    if (pad)
    {
        return false;
    }
    int count = 0;
    auto ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count && !pad; ++i)
    {
        pad = SDL_OpenGamepad(ids[i]);
    }
    SDL_free(ids);
    block_controller();
    return pad != nullptr;
}
void Home::stop_text()
{
    SDL_StopTextInput(window);
    search = typing_title = false;
}
void Home::remember_folder(BrowsePurpose intent, const fs::path& path)
{
    if (const auto* key = browser_folder_key(intent); key && browser_folders[key] != path.string())
    {
        std::error_code ec;
        if (fs::is_directory(path, ec))
        {
            browser_folders[key] = path.string();
            try
            {
                save_storage::write(fs::path(data_dir) / "browser.conf", browser_folders);
            }
            catch (const std::exception& error)
            {
                logger.write(HomeLogLevel::Warn, error.what());
            }
        }
    }
}
void Home::read_folder(const fs::path& path)
{
    if (browsing)
    {
        return;
    }
    folder = path;
    browsing = true;
    browse_selected = browse_top = 0;
    entries.clear();
    remember_folder(purpose, path);
    listing = std::async(
        std::launch::async,
        [path, images_only = purpose == BrowsePurpose::BoxArt]
        {
            Listing result;
            try
            {
                if (path.has_parent_path() && path.parent_path() != path)
                {
                    result.entries.push_back({path.parent_path(), true});
                }
                auto begin = result.entries.size();
                for (const auto& entry : fs::directory_iterator(path, fs::directory_options::skip_permission_denied))
                {
                    std::error_code ec;
                    if (entry.is_directory(ec) || (entry.is_regular_file(ec) && (!images_only || boxart_image(entry.path()))))
                    {
                        result.entries.push_back({entry.path(), entry.is_directory(ec)});
                    }
                    if (result.entries.size() >= 5000)
                    {
                        result.error = "Showing the first 5,000 entries. Choose a smaller folder.";
                        break;
                    }
                }
                std::sort(result.entries.begin() + static_cast<std::ptrdiff_t>(begin), result.entries.end(),
                          [](const Entry& a, const Entry& b)
                          {
                              if (a.directory != b.directory)
                              {
                                  return a.directory;
                              }
                              return SDL_strcasecmp(a.path.filename().string().c_str(),
                                                    b.path.filename().string().c_str()) < 0;
                          });
            }
            catch (const std::exception& error)
            {
                result.error = error.what();
            }
            return result;
        });
}
void Home::start_browse(BrowsePurpose intent)
{
    if (browsing)
    {
        return;
    }
    if (intent == BrowsePurpose::Import && sidebar < 3)
    {
        notify("Select a system before adding a game.");
        return;
    }
    stop_text();
    purpose = intent;
    modal = Modal::Browse;
    auto start = folder;
    if (intent == BrowsePurpose::Import)
    {
        start = fs::current_path();
    }
    if (intent == BrowsePurpose::Core && !draft.core.empty())
    {
        start = fs::path(draft.core).parent_path();
    }
    if (const auto* key = browser_folder_key(intent); key && browser_folders.contains(key))
    {
        std::error_code ec;
        if (fs::is_directory(browser_folders.at(key), ec))
        {
            start = browser_folders.at(key);
        }
    }
    std::error_code ec;
    if (!fs::is_directory(start, ec))
    {
        start = fs::current_path();
    }
    read_folder(start);
}
void Home::select_system(const std::string& id)
{
    const int index = system_index(id);
    if (index < 0)
    {
        throw std::runtime_error("Unknown system: " + id);
    }
    sidebar = index + 3;
    refilter();
}
void Home::import_file(const fs::path& path)
{
    if (sidebar < 3)
    {
        throw std::runtime_error("Select a system before adding a game.");
    }
    if (!fs::is_regular_file(path))
    {
        throw std::runtime_error("Choose an existing game file.");
    }
    stop_text();
    boxart.reset();
    draft = {};
    draft.path = fs::canonical(path).string();
    draft.title = path.stem().string();
    const auto& selected_system = systems()[sidebar - 3];
    draft.system = selected_system.id;
    draft.core = bundled_core(selected_system).string();
    remember_folder(BrowsePurpose::Import, fs::path(draft.path).parent_path());
    if (rom_archive::is_zip(path))
    {
        purpose = BrowsePurpose::Archive;
        modal = Modal::Browse;
        folder = path;
        browse_selected = browse_top = 0;
        entries.clear();
        browsing = true;
        listing =
            std::async(std::launch::async,
                       [path, selected = sidebar - 3]
                       {
                           Listing result;
                           try
                           {
                               for (const auto& member : rom_archive::members(path))
                               {
                                   const auto candidates = identify_systems(member);
                                   if (std::find(candidates.begin(), candidates.end(), selected) != candidates.end())
                                   {
                                       result.entries.push_back({member, false});
                                   }
                               }
                               std::stable_sort(result.entries.begin(), result.entries.end(),
                                                [](const Entry& a, const Entry& b)
                                                {
                                                    return SDL_strcasecmp(a.path.generic_string().c_str(),
                                                                          b.path.generic_string().c_str()) < 0;
                                                });
                               if (result.entries.empty())
                               {
                                   result.error = "ZIP contains no ROM files for the selected system.";
                               }
                           }
                           catch (const std::exception& error)
                           {
                               result.error = error.what();
                           }
                           return result;
                       });
        return;
    }
    const auto candidates = identify_systems(path);
    if (std::find(candidates.begin(), candidates.end(), sidebar - 3) == candidates.end())
    {
        throw std::runtime_error("This file format does not match the selected system.");
    }
    editing = false;
    dirty = false;
    modal = Modal::Edit;
    choice = 0;
    folder = fs::path(draft.path).parent_path();
}
void Home::choose_file(const fs::path& path)
{
    if (purpose == BrowsePurpose::Archive)
    {
        draft.archive_member = path.generic_string();
        draft.title = path.stem().string();
        editing = false;
        dirty = false;
        modal = Modal::Edit;
        choice = 0;
        folder = fs::path(draft.path).parent_path();
    }
    else if (purpose == BrowsePurpose::Bios)
    {
        const auto result = install_bios(bios_system, path, fs::path(data_dir) / "system");
        bios_installed = true;
        modal = Modal::Bios;
        choice = 1;
        notify(result);
    }
    else if (purpose == BrowsePurpose::Import)
    {
        import_file(path);
    }
    else if (purpose == BrowsePurpose::BoxArt)
    {
        boxart.select(path);
        dirty = true;
        modal = Modal::Edit;
        choice = 4;
    }
    else if (purpose == BrowsePurpose::Core)
    {
        draft.core = fs::canonical(path).string();
        dirty = true;
        modal = Modal::Edit;
        choice = 2;
    }
    else
    {
        if (!game())
        {
            return;
        }
        Game changed = *game();
        changed.path = fs::canonical(path).string();
        library.update(changed);
        refresh(changed.id);
        modal = Modal::Details;
        choice = 0;
        notify("Game file located. Original files were not moved.");
    }
}
void Home::finish_import()
{
    if (editing)
    {
        library.update(draft);
    }
    else
    {
        draft.id = library.add(draft);
    }
    // A duplicate import selects the existing entry without overwriting it.
    const auto saved = library.game(draft.id);
    if (!saved)
    {
        throw std::runtime_error("Saved game was not found in the library");
    }
    draft = *saved;
    if (editing)
    {
        boxart.save(draft.save_id);
    }
    cache_game(draft);
    sidebar = system_index(draft.system) + 3;
    query.clear();
    refilter(draft.id);
    stop_text();
    modal = Modal::Details;
    choice = 0;
    focus = 1;
    notify(editing ? "Game details saved" : "Game added to your library");
}
void Home::set_system(int direction)
{
    int index = system_index(draft.system);
    const int count = static_cast<int>(systems().size());
    index = (index < 0) ? ((direction > 0) ? 0 : count - 1) : (index + direction + count) % count;
    draft.system = systems()[index].id;
    draft.core = bundled_core(systems()[index]).string();
    dirty = true;
}
void Home::edit_game()
{
    if (!game())
    {
        return;
    }
    draft = *game();
    boxart.reset();
    editing = true;
    dirty = false;
    modal = Modal::Edit;
    choice = 0;
}
void Home::close_editor()
{
    stop_text();
    boxart.reset();
    modal = editing ? Modal::Details : Modal::None;
    choice = 0;
}
std::vector<std::string> Home::modal_rows()
{
    if (modal == Modal::Bios)
    {
        return {"Choose BIOS file", bios_for_import ? (bios_installed ? "Continue import" : "Skip for now") : "Done",
                "Back"};
    }
    if (modal == Modal::Details)
    {
        std::vector<std::string> result{process.active() ? "Game running in another window" : "Play", "Edit details",
                                        (game() && game()->favorite) ? "Remove from favorites" : "Add to favorites",
                                        "Locate file", "Remove from library"};
        if (game() && (game()->system == "psx" || game()->system == "saturn"))
        {
            result.push_back("BIOS files");
        }
        result.push_back("Back");
        return result;
    }
    if (modal == Modal::Edit)
    {
        int sys = system_index(draft.system);
        std::vector<std::string> rows{"Title: " + draft.title,
                                      "System: " + ((sys < 0) ? std::string("Choose a system") : systems()[sys].name),
                                      "Core: " + (draft.core.empty() ? std::string("Choose a core file")
                                                                     : fs::path(draft.core).filename().string()),
                                      draft.favorite ? "Favorite: yes" : "Favorite: no"};
        if (editing)
        {
            rows.push_back(boxart.pending() ? "Box art: new image selected" : "Box art: choose image");
        }
        rows.push_back(editing ? "Save changes" : "Add to library");
        rows.push_back("Cancel");
        return rows;
    }
    if (modal == Modal::Remove)
    {
        return {"Cancel", "Remove from library"};
    }
    if (modal == Modal::Discard)
    {
        return {"Keep editing", "Discard changes"};
    }
    return {};
}
void Home::navigate(int dx, int dy)
{
    update_layout();
    if (typing_title || search)
    {
        return;
    }
    if (modal == Modal::Browse)
    {
        if (dy && !entries.empty())
        {
            browse_selected = std::clamp(browse_selected + dy, 0, static_cast<int>(entries.size()) - 1);
        }
        if (dx && !entries.empty())
        {
            auto same_group = [&](int a, int b)
            {
                return entries[a].directory == entries[b].directory &&
                       SDL_strcasecmp(browser_initial(entries[a], purpose).c_str(),
                                      browser_initial(entries[b], purpose).c_str()) == 0;
            };
            int next = browse_selected;
            if (dx > 0)
            {
                while (next < static_cast<int>(entries.size()) && same_group(browse_selected, next))
                {
                    ++next;
                }
                if (next < static_cast<int>(entries.size()))
                {
                    browse_selected = next;
                }
            }
            else
            {
                while (next > 0 && same_group(browse_selected, next - 1))
                {
                    --next;
                }
                if (next > 0)
                {
                    --next;
                }
                while (next > 0 && same_group(next, next - 1))
                {
                    --next;
                }
                browse_selected = next;
            }
        }
        return;
    }
    if (modal != Modal::None)
    {
        auto rows = modal_rows();
        if (dy && !rows.empty())
        {
            choice = (choice + dy + static_cast<int>(rows.size())) % static_cast<int>(rows.size());
        }
        if (dx && modal == Modal::Edit && choice == 1)
        {
            set_system(dx);
        }
        return;
    }
    if (focus == 0)
    {
        if (dy)
        {
            sidebar = std::clamp(sidebar + dy, 0, static_cast<int>(systems().size()) + 2);
            refilter();
        }
        if (dx > 0)
        {
            focus = visible.empty() ? 2 : 1;
        }
    }
    else if (focus == 2)
    {
        if (dx < 0)
        {
            toolbar = std::max(1, toolbar - 1);
        }
        if (dx > 0 && toolbar >= 1)
        {
            toolbar = 2;
        }
        if (dy > 0)
        {
            if (toolbar == 0)
            {
                toolbar = 2;
            }
            else
            {
                focus = visible.empty() ? 0 : 1;
            }
        }
        if (dy < 0)
        {
            if (toolbar >= 2 && sidebar >= 3)
            {
                toolbar = 0;
            }
            else
            {
                focus = 0;
            }
        }
    }
    else
    {
        const int cols = list_view ? 1 : layout.columns;
        if (dx < 0 && (list_view || selected % cols == 0))
        {
            focus = 0;
            return;
        }
        if (dy < 0 && selected < cols)
        {
            focus = 2;
            return;
        }
        if (!visible.empty())
        {
            selected = std::clamp(selected + dx + dy * cols, 0, static_cast<int>(visible.size()) - 1);
        }
    }
}
void Home::back()
{
    if (typing_title)
    {
        stop_text();
        return;
    }
    if (search)
    {
        stop_text();
        focus = 2;
        return;
    }
    if (modal == Modal::Browse)
    {
        modal = (purpose == BrowsePurpose::Bios) ? Modal::Bios
                : (purpose == BrowsePurpose::Core || purpose == BrowsePurpose::BoxArt) ? Modal::Edit
                : (purpose == BrowsePurpose::Locate) ? Modal::Details
                                                     : Modal::None;
        choice = purpose == BrowsePurpose::BoxArt ? 4 : 0;
        return;
    }
    if (modal == Modal::Edit)
    {
        if (dirty)
        {
            modal = Modal::Discard;
            choice = 0;
        }
        else
        {
            close_editor();
        }
    }
    else if (modal == Modal::Discard)
    {
        modal = Modal::Edit;
        choice = 0;
        exit_after_discard = false;
    }
    else if (modal == Modal::Remove)
    {
        modal = Modal::Details;
        choice = 0;
    }
    else if (modal == Modal::Bios)
    {
        modal = bios_for_import ? Modal::Edit : Modal::Details;
        choice = bios_for_import ? 4 : 0;
    }
    else if (modal == Modal::Details)
    {
        modal = Modal::None;
    }
    else
    {
        focus = 0;
    }
}
void Home::activate(bool with_controller)
{
    if (modal == Modal::Bios)
    {
        if (choice == 0)
        {
            start_browse(BrowsePurpose::Bios);
        }
        else if (choice == 1)
        {
            if (bios_for_import)
            {
                finish_import();
            }
            else
            {
                modal = Modal::Details;
                choice = 0;
            }
        }
        else
        {
            back();
        }
        return;
    }
    if (typing_title || search)
    {
        stop_text();
        return;
    }
    if (modal == Modal::Browse)
    {
        if (browsing || entries.empty())
        {
            return;
        }
        const auto entry = entries[browse_selected];
        if (entry.directory)
        {
            read_folder(entry.path);
        }
        else
        {
            choose_file(entry.path);
        }
        return;
    }
    if (modal == Modal::Remove)
    {
        if (choice == 1 && game())
        {
            const auto id = game()->id;
            library.remove(id);
            std::erase_if(all,
                          [&](const Game& entry)
                          {
                              return entry.id == id;
                          });
            refilter();
            modal = Modal::None;
            notify("Removed from library. The game file remains untouched.");
        }
        else
        {
            modal = Modal::Details;
            choice = 0;
        }
        return;
    }
    if (modal == Modal::Discard)
    {
        if (choice == 1)
        {
            close_editor();
            if (exit_after_discard)
            {
                running = false;
            }
        }
        else
        {
            modal = Modal::Edit;
            choice = 0;
        }
        exit_after_discard = false;
        return;
    }
    if (modal == Modal::Edit)
    {
        if (choice == 0)
        {
            typing_title = true;
            SDL_StartTextInput(window);
        }
        else if (choice == 1)
        {
            set_system(1);
        }
        else if (choice == 2)
        {
            start_browse(BrowsePurpose::Core);
        }
        else if (choice == 3)
        {
            draft.favorite = !draft.favorite;
            dirty = true;
        }
        else if (editing && choice == 4)
        {
            start_browse(BrowsePurpose::BoxArt);
        }
        else if (choice == (editing ? 5 : 4))
        {
            if (!editing && (draft.system == "psx" || draft.system == "saturn") &&
                !has_bios(draft.system, fs::path(data_dir) / "system"))
            {
                bios_system = draft.system;
                bios_for_import = true;
                bios_installed = false;
                stop_text();
                modal = Modal::Bios;
                choice = 0;
            }
            else
            {
                finish_import();
            }
        }
        else
        {
            back();
        }
        return;
    }
    if (modal == Modal::Details && game())
    {
        if (choice == 0)
        {
            std::vector<std::string> arguments{"--data-dir", data_dir, "--title", game()->title};
            if (with_controller && library.session_count() < 3)
            {
                arguments.push_back("--show-menu-hint");
            }
            process.start(executable, *game(), arguments);
            launched_id = game()->id;
            launched_with_controller = with_controller;
            played_recorded = false;
            modal = Modal::None;
            notify(game()->archive_member.empty() ? "Opening " + game()->title
                                                  : "Preparing game...");
        }
        else if (choice == 1)
        {
            edit_game();
        }
        else if (choice == 2)
        {
            auto changed = *game();
            changed.favorite = !changed.favorite;
            library.set_favorite(changed.id, changed.favorite);
            refresh(changed.id);
            if (!game())
            {
                modal = Modal::None;
            }
        }
        else if (choice == 3)
        {
            folder = fs::path(game()->path).parent_path();
            start_browse(BrowsePurpose::Locate);
        }
        else if (choice == 4)
        {
            modal = Modal::Remove;
            choice = 0;
        }
        else if (choice == 5 && (game()->system == "psx" || game()->system == "saturn"))
        {
            bios_system = game()->system;
            bios_for_import = false;
            bios_installed = has_bios(bios_system, fs::path(data_dir) / "system");
            modal = Modal::Bios;
            choice = 0;
        }
        else
        {
            modal = Modal::None;
        }
        return;
    }
    if (focus == 2)
    {
        if (toolbar == 0)
        {
            start_browse(BrowsePurpose::Import);
        }
        else if (toolbar == 1)
        {
            search = true;
            SDL_StartTextInput(window);
        }
        else
        {
            list_view = !list_view;
            first_row = 0;
        }
    }
    else if (focus == 0)
    {
        focus = visible.empty() ? 2 : 1;
    }
    else if (game())
    {
        modal = Modal::Details;
        choice = 0;
    }
}

void Home::event(SDL_Event event)
{
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
    {
        block_controller();
    }
    switch (event.type)
    {
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_SHOWN:
    case SDL_EVENT_RENDER_TARGETS_RESET:
    case SDL_EVENT_RENDER_DEVICE_RESET:
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_DROP_FILE:
        needs_redraw = true;
        break;
    default:
        break;
    }
    if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
    {
        if (process.active())
        {
            notify("Quit the game from its menu before closing the library.");
        }
        else if (dirty && (modal == Modal::Edit || modal == Modal::Discard))
        {
            stop_text();
            modal = Modal::Discard;
            choice = 0;
            exit_after_discard = true;
        }
        else
        {
            running = false;
        }
        return;
    }
    if (event.type == SDL_EVENT_GAMEPAD_ADDED && discover())
    {
        notify("Controller connected");
    }
    if (event.type == SDL_EVENT_GAMEPAD_REMOVED && pad && event.gdevice.which == SDL_GetGamepadID(pad))
    {
        SDL_CloseGamepad(pad);
        pad = nullptr;
        notify(discover() ? "Controller disconnected. Using the next controller."
                          : "Controller disconnected. Keyboard and mouse are ready.");
    }
    if (event.type == SDL_EVENT_DROP_FILE)
    {
        if (modal == Modal::None)
        {
            import_file(event.drop.data);
        }
        else
        {
            notify("Finish or cancel the current dialog before importing another game.");
        }
    }
    if (event.type == SDL_EVENT_TEXT_INPUT && (search || typing_title))
    {
        auto& target = search ? query : draft.title;
        if (target.size() < 512)
        {
            target += event.text.text;
        }
        if (search)
        {
            refilter();
        }
        else
        {
            dirty = true;
        }
    }
    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        auto key = event.key.scancode;
        if (key == SDL_SCANCODE_ESCAPE)
        {
            back();
        }
        else if (key == SDL_SCANCODE_RETURN && !event.key.repeat)
        {
            activate();
        }
        else if (search || typing_title)
        {
            auto& target = search ? query : draft.title;
            if (key == SDL_SCANCODE_BACKSPACE && !target.empty())
            {
                std::size_t i = target.size() - 1;
                while (i && (static_cast<unsigned char>(target[i]) & 0xc0) == 0x80)
                {
                    --i;
                }
                target.erase(i);
                if (search)
                {
                    refilter();
                }
                else
                {
                    dirty = true;
                }
            }
            if ((event.key.mod & SDL_KMOD_CTRL) && key == SDL_SCANCODE_A)
            {
                target.clear();
                if (search)
                {
                    refilter();
                }
                else
                {
                    dirty = true;
                }
            }
        }
        else
        {
            if (key == SDL_SCANCODE_UP)
            {
                navigate(0, -1);
            }
            if (key == SDL_SCANCODE_DOWN)
            {
                navigate(0, 1);
            }
            if (key == SDL_SCANCODE_LEFT)
            {
                navigate(-1, 0);
            }
            if (key == SDL_SCANCODE_RIGHT)
            {
                navigate(1, 0);
            }
            if (key == SDL_SCANCODE_TAB && modal == Modal::None)
            {
                focus = (focus + 1) % 3;
            }
            if (key == SDL_SCANCODE_I && modal == Modal::None)
            {
                start_browse(BrowsePurpose::Import);
            }
            if (key == SDL_SCANCODE_F && modal == Modal::None)
            {
                search = true;
                SDL_StartTextInput(window);
            }
        }
    }
    if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        update_layout();
        hits.clear();
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_WHEEL)
    {
        int direction = (event.wheel.y > 0) ? -1 : (event.wheel.y < 0) ? 1 : 0;
        if (modal == Modal::Browse)
        {
            navigate(0, direction * 3);
        }
        else if (modal == Modal::None)
        {
            float mx, my;
            SDL_GetMouseState(&mx, &my);
            if (mx < layout.side)
            {
                focus = 0;
                sidebar_scroll += direction * (layout.system_height + 4) * 3;
            }
            else if (!visible.empty())
            {
                focus = 1;
                navigate(0, direction);
            }
        }
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
    {
        SDL_ConvertEventToRenderCoordinates(renderer, &event);
        const SDL_FPoint point{event.button.x, event.button.y};
        for (auto it = hits.rbegin(); it != hits.rend(); ++it)
        {
            if (SDL_PointInRectFloat(&point, &it->rect))
            {
                const int kind = it->kind, index = it->index;
                if (kind == 0)
                {
                    stop_text();
                    sidebar = index;
                    focus = 0;
                    refilter();
                }
                if (kind == 1)
                {
                    stop_text();
                    selected = index;
                    focus = 1;
                    activate();
                }
                if (kind == 2)
                {
                    stop_text();
                    toolbar = index;
                    focus = 2;
                    activate();
                }
                if (kind == 3)
                {
                    stop_text();
                    choice = index;
                    activate();
                }
                if (kind == 4)
                {
                    browse_selected = index;
                    activate();
                }
                if (kind == 5)
                {
                    back();
                }
                if (kind == 6)
                {
                    activate();
                }
                if (kind == 9)
                {
                    clear_search();
                }
                if (kind == 7)
                {
                    focus = 0;
                    const float view = layout.height - layout.bar_height;
                    const float total = layout.systems_top + systems().size() * (layout.system_height + 4) + layout.gap;
                    const float track = view - 8, thumb = track * view / total;
                    sidebar_scroll =
                        std::clamp((point.y - 4 - thumb / 2) / (track - thumb), 0.0f, 1.0f) * (total - view);
                }
                if (kind == 8)
                {
                    focus = 1;
                    selected = index;
                }
                break;
            }
        }
    }
}

int Home::wait_timeout() const
{
    const bool focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    if (needs_redraw && focused)
    {
        return 0;
    }
    int timeout = -1;
    auto sooner = [&](int delay)
    {
        if (timeout < 0 || delay < timeout)
        {
            timeout = delay;
        }
    };
    if (browsing)
    {
        sooner(50);
    }
    if (process.active())
    {
        sooner(focused ? 100 : 500);
    }
    const auto now = SDL_GetTicks();
    if (focused && repeat_direction)
    {
        sooner((repeat_at > now) ? static_cast<int>(repeat_at - now) : 0);
    }
    if (message_until > now)
    {
        sooner(static_cast<int>(message_until - now));
    }
    return timeout;
}

void Home::tick()
{
    if (message_until && SDL_GetTicks() >= message_until)
    {
        message_until = 0;
        needs_redraw = true;
    }
    if (browsing && listing.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
    {
        needs_redraw = true;
        auto result = listing.get();
        entries = std::move(result.entries);
        browsing = false;
        if (!result.error.empty())
        {
            notify(result.error);
            logger.write(HomeLogLevel::Warn, result.error);
        }
        if (purpose == BrowsePurpose::Archive && modal == Modal::Browse && entries.size() == 1 && result.error.empty())
        {
            choose_file(entries[0].path);
        }
    }
    if (process.active())
    {
        auto exit = process.poll();
        bool changed = false;
        if (process.ready() && !played_recorded)
        {
            library.played(launched_id);
            if (launched_with_controller)
            {
                library.record_session();
            }
            played_recorded = true;
            changed = true;
        }
        if (exit)
        {
            library.sync_playtime(launched_id);
            changed = true;
        }
        if (changed)
        {
            refresh(launched_id);
        }
        if (exit)
        {
            notify((*exit == 0) ? "Game closed. Welcome back." : process.error(*exit));
            SDL_RaiseWindow(window);
        }
    }
    controller_input((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0);
}

void Home::controller_input(bool focused)
{
    if (!focused)
    {
        block_controller();
        return;
    }
    auto b = [&](SDL_GamepadButton button)
    {
        return pad && SDL_GetGamepadButton(pad, button);
    };
    int x = pad ? SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) : 0;
    int y = pad ? SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) : 0;
    std::array<bool, 7> input{b(SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -16000,
                              b(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > 16000,
                              b(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -16000,
                              b(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > 16000,
                              b(SDL_GAMEPAD_BUTTON_SOUTH),
                              b(SDL_GAMEPAD_BUTTON_EAST),
                              b(SDL_GAMEPAD_BUTTON_NORTH)};
    // Focus can return before a backend reports a fresh axis value. Never
    // rearm auto-repeat from that cached tilt: require neutral menu input first.
    if (controller_blocked)
    {
        previous = input;
        if (std::none_of(input.begin(), input.end(),
                         [](bool down)
                         {
                             return down;
                         }))
        {
            controller_blocked = false;
        }
        return;
    }
    int direction = input[0] ? 1 : input[1] ? 2 : input[2] ? 3 : input[3] ? 4 : 0;
    if (direction && ((!previous[direction - 1] && direction != repeat_direction) ||
                      (direction == repeat_direction && SDL_GetTicks() >= repeat_at)))
    {
        needs_redraw = true;
        navigate((direction == 3) ? -1 : (direction == 4) ? 1 : 0, (direction == 1) ? -1 : (direction == 2) ? 1 : 0);
        repeat_at = SDL_GetTicks() + ((direction != repeat_direction) ? 350 : 120);
    }
    repeat_direction = direction;
    if (input[4] && !previous[4])
    {
        needs_redraw = true;
        activate(true);
    }
    else if (input[5] && !previous[5])
    {
        needs_redraw = true;
        back();
    }
    else if (input[6] && !previous[6])
    {
        clear_search();
    }
    previous = input;
}

void Home::box(SDL_FRect rect, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
}
void Home::button(SDL_FRect rect, const std::string& title, bool active, int kind, int index, const std::string& icon)
{
    if (active)
    {
        if (kind == 0 && (focus != 0 || modal != Modal::None))
        {
            box(rect, {40, 49, 71, 255});
        }
        else
        {
            box(rect, accent);
            box({rect.x + 3, rect.y + 3, rect.w - 6, rect.h - 6}, {40, 49, 71, 255});
        }
    }
    else
    {
        box(rect, {38, 38, 38, 255});
    }
    const SDL_Color color = active ? ink : muted;
    const int size = layout.body;
    const float icon_size = size * 1.3f;
    const float inset = icon.empty() ? 0 : icon_size + layout.gap;
    if (!icon.empty())
    {
        text.icon(rect.x + layout.gap, rect.y + (rect.h - icon_size) / 2, icon_size, icon);
    }
    label(rect.x + layout.gap + inset, rect.y + (rect.h - size) / 2 - size * .15f, title, size, color,
          rect.w - inset - ((kind == 0 && index >= 3) ? layout.gap * 3 : layout.gap * 2));
    hits.push_back({rect, kind, index});
}

void Home::update_layout()
{
    int width = 0, height = 0;
    if (!SDL_GetWindowSize(window, &width, &height) || width <= 0 || height <= 0)
    {
        return;
    }
    layout.width = static_cast<float>(width);
    layout.height = static_cast<float>(height);
    const float scale = std::min(width / 1280.0f, height / 720.0f);
    layout.body = std::max(18, static_cast<int>(18 * scale));
    layout.small = std::max(16, static_cast<int>(21 * scale));
    layout.heading = std::max(24, static_cast<int>(32 * scale));
    layout.gap = std::max(12.0f, 12 * scale);
    layout.bar_height = (width < 760) ? layout.small * 2.0f + 34 : std::max(44.0f, 44 * scale);
    layout.side = width * .20f;
    layout.left = layout.side + layout.gap;
    layout.content = layout.width - layout.left - layout.gap;
    layout.header_height = std::max(48.0f, layout.heading * 1.25f);
    layout.toolbar_height = std::max(40.0f, layout.body * 2.4f);
    layout.grid_top = layout.gap * 3 + layout.header_height + layout.toolbar_height;
    layout.row_height = std::max(49.0f, layout.body * 2.8f);
    layout.system_height = layout.body * 1.9f;
    layout.systems_top = layout.gap * 4 + layout.body * 2 + 3 * (layout.system_height + 4);
    layout.columns =
        std::max(1, static_cast<int>((layout.content + layout.gap) / (std::max(240.0f, 190 * scale) + layout.gap)));
    const float available = layout.height - layout.bar_height - layout.grid_top - layout.gap;
    const float caption_height = layout.gap + layout.body + std::max(12, layout.body * 2 / 3) + 4;
    const float cell_width = (layout.content - (layout.columns - 1) * layout.gap) / layout.columns;
    layout.card_width = std::min(cell_width, std::max(1.0f, available - caption_height));
    layout.card_height = layout.card_width + caption_height;
    layout.grid_rows = std::max(1, static_cast<int>((available + layout.gap) / (layout.card_height + layout.gap)));
    layout.list_rows =
        std::max(1, static_cast<int>((available - layout.body + layout.gap) / (layout.row_height + layout.gap)));
    // Layout uses window points; SDL maps these to all available output pixels.
    SDL_SetRenderLogicalPresentation(renderer, width, height, SDL_LOGICAL_PRESENTATION_STRETCH);
    const float view_height = layout.height - layout.bar_height;
    const float total_height = layout.systems_top + systems().size() * (layout.system_height + 4) + layout.gap;
    if (sidebar_tracked != sidebar || sidebar_view_height != view_height)
    {
        const float selected_y = (sidebar < 3) ? layout.gap * 2 + layout.body + sidebar * (layout.system_height + 4)
                                               : layout.systems_top + (sidebar - 3) * (layout.system_height + 4);
        if (selected_y < sidebar_scroll + layout.gap)
        {
            sidebar_scroll = selected_y - layout.gap;
        }
        if (selected_y + layout.system_height > sidebar_scroll + view_height - layout.gap)
        {
            sidebar_scroll = selected_y + layout.system_height - view_height + layout.gap;
        }
        if (sidebar == 0)
        {
            sidebar_scroll = 0;
        }
        sidebar_tracked = sidebar;
        sidebar_view_height = view_height;
    }
    sidebar_scroll = std::clamp(sidebar_scroll, 0.0f, std::max(0.0f, total_height - view_height));
}

void Home::draw()
{
    update_layout();
    const auto& l = layout;
    hits.clear();
    SDL_SetRenderDrawColor(renderer, 24, 24, 24, 255);
    SDL_RenderClear(renderer);
    const float bottom = l.height - l.bar_height, right = l.width - l.gap;
    box({0, 0, l.side, bottom}, {32, 32, 32, 255});
    const float side_width = std::max(1.0f, l.side - l.gap * 2);
    const SDL_Rect sidebar_clip{0, 0, static_cast<int>(l.side), static_cast<int>(bottom)};
    SDL_SetRenderClipRect(renderer, &sidebar_clip);
    label(l.gap * 2, l.gap - sidebar_scroll, "LIBRARY", std::max(11, l.body * 2 / 3), muted, side_width);
    const std::array<std::string, 3> filters{"All games", "Favorites", "Recently played"};
    for (int i = 0; i < 3; ++i)
    {
        button({l.gap, l.gap * 2 + l.body + i * (l.system_height + 4) - sidebar_scroll, side_width, l.system_height},
               filters[i], sidebar == i, 0, i);
    }
    label(l.gap * 2, l.systems_top - l.body - l.gap - sidebar_scroll, "SYSTEMS", std::max(11, l.body * 2 / 3), muted,
          side_width);
    for (int i = 0; i < static_cast<int>(systems().size()); ++i)
    {
        const auto& system = systems()[i];
        int count = static_cast<int>(std::count_if(all.begin(), all.end(),
                                                   [&](const Game& game)
                                                   {
                                                       return game.system == system.id;
                                                   }));
        std::string name = (l.side < 200) ? system.short_name : system.name;
        if (name.size() > 24)
        {
            name = system.short_name == "GB / GBC" ? "Game Boy / Color"
                   : system.short_name == "NES"    ? "Nintendo/Famicom"
                   : system.short_name == "MD"     ? "Genesis/Mega Drive"
                                                   : "TurboGrafx/PC Engine";
        }
        const float y = l.systems_top + i * (l.system_height + 4) - sidebar_scroll;
        button({l.gap, y, side_width, l.system_height}, name, sidebar == i + 3, 0, i + 3);
        label(l.gap + side_width - l.gap * 1.6f, y + (l.system_height - l.body) / 2, std::to_string(count),
              std::max(11, l.body * 2 / 3), muted, l.gap * 1.5f);
    }
    SDL_SetRenderClipRect(renderer, nullptr);
    // Hit targets obey the same clipping as the scrolling pane.
    for (auto& hit : hits)
    {
        const float end = std::clamp(hit.rect.y + hit.rect.h, 0.0f, bottom);
        hit.rect.y = std::clamp(hit.rect.y, 0.0f, bottom);
        hit.rect.h = std::max(0.0f, end - hit.rect.y);
    }
    const float sidebar_total = l.systems_top + systems().size() * (l.system_height + 4) + l.gap;
    if (sidebar_total > bottom)
    {
        const float track_height = bottom - 8;
        const float thumb = track_height * bottom / sidebar_total;
        const float x = l.side - l.gap / 2 - 2.5f;
        box({x, 4 + (track_height - thumb) * sidebar_scroll / (sidebar_total - bottom), 5, thumb}, {96, 96, 96, 255});
        hits.push_back({{l.side - l.gap, 0, l.gap, bottom}, 7, 0});
    }
    auto scrollbar = [&](float x, float y, float height, int total, int shown, int first, int kind, int stride)
    {
        if (total <= shown)
        {
            return;
        }
        const float thumb = std::max(16.0f, height * shown / total);
        box({x, y + (height - thumb) * first / (total - shown), 5, thumb}, {96, 96, 96, 255});
        for (int i = 0; i < total; ++i)
        {
            hits.push_back({{x - 3, y + height * i / total, 11, height / total}, kind, i * stride});
        }
    };
    const std::string title = (sidebar >= 3) ? systems()[sidebar - 3].name : filters[sidebar];
    const float add_width = text.width("+ Add game", l.body) + l.gap * 2;
    label(l.left + l.gap, l.gap, title, l.heading, ink,
          (sidebar >= 3) ? l.content - add_width - l.gap * 2 : l.content - l.gap);
    if (sidebar >= 3)
    {
        button({right - add_width, l.gap, add_width, l.header_height}, "+ Add game", focus == 2 && toolbar == 0, 2, 0);
    }
    const float toolbar_y = l.gap * 2 + l.header_height;
    const float view_size = l.toolbar_height;
    button({l.left, toolbar_y, l.content - view_size - l.gap, l.toolbar_height},
           search          ? query + "|"
           : query.empty() ? "Search..."
                           : query,
           search || (focus == 2 && toolbar == 1), 2, 1);
    {
        const int view = list_view ? 0 : 1;
        const float x = right - view_size;
        button({x, toolbar_y, view_size, view_size}, "", focus == 2 && toolbar == 2, 2, 2);
        const float unit = view_size / 7;
        const SDL_Color color = (focus == 2 && toolbar == 2) ? ink : muted;
        if (view == 0)
        {
            for (int row = 0; row < 2; ++row)
            {
                for (int col = 0; col < 2; ++col)
                {
                    box({x + unit * (1.5f + col * 2.2f), toolbar_y + unit * (1.5f + row * 2.2f), unit * 1.8f,
                         unit * 1.8f},
                        color);
                }
            }
        }
        else
        {
            for (int row = 0; row < 3; ++row)
            {
                box({x + unit * 1.3f, toolbar_y + unit * (1.4f + row * 1.5f), unit, unit}, color);
                box({x + unit * 2.8f, toolbar_y + unit * (1.6f + row * 1.5f), unit * 2.8f, unit * .6f}, color);
            }
        }
    }
    if (visible.empty())
    {
        const float y = l.grid_top + l.gap;
        label(l.left + l.gap, y, all.empty() ? "Your collection starts here" : "No games in this view", l.body, ink,
              l.content - l.gap * 2);
        label(l.left + l.gap, y + l.body * 2,
              (sidebar < 3 && all.empty()) ? "Select a system to add your first game."
              : all.empty()                ? "Add a game file to build your library."
                                           : "Try another system, filter, or search.",
              l.body, muted, l.content - l.gap * 2);
    }
    else
    {
        const int cols = list_view ? 1 : l.columns, rows = list_view ? l.list_rows : l.grid_rows;
        const float cell_width = (l.content - (cols - 1) * l.gap) / cols;
        const float card_width = l.card_width;
        int row = selected / cols;
        if (row < first_row)
        {
            first_row = row;
        }
        if (row >= first_row + rows)
        {
            first_row = row - rows + 1;
        }
        first_row = std::clamp(first_row, 0, std::max(0, (static_cast<int>(visible.size()) + cols - 1) / cols - rows));
        const int detail = std::max(12, l.body * 2 / 3);
        float playtime_width = 104;
        if (list_view)
        {
            for (const auto& g : visible)
            {
                playtime_width = std::max(playtime_width, text.width(format_playtime(g.playtime), detail));
            }
            playtime_width = std::min(playtime_width, l.content * .45f);
            label(right - playtime_width - l.gap, l.grid_top, "Playtime", detail, muted, playtime_width);
        }
        // Navigation counts complete rows, but drawing also includes the next
        // row wherever it intersects the pane, as a preview of more games.
        const SDL_Rect library_clip{static_cast<int>(l.left), static_cast<int>(l.grid_top), static_cast<int>(l.content),
                                    static_cast<int>(bottom - l.grid_top)};
        SDL_SetRenderClipRect(renderer, &library_clip);
        const std::size_t first_game_hit = hits.size();
        const int drawn_rows = rows + (list_view ? 0 : 1);
        for (int i = first_row * cols; i < static_cast<int>(visible.size()) && i < (first_row + drawn_rows) * cols; ++i)
        {
            if (!list_view && l.grid_top + (i / cols - first_row) * (l.card_height + l.gap) >= bottom)
            {
                break;
            }
            const auto& g = visible[i];
            int sys = system_index(g.system);
            const bool missing = !fs::is_regular_file(g.path), active = i == selected && focus == 1;
            const std::string status = missing     ? "File missing"
                                       : (sys < 0) ? "Unknown system"
                                                   : systems()[sys].short_name;
            if (list_view)
            {
                SDL_FRect rect{l.left, l.grid_top + l.body + (i - first_row) * (l.row_height + l.gap), l.content,
                               l.row_height};
                button(rect, "", active, 1, i);
                label(rect.x + l.gap, rect.y + 4, g.title, l.body, ink, rect.w - l.gap * 2);
                label(rect.x + l.gap, rect.y + l.body + 7, status, detail, muted,
                      rect.w - playtime_width - l.gap * 4 - detail);
                if (g.favorite)
                {
                    text.icon(right - playtime_width - l.gap * 2 - detail, rect.y + l.body + 7, detail, "star");
                }
                label(right - playtime_width - l.gap, rect.y + l.body + 7, format_playtime(g.playtime), detail, muted,
                      playtime_width);
            }
            else
            {
                const float x = l.left + (i % cols) * (cell_width + l.gap);
                const float y = l.grid_top + (i / cols - first_row) * (l.card_height + l.gap);
                SDL_FRect rect{x, y, card_width, l.card_height};
                if (active)
                {
                    button(rect, "", true, 1, i);
                }
                else
                {
                    hits.push_back({rect, 1, i});
                }
                const float art = card_width - l.gap * 2;
                const SDL_FRect image{x + l.gap, y + l.gap, art, art};
                if (!boxart.draw(g.save_id, image))
                {
                    text.icon(image.x, image.y, art, "placeholder");
                }
                grid_titles.draw(x + l.gap, y + l.card_height - l.gap - l.body - detail - 4, g.title, l.body, ink,
                                 card_width - l.gap * 2);
                label(x + l.gap, y + l.card_height - l.gap - detail, status, detail, muted,
                      card_width - l.gap * 3 - l.body);
                if (g.favorite)
                {
                    text.icon(x + card_width - l.gap - l.body * 1.3f, y + l.card_height - l.gap - l.body * 1.3f,
                              l.body * 1.3f, "star");
                }
            }
        }
        SDL_SetRenderClipRect(renderer, nullptr);
        for (std::size_t i = first_game_hit; i < hits.size(); ++i)
        {
            hits[i].rect.h = std::max(0.0f, std::min(hits[i].rect.h, bottom - hits[i].rect.y));
        }
        scrollbar(l.width - l.gap / 2 - 2.5f, l.grid_top, bottom - l.grid_top - l.gap,
                  (static_cast<int>(visible.size()) + cols - 1) / cols, rows, first_row, 8, cols);
    }
    if (modal != Modal::None)
    {
        draw_modal();
    }
    box({0, bottom, l.width, l.bar_height}, {16, 16, 16, 255});
    const bool compact = l.width < 760;
    const float margin = std::max(24.0f, l.width * .045f), icon_size = l.small * 1.35f, gap = l.small * .35f;
    const float between = l.small, lift = l.small * .15f;
    const bool show_clear = pad && can_clear_search();
    const float clear_width = show_clear ? icon_size + gap + text.width("Clear", l.small) + between : 0;
    float x = l.width - margin - 2 * (icon_size + gap) - text.width("Back", l.small) - text.width("Select", l.small) -
              between - clear_width;
    const float prompt_y = (compact ? l.height - l.small - 14 : bottom + (l.bar_height - l.small) / 2 - 2) - lift;
    label(margin, bottom + (compact ? 6 : (l.bar_height - l.small) / 2 - 2) - lift,
          process.active() ? (process.ready() ? "Game running" : "Preparing game...")
          : game()         ? game()->title
                           : "",
          l.small, ink, compact ? l.width - margin * 2 : x - margin - between);
    auto prompt = [&](const char* icon, const char* title, int kind)
    {
        const float width = icon_size + gap + text.width(title, l.small);
        hits.push_back({{x, prompt_y - 4, width, icon_size + 8}, kind, 0});
        text.icon(x, prompt_y + lift - (icon_size - l.small) / 2 + 2, icon_size, icon);
        label(x + icon_size + gap, prompt_y, title, l.small);
        x += width + between;
    };
    if (show_clear)
    {
        prompt("controller_buttons_north", "Clear", 9);
    }
    prompt(pad ? "controller_buttons_east" : "keyboard_escape", "Back", 5);
    prompt(pad ? "controller_buttons_south" : "keyboard_enter", "Select", 6);
    if (!message.empty() && SDL_GetTicks() < message_until)
    {
        const float width = l.content - l.gap * 2;
        std::vector<std::string> lines;
        std::string line;
        const char* next = message.c_str();
        while (*next)
        {
            const char* begin = next;
            const auto code = SDL_StepUTF8(&next, nullptr);
            if (code == '\n')
            {
                lines.push_back(line);
                line.clear();
                continue;
            }
            const std::string character(begin, next);
            if (line.empty() && (code == ' ' || code == '\t'))
            {
                continue;
            }
            if (!line.empty() && text.width(line + character, l.body) > width)
            {
                const auto space = line.find_last_of(" \t");
                if (space == std::string::npos)
                {
                    lines.push_back(line);
                    line.clear();
                }
                else
                {
                    lines.push_back(line.substr(0, space));
                    line.erase(0, space + 1);
                }
                if (line.empty() && (code == ' ' || code == '\t'))
                {
                    continue;
                }
            }
            line += character;
        }
        if (!line.empty() || lines.empty())
        {
            lines.push_back(line);
        }
        const float line_height = l.body * 1.35f;
        const float text_height = l.body + (lines.size() - 1) * line_height;
        const float height = std::max(l.toolbar_height, text_height + l.gap * 2);
        const float top = bottom - height - l.gap;
        box({l.left, top, l.content, height}, {48, 48, 48, 255});
        float y = top + (height - text_height) / 2 - l.body * .15f;
        for (const auto& value : lines)
        {
            label(l.left + l.gap, y, value, l.body, ink, width);
            y += line_height;
        }
    }
}

void Home::draw_modal()
{
    hits.clear();
    const float available_height = layout.height - layout.bar_height - layout.gap * 2;
    const bool editing_details = modal == Modal::Edit && editing;
    const float scale = std::min(layout.body / 16.0f, available_height / (editing_details ? 600.0f : 540.0f));
    const float dw = std::min(800.0f, (layout.width - layout.gap * 2) / scale);
    const float dh = (modal == Modal::Browse) ? available_height / scale
                                             : std::min(editing_details ? 640.0f : 590.0f, available_height / scale);
    const float left = (layout.width - dw * scale) / 2;
    const float top = (layout.height - layout.bar_height - dh * scale) / 2;
    const float x = 24, inner = dw - 48;
    auto line = [&](float y, const std::string& value, int size, SDL_Color color = ink, float max_width = 0)
    {
        label(left + x * scale, top + y * scale, value, std::max(14, static_cast<int>(size * scale)), color,
              (max_width > 0 ? max_width : inner) * scale);
    };
    auto button =
        [&](SDL_FRect rect, const std::string& value, bool active, int kind, int index, const std::string& icon = {})
    {
        Home::button({left + rect.x * scale, top + rect.y * scale, rect.w * scale, rect.h * scale}, value, active, kind,
                     index, icon);
    };
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    box({0, 0, layout.width, layout.height}, {0, 0, 0, 180});
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    box({left, top, dw * scale, dh * scale}, {32, 32, 32, 255});
    std::string title = (modal == Modal::Bios) ? (bios_system == "psx" ? "PlayStation BIOS" : "Sega Saturn BIOS")
                        : (modal == Modal::Browse)
                            ? ((purpose == BrowsePurpose::Archive) ? "Choose a ROM inside the ZIP"
                               : (purpose == BrowsePurpose::Bios)  ? "Choose a BIOS file"
                               : (purpose == BrowsePurpose::BoxArt) ? "Choose box art"
                                                                   : "Choose a file")
                        : (modal == Modal::Edit)    ? editing ? "Edit game details" : "Add to library"
                        : (modal == Modal::Remove)  ? "Remove this game?"
                        : (modal == Modal::Discard) ? "Discard changes?"
                                                    : "Game details";
    line(26, title, 26);

    if (modal == Modal::Browse)
    {
        line(78, folder.string(), 13, muted);
        if (purpose == BrowsePurpose::BoxArt)
        {
            line(104, "PNG, JPEG, or BMP. The image is copied when you save changes.", 13, muted);
        }
        if (browsing)
        {
            line(128, (purpose == BrowsePurpose::Archive) ? "Reading ZIP..." : "Reading folder...", 16, muted);
        }
        else if (entries.empty())
        {
            line(128, (purpose == BrowsePurpose::Archive) ? "No compatible ROM files found." : "This folder is empty.",
                 16, muted);
        }
        const int browser_rows = std::max(1, static_cast<int>((dh - 178) / 50));
        if (browse_selected < browse_top)
        {
            browse_top = browse_selected;
        }
        if (browse_selected >= browse_top + browser_rows)
        {
            browse_top = browse_selected - browser_rows + 1;
        }
        for (int i = browse_top; i < static_cast<int>(entries.size()) && i < browse_top + browser_rows; ++i)
        {
            const auto& entry = entries[i];
            const bool parent = entry.directory && entry.path == folder.parent_path();
            auto name = (purpose == BrowsePurpose::Archive) ? entry.path.generic_string()
                        : parent                            ? ".. (parent folder)"
                                                            : entry.path.filename().string();
            button({x, 114.0f + (i - browse_top) * 50, inner, 38}, name, i == browse_selected, 4, i,
                   entry.directory ? (parent ? "folder_open" : "folder") : "");
        }
        line(dh - 38, "Enter / South: open    Left / Right: jump by letter    Esc / East: cancel", 13, muted);
        return;
    }
    if (modal == Modal::Details && game())
    {
        line(77, game()->title, 20, ink);
        int sys = system_index(game()->system);
        line(110,
             ((sys < 0) ? "Unknown system" : systems()[sys].name) + "  /  " +
                 fs::path(game()->core).filename().string(),
             12, muted);
        line(137, game()->path, 12, muted);
        if (!game()->archive_member.empty())
        {
            line(160, "ROM: " + game()->archive_member, 12, muted);
        }
        line(184, "Save folder: " + game()->save_name, 12, muted);
        line(208, "Playtime: " + format_playtime(game()->playtime), 14, ink);
        if (!fs::is_regular_file(game()->path))
        {
            line(163, "File missing. Choose Locate file below.", 14, muted);
        }
    }
    if (modal == Modal::Bios)
    {
        line(78, bios_installed ? "A compatible BIOS is installed." : "This system requires a BIOS file to run games.",
             16);
        line(107,
             bios_system == "psx" ? "Select an unmodified PlayStation BIOS (512 KiB)."
                                  : "Select sega_101.bin (Japan) or mpr-17933.bin (US/Europe).",
             14, muted);
        line(136, "The BIOS is checked and copied to Cabinet's system folder.", 14, muted);
        line(165, "Other game regions may need another BIOS. Add more here later.", 13, muted);
    }
    if (modal == Modal::Edit)
    {
        const float width = editing ? inner - 112 : inner;
        line(78, draft.path + (draft.archive_member.empty() ? "" : " / " + draft.archive_member), 12, muted, width);
        line(107, editing ? "Changes are applied when you save." : "Choose a system and core, then confirm to save.",
             14, muted, width);
        if (editing)
        {
            const SDL_FRect preview{left + (dw - 112) * scale, top + 74 * scale, 88 * scale, 110 * scale};
            if (!boxart.draw(draft.save_id, preview, true))
            {
                text.icon(preview.x, preview.y + 11 * scale, 88 * scale, "placeholder");
            }
        }
    }
    if (modal == Modal::Remove)
    {
        line(90, "This removes the library entry only.", 18);
        line(125, "Your game file and save states will not be deleted.", 15, muted);
    }
    if (modal == Modal::Discard)
    {
        line(90, "Your changes to these details have not been saved.", 16, muted);
    }
    auto rows = modal_rows();
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        std::string value = rows[i];
        if (typing_title && i == 0)
        {
            value += "|";
        }
        const float rows_top = (modal == Modal::Details) ? 236.0f : 200.0f;
        button({x, rows_top + static_cast<float>(i) * 50, inner, 38}, value, choice == static_cast<int>(i), 3,
               static_cast<int>(i));
    }
    if (modal == Modal::Edit)
    {
        line(dh - 38,
             typing_title ? "Type a title. Ctrl+A clears. Enter finishes."
                          : "System: Left / Right to change. Core: select to browse.",
             13, muted);
    }
}
} // namespace

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
        home.import_file(import);
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
