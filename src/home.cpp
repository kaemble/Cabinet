#include "home.h"
#include "file_browser.h"
#include "text_edit.h"
#include "boxart.h"
#include "steamgriddb.h"
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
#include <cmath>
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
    Menu,
    Settings,
    ApiKey,
    SteamGrid,
    Details,
    Edit,
    Browse,
    Bios,
    Remove,
    Discard
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

class Home
{
public:
    Home(SDL_Window* window, SDL_Renderer* renderer, const fs::path& data, std::string executable,
         HomeLogger logger = {}, SteamGridDB::Request artwork_request = {})
        : window(window), renderer(renderer), text(renderer), grid_titles(renderer, TextRenderer::Font::Condensed),
          boxart(renderer, data), library(data / "library.sqlite3"), process(logger), logger(logger), executable(std::move(executable)),
          data_dir(data.string()), browser(data, logger)
    {
        this->artwork_request = std::move(artwork_request);
        const auto credentials = save_storage::read(data / "steamgriddb" / "key.conf");
        if (credentials.contains("api_key"))
        {
            api_key = credentials.at("api_key");
        }
        const auto settings = save_storage::read(fs::path(data_dir) / "home.conf");
        swap_confirm_cancel = settings.contains("swap_confirm_cancel") && settings.at("swap_confirm_cancel") == "1";
        dual_screen = settings.contains("dual_screen") && settings.at("dual_screen") == "1";
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
    void import_file(const fs::path& path, bool custom = false);
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
    bool swap_confirm_cancel = false, dual_screen = false;
    std::string api_key, key_draft, artwork_query, queued_artwork, artwork_title;
    SteamGridDB::Request artwork_request;
    struct ArtworkResult
    {
        std::vector<SteamGridGame> games;
        std::string image, error;
        std::uint64_t generation = 0;
        bool searching = false;
    };
    std::future<ArtworkResult> artwork_task;
    std::vector<SteamGridGame> artwork_games;
    std::uint64_t artwork_generation = 0, artwork_task_generation = 0;
    bool artwork_loading() const
    {
        return (artwork_task.valid() && artwork_task_generation == artwork_generation) || !queued_artwork.empty();
    }
    void fetch_artwork(std::int64_t game_id = 0, bool automatic = false);
    void import_artwork();
    std::string& input_value()
    {
        return modal == Modal::ApiKey ? key_draft : modal == Modal::SteamGrid ? artwork_query
               : typing_title ? draft.title : query;
    }
    TextEdit text_edit;
    float text_scroll = 0;
    SDL_FRect text_rect{};
    bool text_dragging = false, caret_visible = true;
    std::uint64_t next_caret = 0;
    Modal modal = Modal::None;
    int choice = 0;
    Game draft;
    bool bios_for_import = false, bios_installed = false;
    std::string bios_system;
    bool editing = false, dirty = false, exit_after_discard = false;
    FileBrowser browser;
    float sidebar_scroll = 0;
    int sidebar_tracked = -1;
    float sidebar_view_height = 0;
    SDL_Gamepad* pad = nullptr;
    std::array<bool, 8> previous{};
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
    void open_menu();
    void request_close();
    int system_count() const
    {
        const bool custom = std::any_of(all.begin(), all.end(),
                                        [](const Game& game)
                                        {
                                            return game.system == "generic";
                                        });
        return static_cast<int>(systems().size()) - (custom ? 0 : 1);
    }
    void start_browse(BrowsePurpose intent);
    void choose_file(const fs::path& path);
    void browse_archive(const fs::path& path, BrowsePurpose intent);
    void finish_locate(const Game& changed);
    void edit_game();
    void finish_import();
    void set_system(int direction);
    void stop_text();
    void start_text(bool title);
    void reset_caret();
    void place_caret(const SDL_FRect& rect, float x, bool extend);
    void draw_text_input(const SDL_FRect& rect);
    bool can_clear_search() const
    {
        return modal == Modal::None && !query.empty() && (search || (focus == 2 && toolbar == 1));
    }
    void clear_search()
    {
        if (can_clear_search())
        {
            query.clear();
            text_edit.begin(query);
            reset_caret();
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
    sidebar = std::clamp(sidebar, 0, system_count() + 2);
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
void Home::start_text(bool title)
{
    typing_title = title;
    search = !title;
    text_edit.begin(input_value());
    text_scroll = 0;
    reset_caret();
    SDL_StartTextInput(window);
}

void Home::reset_caret()
{
    caret_visible = true;
    next_caret = SDL_GetTicks() + 500;
    needs_redraw = true;
}

void Home::stop_text()
{
    SDL_StopTextInput(window);
    search = typing_title = text_dragging = false;
    next_caret = 0;
    if (modal == Modal::Edit && !editing && !api_key.empty() && artwork_title != draft.title)
    {
        import_artwork();
    }
}

void Home::place_caret(const SDL_FRect& rect, float x, bool extend)
{
    const auto value = modal == Modal::ApiKey ? std::string(key_draft.size(), '*') : input_value();
    const float prefix = (typing_title && modal == Modal::Edit) ? text.width("Title: ", layout.body) : 0;
    const float offset = x - rect.x - layout.gap - prefix + text_scroll;
    std::size_t position = 0;
    float previous = 0;
    while (position < value.size())
    {
        const auto end = TextEdit::next(value, position);
        const float width = text.width(value.substr(0, end), layout.body);
        if (offset < (previous + width) / 2)
        {
            break;
        }
        position = end;
        previous = width;
    }
    text_edit.place(position, extend);
    text_rect = rect;
    reset_caret();
}

void Home::draw_text_input(const SDL_FRect& rect)
{
    text_rect = rect;
    const auto value = modal == Modal::ApiKey ? std::string(key_draft.size(), '*') : input_value();
    const auto prefix = (typing_title && modal == Modal::Edit) ? std::string("Title: ") : std::string{};
    const int size = layout.body;
    const float y = rect.y + (rect.h - size) / 2 - size * .15f;
    const float prefix_width = text.width(prefix, size);
    const float left = rect.x + layout.gap + prefix_width;
    const float width = std::max(1.0f, rect.w - layout.gap * 2 - prefix_width);
    const float caret = text.width(value.substr(0, text_edit.position()), size);
    const float total = text.width(value, size);
    const auto [first, last] = text_edit.selection();
    const float selection_start = text.width(value.substr(0, first), size);
    const float selection_end = text.width(value.substr(0, last), size);
    const float visible_width = std::max(1.0f, width - 2);
    text_scroll = std::clamp(text_scroll, std::max(0.0f, caret - visible_width), caret);
    if (first != last && selection_end - selection_start <= visible_width)
    {
        text_scroll = std::clamp(text_scroll, std::max(0.0f, selection_end - visible_width), selection_start);
    }
    text_scroll = std::min(text_scroll, std::max(0.0f, total - visible_width));
    label(rect.x + layout.gap, y, prefix, size, ink, rect.w - layout.gap * 2);
    const bool clipped = SDL_RenderClipEnabled(renderer);
    SDL_Rect previous{};
    SDL_GetRenderClipRect(renderer, &previous);
    SDL_Rect clip{static_cast<int>(std::ceil(left)), static_cast<int>(rect.y),
                  static_cast<int>(width), static_cast<int>(rect.h)};
    if (clipped)
    {
        SDL_GetRectIntersection(&clip, &previous, &clip);
    }
    SDL_SetRenderClipRect(renderer, &clip);
    if (first != last)
    {
        box({left + selection_start - text_scroll, y - 1, selection_end - selection_start,
             static_cast<float>(size + 4)}, accent);
    }
    label(left - text_scroll, y, value, size, ink, 100000);
    if (caret_visible)
    {
        box({left + caret - text_scroll, y, 1.5f, static_cast<float>(size + 2)}, ink);
    }
    SDL_SetRenderClipRect(renderer, clipped ? &previous : nullptr);
    const SDL_Rect input_area{static_cast<int>(left), static_cast<int>(rect.y),
                              static_cast<int>(width), static_cast<int>(rect.h)};
    SDL_SetTextInputArea(window, &input_area, static_cast<int>(caret - text_scroll));
}
void Home::start_browse(BrowsePurpose intent)
{
    if (browser.loading())
    {
        return;
    }
    if (intent == BrowsePurpose::Import && sidebar < 3)
    {
        notify("Select a system before adding a game.");
        return;
    }
    stop_text();
    modal = Modal::Browse;
    browser.start(intent, draft.core);
}
void Home::select_system(const std::string& id)
{
    const int index = system_index(id);
    if (index < 0)
    {
        throw std::runtime_error("Unknown system: " + id);
    }
    sidebar = index < system_count() ? index + 3 : 0;
    refilter();
}
void Home::import_file(const fs::path& path, bool custom)
{
    if (!custom && sidebar < 3)
    {
        throw std::runtime_error("Select a system before adding a game.");
    }
    if (!fs::is_regular_file(path))
    {
        throw std::runtime_error("Choose an existing game file.");
    }
    stop_text();
    ++artwork_generation;
    queued_artwork.clear();
    boxart.reset();
    draft = {};
    draft.path = fs::canonical(path).string();
    draft.title = path.stem().string();
    const auto& selected_system = systems()[custom ? system_index("generic") : sidebar - 3];
    draft.system = selected_system.id;
    draft.core = bundled_core(selected_system).string();
    browser.remember_folder(BrowsePurpose::Import, fs::path(draft.path).parent_path());
    if (draft.system == "generic")
    {
        editing = false;
        dirty = false;
        modal = Modal::Edit;
        choice = 2;
        browser.set_folder(fs::path(draft.path).parent_path());
        import_artwork();
        return;
    }
    if (rom_archive::is_zip(path))
    {
        browse_archive(path, BrowsePurpose::Import);
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
    browser.set_folder(fs::path(draft.path).parent_path());
    import_artwork();
}
void Home::browse_archive(const fs::path& path, BrowsePurpose intent)
{
    modal = Modal::Browse;
    browser.browse_archive(path, intent, draft.system);
}
void Home::finish_locate(const Game& changed)
{
    library.update(changed);
    refresh(changed.id);
    modal = Modal::Details;
    choice = 0;
    notify("Game file located.");
}
void Home::choose_file(const fs::path& path)
{
    if (browser.purpose() == BrowsePurpose::Archive)
    {
        draft.archive_member = path.generic_string();
        if (browser.intent() == BrowsePurpose::Locate)
        {
            finish_locate(draft);
            return;
        }
        draft.title = path.stem().string();
        editing = false;
        dirty = false;
        modal = Modal::Edit;
        choice = 0;
        browser.set_folder(fs::path(draft.path).parent_path());
        import_artwork();
    }
    else if (browser.purpose() == BrowsePurpose::Bios)
    {
        const auto result = install_bios(bios_system, path, fs::path(data_dir) / "system");
        bios_installed = true;
        modal = Modal::Bios;
        choice = 1;
        notify(result);
    }
    else if (browser.purpose() == BrowsePurpose::Import)
    {
        import_file(path);
    }
    else if (browser.purpose() == BrowsePurpose::Custom)
    {
        ++artwork_generation;
        queued_artwork.clear();
        boxart.reset();
        draft = {};
        draft.system = "generic";
        draft.core = fs::canonical(path).string();
        draft.title = path.stem().string();
        editing = false;
        dirty = false;
        modal = Modal::Edit;
        choice = 4;
        import_artwork();
    }
    else if (browser.purpose() == BrowsePurpose::Content)
    {
        draft.path = fs::canonical(path).string();
        draft.title = path.stem().string();
        dirty = true;
        modal = Modal::Edit;
        choice = api_key.empty() ? 5 : 6;
        import_artwork();
    }
    else if (browser.purpose() == BrowsePurpose::BoxArt)
    {
        ++artwork_generation;
        queued_artwork.clear();
        boxart.select(path);
        dirty = true;
        modal = Modal::Edit;
        choice = 4;
    }
    else if (browser.purpose() == BrowsePurpose::Core)
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
        if (changed.system != "generic" && rom_archive::is_zip(path))
        {
            draft = changed;
            browse_archive(path, BrowsePurpose::Locate);
            return;
        }
        changed.archive_member.clear();
        finish_locate(changed);
    }
}
void Home::finish_import()
{
    if (artwork_loading())
    {
        notify("Artwork is loading. Select the SteamGridDB row to cancel, or wait before saving.");
        return;
    }
    const bool duplicate = !editing && std::any_of(all.begin(), all.end(), [&](const Game& game)
    {
        return game.path == draft.path && game.archive_member == draft.archive_member &&
               (!draft.path.empty() || game.core == draft.core);
    });
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
    if (!duplicate)
    {
        boxart.save(draft.save_id);
    }
    ++artwork_generation;
    queued_artwork.clear();
    boxart.reset();
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
    if (draft.system == "generic")
    {
        return;
    }
    int index = system_index(draft.system);
    const int count = static_cast<int>(systems().size()) - 1;
    index = (index < 0) ? ((direction > 0) ? 0 : count - 1) : (index + direction + count) % count;
    draft.system = systems()[index].id;
    draft.core = bundled_core(systems()[index]).string();
    dirty = true;
}
void Home::import_artwork()
{
    artwork_title = draft.title;
    boxart.reset();
    ++artwork_generation;
    queued_artwork.clear();
    if (!api_key.empty())
    {
        queued_artwork = draft.title;
    }
}

void Home::fetch_artwork(std::int64_t game_id, bool automatic)
{
    if (artwork_task.valid())
    {
        notify("A SteamGridDB request is still finishing. Try again shortly.");
        return;
    }
    SteamGridDB client(api_key, artwork_request);
    const auto generation = ++artwork_generation;
    artwork_task_generation = generation;
    const auto title = artwork_query;
    artwork_task = std::async(std::launch::async, [client = std::move(client), generation, title, game_id, automatic]
    {
        ArtworkResult result;
        result.generation = generation;
        result.searching = !automatic && game_id == 0;
        try
        {
            if (result.searching)
            {
                result.games = client.search(title);
            }
            else
            {
                result.image = automatic ? client.import_image(title) : client.image(game_id);
            }
        }
        catch (const std::exception& error)
        {
            result.error = error.what();
        }
        return result;
    });
    needs_redraw = true;
}

void Home::edit_game()
{
    if (!game())
    {
        return;
    }
    draft = *game();
    ++artwork_generation;
    queued_artwork.clear();
    boxart.reset();
    editing = true;
    dirty = false;
    modal = Modal::Edit;
    choice = 0;
}
void Home::close_editor()
{
    stop_text();
    ++artwork_generation;
    queued_artwork.clear();
    boxart.reset();
    modal = editing ? Modal::Details : Modal::None;
    choice = 0;
}
std::vector<std::string> Home::modal_rows()
{
    if (modal == Modal::Menu)
    {
        return {"Add custom entry", "Settings", "Close Cabinet", "SteamGridDB API key", "Back"};
    }
    if (modal == Modal::ApiKey)
    {
        return {"API key: " + std::string(key_draft.size(), '*'), "Save key", "Remove key", "Cancel"};
    }
    if (modal == Modal::SteamGrid)
    {
        std::vector<std::string> rows{"Search: " + artwork_query, "Search SteamGridDB"};
        for (const auto& game : artwork_games)
        {
            rows.push_back(game.name);
        }
        rows.push_back("Cancel search");
        return rows;
    }
    if (modal == Modal::Settings)
    {
        return {std::string("Swap confirm/cancel: ") + (swap_confirm_cancel ? "on" : "off"),
                std::string("Dual-screen runner: ") + (dual_screen ? "on" : "off"), "Back"};
    }
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
                                      "System: " + (draft.system == "generic" ? std::string("Generic")
                                                      : (sys < 0) ? std::string("Choose a system") : systems()[sys].name),
                                      "Core: " + (draft.core.empty() ? std::string("Choose a core file")
                                                                     : fs::path(draft.core).filename().string()),
                                      draft.favorite ? "Favorite: yes" : "Favorite: no"};
        if (editing)
        {
            rows.push_back(boxart.pending() ? "Box art: new image selected" : "Box art: choose image");
        }
        if (!editing && draft.system == "generic")
        {
            rows.push_back("ROM: " + (draft.path.empty() ? std::string("Skip (run without content)")
                                                       : fs::path(draft.path).filename().string()));
        }
        if (!api_key.empty())
        {
            rows.push_back(artwork_loading() ? "SteamGridDB: loading... (select to cancel)"
                           : boxart.pending() ? "SteamGridDB: image selected (search again)" : "SteamGridDB: find box art");
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
        browser.navigate(dx, dy);
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
            sidebar = std::clamp(sidebar + dy, 0, system_count() + 2);
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
        const auto intent = browser.intent();
        modal = (intent == BrowsePurpose::Bios) ? Modal::Bios
                : (intent == BrowsePurpose::Core || intent == BrowsePurpose::Content || intent == BrowsePurpose::BoxArt) ? Modal::Edit
                : (intent == BrowsePurpose::Locate) ? Modal::Details
                : (intent == BrowsePurpose::Custom) ? Modal::Menu
                                                     : Modal::None;
        choice = (intent == BrowsePurpose::BoxArt || intent == BrowsePurpose::Content) ? 4 : 0;
        return;
    }
    if (modal == Modal::ApiKey)
    {
        key_draft.clear();
        modal = Modal::Menu;
        choice = 3;
    }
    else if (modal == Modal::SteamGrid)
    {
        ++artwork_generation;
        queued_artwork.clear();
        modal = Modal::Edit;
        choice = (editing || draft.system == "generic") ? 5 : 4;
    }
    else if (modal == Modal::Settings)
    {
        modal = Modal::Menu;
        choice = 1;
    }
    else if (modal == Modal::Menu)
    {
        modal = Modal::None;
    }
    else if (modal == Modal::Edit)
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
void Home::open_menu()
{
    if (modal == Modal::Menu)
    {
        back();
    }
    else if (modal == Modal::None)
    {
        stop_text();
        modal = Modal::Menu;
        choice = 0;
    }
    needs_redraw = true;
}
void Home::request_close()
{
    if (process.active())
    {
        notify("Quit the game from its menu before closing the library.");
    }
    else if (dirty && (modal == Modal::Edit || modal == Modal::Discard || modal == Modal::SteamGrid))
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
}
void Home::activate(bool with_controller)
{
    if (modal == Modal::Menu)
    {
        if (choice == 0)
        {
            start_browse(BrowsePurpose::Custom);
        }
        else if (choice == 1)
        {
            modal = Modal::Settings;
            choice = 0;
        }
        else if (choice == 2)
        {
            request_close();
        }
        else if (choice == 3)
        {
            key_draft.clear();
            modal = Modal::ApiKey;
            choice = 0;
        }
        else
        {
            back();
        }
        return;
    }
    if ((modal == Modal::ApiKey || modal == Modal::SteamGrid) && typing_title)
    {
        stop_text();
        return;
    }
    if (modal == Modal::ApiKey)
    {
        if (choice == 0)
        {
            start_text(true);
        }
        else if (choice == 1 || choice == 2)
        {
            if (choice == 1)
            {
                SteamGridDB validate(key_draft);
            }
            const auto directory = fs::path(data_dir) / "steamgriddb";
            fs::create_directories(directory);
            fs::permissions(directory, fs::perms::owner_all);
            const auto next_key = choice == 2 ? std::string{} : key_draft;
            save_storage::write(directory / "key.conf", {{"api_key", next_key}});
            fs::permissions(directory / "key.conf", fs::perms::owner_read | fs::perms::owner_write);
            api_key = next_key;
            back();
            notify(api_key.empty() ? "SteamGridDB key removed" : "SteamGridDB key saved");
        }
        else
        {
            back();
        }
        return;
    }
    if (modal == Modal::SteamGrid)
    {
        if (choice == 0)
        {
            start_text(true);
        }
        else if (choice == 1)
        {
            fetch_artwork();
        }
        else if (choice - 2 < static_cast<int>(artwork_games.size()))
        {
            fetch_artwork(artwork_games[choice - 2].id);
        }
        else
        {
            back();
        }
        return;
    }
    if (modal == Modal::Settings)
    {
        if (choice == 2)
        {
            back();
            return;
        }
        const bool next_swap = choice == 0 ? !swap_confirm_cancel : swap_confirm_cancel;
        const bool next_dual = choice == 1 ? !dual_screen : dual_screen;
        save_storage::write(fs::path(data_dir) / "home.conf",
                            {{"swap_confirm_cancel", next_swap ? "1" : "0"}, {"dual_screen", next_dual ? "1" : "0"}});
        swap_confirm_cancel = next_swap;
        dual_screen = next_dual;
        block_controller();
        return;
    }
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
        if (browser.loading() || browser.items().empty())
        {
            return;
        }
        const auto entry = browser.items()[browser.selected()];
        if (entry.directory)
        {
            browser.read_folder(entry.path);
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
            start_text(true);
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
        else if (!editing && draft.system == "generic" && choice == 4)
        {
            start_browse(BrowsePurpose::Content);
        }
        else if (!api_key.empty() && choice == ((editing || draft.system == "generic") ? 5 : 4))
        {
            if (artwork_loading())
            {
                ++artwork_generation;
                queued_artwork.clear();
                notify("Artwork search cancelled");
                return;
            }
            SteamGridDB validate(api_key);
            stop_text();
            modal = Modal::SteamGrid;
            choice = 1;
            artwork_query = draft.title;
            artwork_games.clear();
            fetch_artwork();
        }
        else if (choice == ((editing || draft.system == "generic") ? 5 : 4) + (api_key.empty() ? 0 : 1))
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
            if (swap_confirm_cancel)
            {
                arguments.push_back("--swap-confirm-cancel");
            }
            if (dual_screen)
            {
                arguments.push_back("--dual-screen");
            }
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
            browser.set_folder(fs::path(game()->path).parent_path());
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
            start_text(false);
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
        text_dragging = false;
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
        request_close();
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
        auto& target = input_value();
        const auto previous = target;
        text_edit.insert(target, event.text.text);
        reset_caret();
        if (target != previous)
        {
            if (search)
            {
                refilter();
            }
            else if (modal == Modal::Edit)
            {
                dirty = true;
                if (!editing)
                {
                    ++artwork_generation;
                    queued_artwork.clear();
                }
            }
        }
    }
    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        auto key = event.key.scancode;
        if (key == SDL_SCANCODE_ESCAPE)
        {
            if (!event.key.repeat)
            {
                if (modal == Modal::None && !search && !typing_title)
                {
                    open_menu();
                }
                else
                {
                    back();
                }
            }
        }
        else if (key == SDL_SCANCODE_RETURN && !event.key.repeat)
        {
            activate();
        }
        else if (search || typing_title)
        {
            auto& target = input_value();
            const auto previous = target;
            text_edit.key(target, event.key);
            reset_caret();
            if (target != previous)
            {
                if (search)
                {
                    refilter();
                }
                else if (modal == Modal::Edit)
                {
                    dirty = true;
                    if (!editing)
                    {
                        ++artwork_generation;
                        queued_artwork.clear();
                    }
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
                start_text(false);
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
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT)
    {
        text_dragging = false;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION && text_dragging && (search || typing_title))
    {
        SDL_ConvertEventToRenderCoordinates(renderer, &event);
        place_caret(text_rect, event.motion.x, true);
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
                    toolbar = index;
                    focus = 2;
                    if (index == 1)
                    {
                        if (!search)
                        {
                            start_text(false);
                        }
                        place_caret(it->rect, point.x, SDL_GetModState() & SDL_KMOD_SHIFT);
                        text_dragging = true;
                    }
                    else
                    {
                        stop_text();
                        activate();
                    }
                }
                if (kind == 3)
                {
                    choice = index;
                    if ((modal == Modal::Edit || modal == Modal::ApiKey || modal == Modal::SteamGrid) && index == 0)
                    {
                        if (!typing_title)
                        {
                            start_text(true);
                        }
                        place_caret(it->rect, point.x, SDL_GetModState() & SDL_KMOD_SHIFT);
                        text_dragging = true;
                    }
                    else
                    {
                        stop_text();
                        activate();
                    }
                }
                if (kind == 4)
                {
                    browser.select(index);
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
                if (kind == 10)
                {
                    open_menu();
                }
                if (kind == 7)
                {
                    focus = 0;
                    const float view = layout.height - layout.bar_height;
                    const float total = layout.systems_top + system_count() * (layout.system_height + 4) + layout.gap;
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
    if (browser.loading() || artwork_task.valid() || !queued_artwork.empty())
    {
        sooner(50);
    }
    if (process.active())
    {
        sooner(focused ? 100 : 500);
    }
    const auto now = SDL_GetTicks();
    if (focused && (search || typing_title))
    {
        sooner(next_caret > now ? static_cast<int>(next_caret - now) : 0);
    }
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
    if (artwork_task.valid() && artwork_task.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
    {
        auto result = artwork_task.get();
        needs_redraw = true;
        if (result.generation == artwork_generation)
        {
            if (!result.error.empty())
            {
                notify(result.error);
            }
            else if (result.searching && modal == Modal::SteamGrid)
            {
                artwork_games = std::move(result.games);
                if (artwork_games.size() > 5)
                {
                    artwork_games.resize(5);
                }
                choice = artwork_games.empty() ? 1 : 2;
                if (artwork_games.empty())
                {
                    notify("No matching games found on SteamGridDB");
                }
            }
            else if (!result.image.empty())
            {
                try
                {
                    boxart.select_data(result.image);
                    dirty = true;
                    if (modal == Modal::SteamGrid)
                    {
                        modal = Modal::Edit;
                        choice = (editing || draft.system == "generic") ? 5 : 4;
                    }
                    notify("SteamGridDB artwork selected. Save to keep it.");
                }
                catch (const std::exception& error)
                {
                    notify(error.what());
                }
            }
        }
    }
    if (!artwork_task.valid() && !queued_artwork.empty())
    {
        artwork_query = std::move(queued_artwork);
        queued_artwork.clear();
        fetch_artwork(0, true);
    }
    if ((search || typing_title) && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) &&
        SDL_GetTicks() >= next_caret)
    {
        caret_visible = !caret_visible;
        next_caret = SDL_GetTicks() + 500;
        needs_redraw = true;
    }
    if (message_until && SDL_GetTicks() >= message_until)
    {
        message_until = 0;
        needs_redraw = true;
    }
    if (const auto error = browser.poll())
    {
        needs_redraw = true;
        if (!error->empty())
        {
            notify(*error);
            logger.write(HomeLogLevel::Warn, *error);
        }
        if (browser.purpose() == BrowsePurpose::Archive && modal == Modal::Browse &&
            browser.items().size() == 1 && error->empty())
        {
            choose_file(browser.items()[0].path);
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
    std::array<bool, 8> input{b(SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -16000,
                              b(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > 16000,
                              b(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -16000,
                              b(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > 16000,
                              b(SDL_GAMEPAD_BUTTON_SOUTH),
                              b(SDL_GAMEPAD_BUTTON_EAST),
                              b(SDL_GAMEPAD_BUTTON_NORTH),
                              b(SDL_GAMEPAD_BUTTON_START)};
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
    const int confirm = swap_confirm_cancel ? 5 : 4;
    const int cancel = swap_confirm_cancel ? 4 : 5;
    if (input[7] && !previous[7])
    {
        open_menu();
    }
    else if (input[confirm] && !previous[confirm])
    {
        needs_redraw = true;
        activate(true);
    }
    else if (input[cancel] && !previous[cancel])
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
    if ((kind == 2 && index == 1 && search) || (kind == 3 && index == 0 && typing_title))
    {
        draw_text_input(rect);
    }
    else
    {
        label(rect.x + layout.gap + inset, rect.y + (rect.h - size) / 2 - size * .15f, title, size, color,
              rect.w - inset - ((kind == 0 && index >= 3) ? layout.gap * 3 : layout.gap * 2));
    }
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
    const float total_height = layout.systems_top + system_count() * (layout.system_height + 4) + layout.gap;
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
    for (int i = 0; i < system_count(); ++i)
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
    const float sidebar_total = l.systems_top + system_count() * (l.system_height + 4) + l.gap;
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
           query.empty() ? "Search..." : query,
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
    const bool show_menu = modal == Modal::None || modal == Modal::Menu;
    const float menu_width = show_menu ? (pad ? text.width("Start: Menu", l.small)
                                             : icon_size + gap + text.width("Menu", l.small)) + between : 0;
    float x = l.width - margin - 2 * (icon_size + gap) - text.width("Back", l.small) - text.width("Select", l.small) -
              between - clear_width - menu_width;
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
    if (show_menu)
    {
        if (pad)
        {
            hits.push_back({{x, prompt_y - 4, menu_width - between, icon_size + 8}, 10, 0});
            label(x, prompt_y, "Start: Menu", l.small);
            x += menu_width;
        }
        else
        {
            prompt("keyboard_escape", "Menu", 10);
        }
    }
    prompt(pad ? (swap_confirm_cancel ? "controller_buttons_south" : "controller_buttons_east") : "keyboard_escape",
           "Back", 5);
    prompt(pad ? (swap_confirm_cancel ? "controller_buttons_east" : "controller_buttons_south") : "keyboard_enter",
           "Select", 6);
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
    const auto purpose = browser.purpose();
    const float available_height = layout.height - layout.bar_height - layout.gap * 2;
    const bool editing_details = modal == Modal::Edit && (editing || draft.system == "generic" || !api_key.empty());
    const float editor_height = api_key.empty() ? 640.0f : 720.0f;
    const float scale = std::min(layout.body / 16.0f, available_height / (editing_details ? editor_height : 540.0f));
    const float dw = std::min(800.0f, (layout.width - layout.gap * 2) / scale);
    const float modal_height = (modal == Modal::Menu) ? 380.0f
                               : (modal == Modal::Settings) ? 280.0f
                               : editing_details ? editor_height : 590.0f;
    const float dh = (modal == Modal::Browse) ? available_height / scale
                                             : std::min(modal_height, available_height / scale);
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
    std::string title = (modal == Modal::Menu) ? "Cabinet menu"
                        : (modal == Modal::Settings) ? "Home settings"
                        : (modal == Modal::ApiKey) ? "SteamGridDB API key"
                        : (modal == Modal::SteamGrid) ? "Find SteamGridDB box art"
                        : (modal == Modal::Bios) ? (bios_system == "psx" ? "PlayStation BIOS" : "Sega Saturn BIOS")
                        : (modal == Modal::Browse)
                            ? ((purpose == BrowsePurpose::Archive) ? "Choose a ROM inside the ZIP"
                               : (purpose == BrowsePurpose::Bios)  ? "Choose a BIOS file"
                               : (purpose == BrowsePurpose::BoxArt) ? "Choose box art"
                               : (purpose == BrowsePurpose::Core || purpose == BrowsePurpose::Custom) ? "Choose a core"
                               : (purpose == BrowsePurpose::Content) ? "Choose a ROM"
                                                                   : "Choose a file")
                        : (modal == Modal::Edit)    ? editing ? "Edit game details" : "Add to library"
                        : (modal == Modal::Remove)  ? "Remove this game?"
                        : (modal == Modal::Discard) ? "Discard changes?"
                                                    : "Game details";
    line(26, title, 26);

    if (modal == Modal::Browse)
    {
        const auto& folder = browser.current_folder();
        const auto& entries = browser.items();
        line(78, folder.string(), 13, muted);
        if (purpose == BrowsePurpose::BoxArt)
        {
            line(104, "PNG, JPEG, or BMP. The image is copied when you save changes.", 13, muted);
        }
        if (browser.loading())
        {
            line(128, (purpose == BrowsePurpose::Archive) ? "Reading ZIP..." : "Reading folder...", 16, muted);
        }
        else if (entries.empty())
        {
            line(128, (purpose == BrowsePurpose::Archive) ? "No compatible ROM files found." : "This folder is empty.",
                 16, muted);
        }
        const int browser_rows = std::max(1, static_cast<int>((dh - 178) / 50));
        browser.scroll_into_view(browser_rows);
        for (int i = browser.top(); i < static_cast<int>(entries.size()) && i < browser.top() + browser_rows; ++i)
        {
            const auto& entry = entries[i];
            const bool parent = entry.directory && entry.path == folder.parent_path();
            auto name = (purpose == BrowsePurpose::Archive) ? entry.path.generic_string()
                        : parent                            ? ".. (parent folder)"
                                                            : entry.path.filename().string();
            button({x, 114.0f + (i - browser.top()) * 50, inner, 38}, name, i == browser.selected(), 4, i,
                   entry.directory ? (parent ? "folder_open" : "folder") : "");
        }
        line(dh - 38, swap_confirm_cancel
                         ? "Enter / East: open    Left / Right: jump by letter    Esc / South: cancel"
                         : "Enter / South: open    Left / Right: jump by letter    Esc / East: cancel", 13, muted);
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
        line(137, game()->path.empty() ? "No ROM (runs without content)" : game()->path, 12, muted);
        if (!game()->archive_member.empty())
        {
            line(160, "ROM: " + game()->archive_member, 12, muted);
        }
        line(184, "Save folder: " + game()->save_name, 12, muted);
        line(208, "Playtime: " + format_playtime(game()->playtime), 14, ink);
        if (!game()->path.empty() && !fs::is_regular_file(game()->path))
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
        const float width = inner - 112;
        line(78, draft.path + (draft.archive_member.empty() ? "" : " / " + draft.archive_member), 12, muted, width);
        line(107, editing ? "Changes are applied when you save."
                          : (draft.system == "generic") ? "ROM can be skipped for cores that support running without content."
                                                        : "Choose a system and core, then confirm to save.",
             14, muted, width);
        {
            const SDL_FRect preview{left + (dw - 112) * scale, top + 74 * scale, 88 * scale, 110 * scale};
            if (!boxart.draw(draft.save_id, preview, true))
            {
                text.icon(preview.x, preview.y + 11 * scale, 88 * scale, "placeholder");
            }
        }
    }
    if (modal == Modal::ApiKey)
    {
        line(80, "Get a key at steamgriddb.com/profile/preferences/api", 14, muted);
        line(115, api_key.empty() ? "No key configured." : "A key is saved. Enter a new key to replace it.", 14, muted);
        line(145, "Stored locally with access restricted to your user account.", 14, muted);
    }
    if (modal == Modal::SteamGrid)
    {
        line(80, "Choose a game. Alternate square covers are preferred.", 14, muted);
        line(115, artwork_task.valid() ? "Loading from SteamGridDB..." : "Search by title, then choose a matching game.", 14, muted);
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
        const float rows_top = (modal == Modal::Details) ? 236.0f
                               : (modal == Modal::Menu || modal == Modal::Settings) ? 100.0f : 200.0f;
        button({x, rows_top + static_cast<float>(i) * 50, inner, 38}, value, choice == static_cast<int>(i), 3,
               static_cast<int>(i));
    }
    if (modal == Modal::Edit)
    {
        line(dh - 38,
             typing_title ? "Arrows move. Shift selects. Ctrl+A selects all. Enter finishes."
                          : draft.system == "generic" ? "Generic system. Core: select to browse."
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
        home.import_file(import, import_system == "generic");
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
