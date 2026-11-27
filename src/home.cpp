#include "home_window.h"
#include "save_storage.h"

Home::Home(SDL_Window* window, SDL_Renderer* renderer, const fs::path& data, std::string executable,
           HomeLogger logger, SteamGridDB::Request artwork_request)
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
    recount_systems();
    refilter();
    if (!discover())
    {
        notify("Controller not detected. Keyboard and mouse are ready.");
    }
    logger.write(HomeLogLevel::Debug, "Library data: " + data_dir);
    logger.write(HomeLogLevel::Debug, "Runner executable: " + this->executable);
    logger.write(HomeLogLevel::Debug, "Loaded " + std::to_string(all.size()) + " library entries");
}

Home::~Home()
{
    if (pad)
    {
        SDL_CloseGamepad(pad);
    }
}

void Home::recount_systems()
{
    system_counts.clear();
    for (const auto& entry : all)
    {
        ++system_counts[entry.system];
    }
}

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
            const bool system_changed = existing->system != changed.system;
            *existing = changed;
            if (system_changed)
            {
                recount_systems();
            }
            return;
        }
        all.erase(existing);
    }
    all.insert(std::lower_bound(all.begin(), all.end(), changed, game_title_less), changed);
    recount_systems();
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
        recount_systems();
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
    measured_playtime_width = 0;
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
    poll_artwork();
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
