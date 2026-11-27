#pragma once
#include "file_browser.h"
#include "text_edit.h"
#include "boxart.h"
#include "steamgriddb.h"
#include <cstdint>
#include <cstddef>
#include "library.h"
#include "game_process.h"
#include "ui_text.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <future>
#include <utility>

namespace fs = std::filesystem;

class Home
{
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
    static constexpr SDL_Color ink{255, 255, 255, 255}, muted{187, 187, 187, 255};

public:
    static constexpr SDL_Color accent{123, 147, 203, 255};
    Home(SDL_Window* window, SDL_Renderer* renderer, const fs::path& data, std::string executable,
         HomeLogger logger = {}, SteamGridDB::Request artwork_request = {});
    ~Home();
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
        void resize(int window_width, int window_height);
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
    void poll_artwork();
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
